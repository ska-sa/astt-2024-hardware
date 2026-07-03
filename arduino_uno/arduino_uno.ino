#include <math.h>
#include <Wire.h>
#include <WiFi.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GPS.h>
#include <SparkFun_MMC5983MA_Arduino_Library.h>

// Magnetometer
SFE_MMC5983MA myMag;

// Declination angle in degrees
float magneticDeclination = -25.4;

// FIX: 90° correction (your case needs +90)
float headingOffset = 90.0;

// Calibration variables
bool calibrated = false;
unsigned long calibStartTime = 0;

uint32_t minX = 4294967295;
uint32_t minY = 4294967295;
uint32_t minZ = 4294967295;

uint32_t maxX = 0;
uint32_t maxY = 0;
uint32_t maxZ = 0;

float offX = 0, offY = 0, offZ = 0;
float scaleX = 1, scaleY = 1, scaleZ = 1;

// WiFi
const char *ssid = "SARAO_Guest";
const char *password = "ska.2009";

// API
const char *commandsUrl = "http://172.22.9.145:8000/api/v1/commands/1/latest";
const char *readingsUrl = "http://172.22.9.145:8000/api/v1/readings";

// Telescope
int telescopeId = 1;

// Motor pins
const int IN1 = 7;   // GPIO7  -> Motor driver input 1
const int IN2 = 6;   // GPIO6  -> Motor driver input 2
const int ENA = 10;  // GPIO10 -> Motor driver PWM enable

// Sensor pins
const int potPin = A0;    // GPIO0  -> Potentiometer analog input (ADC0)
const int encPin = 21;    // GPIO21 -> Rotary encoder input (interrupt-capable) // changed
const int estopPin = 19;  // GPIO19 -> Emergency stop switch (interrupt-capable) // changed

// Control
float kP = 20.0;
float deadband = 3.0;
int minPWM = 150;

// Encoder state
float prevRaw = 0;
float az = 0;

// Estop
bool estopState = false;
bool lastButton = HIGH;

// Command source arbitration
float apiTarget = -1.0;
float potTarget = 0.0;
unsigned long lastApiTime = 0;
unsigned long lastPotTime = 0;
const unsigned long SOURCE_TIMEOUT = 10000;

// Network timing
unsigned long lastPostTime = 0;
unsigned long lastPollTime = 0;
const unsigned long POST_INTERVAL = 5000;
const unsigned long POLL_INTERVAL = 2000;

// Readings state
float azimuthAngle = 0.0;
float elevationAngle = 0.0;
float latitude = 0.0;
float longitude = 0.0;
float altitude = 0.0;
String healthStatus = "OK";
String movementStatus = "IDLE";

// GPS
Adafruit_GPS GPS(&Wire);

// motor helpers

void stopMotor(){
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
    analogWrite(ENA, 0);
}

void rotateCW(int pwm){
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
    analogWrite(ENA, pwm);
}

void rotateCCW(int pwm){
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
    analogWrite(ENA, pwm);
}

void applyMotor(float power){
    if (power == 0){
        stopMotor();
        return;
    }
    int pwm = abs(power) * 255;
    if (pwm < minPWM){
        pwm = minPWM;
    }
    if (power > 0){
        rotateCW(pwm);
    }
    else{
        rotateCCW(pwm);
    }
}

// encoder

float readEncoderAngle(){
    const int samples = 5;
    float sum = 0;
    int valid = 0;

    for (int i = 0; i < samples; i++){
        unsigned long hi = pulseIn(encPin, HIGH, 25000);
        unsigned long lo = pulseIn(encPin, LOW, 25000);
        if (hi + lo == 0)
            continue;
        float angle = ((float)hi / (float)(hi + lo)) * 360.0;
        if (angle < 0)
            angle = 0;
        if (angle > 360)
            angle = 360;
        sum += angle;
        valid++;
    }

    if (valid == 0){
        return prevRaw;
    }
    float avg = sum / valid;

    // reject impossible jump
    if (abs(avg - prevRaw) > 20.0){
        avg = prevRaw;
    }
    // low pass filter
    float filtered = (0.7 * prevRaw) + (0.3 * avg);
    prevRaw = filtered;
    return filtered;
}

// GPS

void readGPS(){
    char c = GPS.read();
    if (!GPS.newNMEAreceived()){
        return;
    }
    if (!GPS.parse(GPS.lastNMEA())){
        return;
    }
    if (GPS.fix){
        latitude = GPS.latitudeDegrees;
        longitude = GPS.longitudeDegrees;
        altitude = GPS.altitude;
    }
}

// network

void connectWifi(){
    WiFi.begin(ssid, password);
    Serial.print("Connecting");
    while (WiFi.status() != WL_CONNECTED){
        delay(50);
        Serial.print(".");
    }
    Serial.println("\nConnected: " + WiFi.localIP().toString());
}

void postReadings(){
    if (WiFi.status() != WL_CONNECTED){
        return;
    }
    HTTPClient http;
    http.begin(readingsUrl);
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;
    doc["telescope_id"] = telescopeId;
    doc["azimuth_angle"] = azimuthAngle;
    doc["elevation_angle"] = elevationAngle;
    doc["latitude"] = latitude;
    doc["longitude"] = longitude;
    doc["altitude"] = altitude;
    doc["gyroscope_x"] = 0;
    doc["gyroscope_y"] = 0;
    doc["gyroscope_z"] = 0;
    doc["acceleration_x"] = 0;
    doc["acceleration_y"] = 0;
    doc["acceleration_z"] = 0;
    doc["magnetic_field_x"] = 0;
    doc["magnetic_field_y"] = 0;
    doc["magnetic_field_z"] = 0;
    doc["health_status"] = healthStatus;
    doc["movement_status"] = movementStatus;

    String payload;
    serializeJson(doc, payload);

    int code = http.POST(payload);
    Serial.printf("POST => %d\n", code);
    http.end();
}

void pollCommands(){
    if (WiFi.status() != WL_CONNECTED){
        return;
    }

    HTTPClient http;
    http.begin(commandsUrl);

    int code = http.GET();
    if (code != 200){
        Serial.printf("GET failed: %d\n", code);
        http.end();
        return;
    }

    String response = http.getString();
    JsonDocument doc;

    if (deserializeJson(doc, response)){
        Serial.println("JSON parse error");
        http.end();
        return;
    }

    float newAz = doc["target_az_angle"] | -1.0f;

    if (newAz >= 0 && abs(newAz - apiTarget) > 0.5){
        apiTarget = newAz;
        lastApiTime = millis();
        Serial.printf("API target: %.2f\n", apiTarget);
    }

    http.end();
}

// setup

void setup(){
    Serial.begin(115200);

    // Motor Setup
    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    pinMode(ENA, OUTPUT);
    pinMode(potPin, INPUT);
    pinMode(encPin, INPUT);
    pinMode(estopPin, INPUT_PULLUP);

    stopMotor();

    // GPS Setup
    GPS.begin(0x10);
    GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
    GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);
    GPS.sendCommand(PMTK_API_SET_FIX_CTL_1HZ);
    GPS.sendCommand(PGCMD_ANTENNA);
    delay(10);

    // WiFi Setup
    connectWifi();

    // Magnetometer Setup
    Wire.begin();

    if (myMag.begin() == false){
        Serial.println("MMC5983MA did not respond - check wiring. Freezing.");
        while (true);
    }

    myMag.softReset();
    // Serial.println("Calibrating... move the sensor in all directions");

    calibStartTime = millis();
    Serial.println("Ready");
}

// loop
void loop(){
    unsigned long now = millis();

    // GPS
    readGPS();

    // estop toggle
    bool button = digitalRead(estopPin);
    if (button == LOW && lastButton == HIGH){
        estopState = !estopState;
        delay(15);
    }
    lastButton = button;

    if (estopState){
        stopMotor();
        movementStatus = "ESTOP";
        healthStatus = "FAULT";
        Serial.println("ESTOP");
        delay(10);
        return;
    }

    healthStatus = "OK";

    // read encoder
    az = readEncoderAngle();
    azimuthAngle = az;

    // read pot, detect movement
    float rawPot = map(analogRead(potPin), 0, 1023, 0, 360);
    if (abs(rawPot - potTarget) > 2.0){
        potTarget = rawPot;
        lastPotTime = now;
    }

    // source arbitration, most recent wins
    bool apiRecent = (apiTarget >= 0) && (now - lastApiTime < SOURCE_TIMEOUT);
    bool potRecent = (now - lastPotTime < SOURCE_TIMEOUT);
    float target;

    if (apiRecent && (!potRecent || lastApiTime > lastPotTime)){
        target = apiTarget;
    }
    else{
        target = potTarget;
    }

    // hard limits
    if ((az <= 0 && target < az) || (az >= 360 && target > az)){
        stopMotor();
        movementStatus = "LIMIT";
        Serial.println("Hard limit");
        return;
    }

    // P controller
    float error = target - az;
    float power = kP * (error / 180.0);
    power = constrain(power, -1.0, 1.0);

    if (abs(error) < deadband){
        stopMotor();
        movementStatus = "IDLE";
    }
    else{
        applyMotor(power);
        movementStatus = "MOVING";
    }

    Serial.printf("AZ=%.1f  TGT=%.1f  ERR=%.1f  SRC=%s\n",
        az, target, error,
        (apiRecent && lastApiTime > lastPotTime) ? "API" : "POT"
    );

    // network
    if (now - lastPostTime >= POST_INTERVAL){
        lastPostTime = now;
        postReadings();
    }

    if (now - lastPollTime >= POLL_INTERVAL){
        lastPollTime = now;
        pollCommands();
    }

    uint32_t rawX, rawY, rawZ;

    myMag.getMeasurementXYZ(&rawX, &rawY, &rawZ);

    // -------------------------
    // CALIBRATION PHASE (20 seconds)
    // -------------------------
    if (!calibrated){
        if (rawX < minX) minX = rawX;
        if (rawY < minY) minY = rawY;
        if (rawZ < minZ) minZ = rawZ;

        if (rawX > maxX) maxX = rawX;
        if (rawY > maxY) maxY = rawY;
        if (rawZ > maxZ) maxZ = rawZ;

        if (millis() - calibStartTime > 20000){
            offX = (maxX + minX) / 2.0;
            offY = (maxY + minY) / 2.0;
            offZ = (maxZ + minZ) / 2.0;

            scaleX = (maxX - minX) / 2.0;
            scaleY = (maxY - minY) / 2.0;
            scaleZ = (maxZ - minZ) / 2.0;

            calibrated = true;

            Serial.println("Calibration complete!");
        }

        delay(50);
        return;
    }

    // -------------------------
    // APPLY CALIBRATION
    // -------------------------
    float cx = ((float)rawX - offX) / scaleX;
    float cy = ((float)rawY - offY) / scaleY;
    float cz = ((float)rawZ - offZ) / scaleZ;

    // -------------------------
    // HEADING
    // -------------------------
    float heading = atan2(cx, -cy);
    heading = heading * 180.0 / PI;
    heading += 180;

    // FIX: correct 90° rotation
    heading += headingOffset;

    // Normalize magnetic heading
    if (heading >= 360){
        heading -= 360;
    }
    if (heading < 0){
        heading += 360;
    }

    // Apply declination
    float trueHeading = heading + magneticDeclination;

    // Normalize true heading
    if (trueHeading >= 360){
        trueHeading -= 360;
    }
    if (trueHeading < 0){
        trueHeading += 360;
    }

    Serial.print("\nMag Heading: ");
    Serial.println(heading, 1);

    Serial.print("True Heading: ");
    Serial.println(trueHeading, 1);
    
    delay(20);
}
