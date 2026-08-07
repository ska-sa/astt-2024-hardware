#include <math.h>
#include <Wire.h>
#include <WiFi.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GPS.h>
#include <time.h>
#include <SparkFun_MMC5983MA_Arduino_Library.h>

// Magnetometer
SFE_MMC5983MA myMag;

float magneticDeclination = -25.4;
float headingOffset = 90.0;

bool calibrated = false;
unsigned long calibStartTime = 0;

uint32_t minX = 4294967295, minY = 4294967295, minZ = 4294967295;
uint32_t maxX = 0,          maxY = 0,          maxZ = 0;

float offX = 0,   offY = 0,   offZ = 0;
float scaleX = 1, scaleY = 1, scaleZ = 1;

// WiFi
const char* ssid     = "SARAO_Guest";
const char* password = "ska.2009";

// API
const char* commandsUrl = "http://172.22.9.145:8000/api/v1/commands/1/latest";
const char* readingsUrl = "http://172.22.9.145:8000/api/v1/readings";

// NTP
const char* ntpServer = "pool.ntp.org";

// Telescope
int telescopeId = 1;

// Motor pins
const int IN1 = 7;
const int IN2 = 6;
const int ENA = 10;

// Sensor pins
const int potPin   = A0;
const int encPin   = 21;
const int estopPin = 19;

// Software az limits — adjust these to your physical range
const float AZ_MIN = 10.0;
const float AZ_MAX = 350.0;

// Control — no deadband, any error moves motor
float kP    = 20.0;
int minPWM  = 150;

// Encoder state
float prevRaw = 0;
float az      = 0;

// Estop
bool estopState = false;
bool lastButton = HIGH;

// Command source arbitration
float apiTarget           = -1.0;
float potTarget           = 0.0;
unsigned long lastApiTime = 0;
unsigned long lastPotTime = 0;
const unsigned long SOURCE_TIMEOUT = 10000;

// Tracking state
bool   isTracking       = false;
float  track_m1         = 0, track_m2  = 0;
float  track_c1         = 0, track_c2  = 0;
float  track_T_ra       = 0, track_A   = 0;
float  track_phi        = 0, track_D   = 0;
float  track_T_dec      = 0;

// Network timing
unsigned long lastPostTime = 0;
unsigned long lastPollTime = 0;
const unsigned long POST_INTERVAL = 5000;
const unsigned long POLL_INTERVAL = 2000;

// Readings state
float  azimuthAngle   = 0.0;
float  elevationAngle = 0.0;  // hardcoded 0 until elevation motor is added
float  latitude       = 0.0;
float  longitude      = 0.0;
float  altitude       = 0.0;
String healthStatus   = "OK";
String movementStatus = "IDLE";

// GPS
Adafruit_GPS GPS(&Wire);


// motor helpers

void stopMotor() {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
    analogWrite(ENA, 0);
}

void rotateCW(int pwm) {
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
    analogWrite(ENA, pwm);
}

void rotateCCW(int pwm) {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
    analogWrite(ENA, pwm);
}

void applyMotor(float power) {
    if (power == 0) { stopMotor(); return; }
    int pwm = abs(power) * 255;
    if (pwm < minPWM) pwm = minPWM;
    if (power > 0) rotateCW(pwm);
    else           rotateCCW(pwm);
}


// encoder

float readEncoderAngle() {
    const int samples = 5;
    float sum = 0;
    int valid = 0;

    for (int i = 0; i < samples; i++) {
        unsigned long hi = pulseIn(encPin, HIGH, 25000);
        unsigned long lo = pulseIn(encPin, LOW,  25000);
        if (hi + lo == 0) continue;
        float angle = ((float)hi / (float)(hi + lo)) * 360.0;
        if (angle < 0)   angle = 0;
        if (angle > 360) angle = 360;
        sum += angle;
        valid++;
    }

    if (valid == 0) return prevRaw;

    float avg = sum / valid;
    if (abs(avg - prevRaw) > 20.0) avg = prevRaw;
    float filtered = (0.7 * prevRaw) + (0.3 * avg);
    prevRaw = filtered;
    return filtered;
}


// GPS

void readGPS() {
    char c = GPS.read();
    if (!GPS.newNMEAreceived()) return;
    if (!GPS.parse(GPS.lastNMEA())) return;
    if (GPS.fix) {
        latitude  = GPS.latitudeDegrees;
        longitude = GPS.longitudeDegrees;
        altitude  = GPS.altitude;
    }
}


// tracking math — direct port of your Python angle_C function
// given source params, returns estimated az angle in degrees

float computeAzFromSource(float m1, float m2, float c1, float c2,
                           float T_ra, float A, float phi,
                           float D,   float T_dec) {

    // get current UTC seconds since midnight
    struct tm timeinfo;
    float t = 0;
    if (getLocalTime(&timeinfo)) {
        t = timeinfo.tm_hour * 3600.0 + timeinfo.tm_min * 60.0 + timeinfo.tm_sec;
    }

    // RA and Dec from sinusoidal model using source params
    float ra  = m1 * sin((2.0 * PI / T_ra)  * t + phi) + c1;
    float dec = A  * sin((2.0 * PI / T_dec) * t)       + D + c2;

    // convert ra/dec to az using your triangle geometry
    float x      = 0.3;
    float ac1    = ra  + x;
    float ac2    = dec + x;
    float ab     = 0.40;  // fixed baseline from your script

    float numerator   = ac1 * ac1 + ac2 * ac2 - ab * ab;
    float denominator = 2.0 * ac1 * ac2;

    float cos_C = numerator / denominator;
    cos_C = max(-1.0f, min(1.0f, cos_C));

    float az_rad = acos(cos_C);
    float az_deg = az_rad * 180.0 / PI;

    // clamp to 0-360
    az_deg = fmod(az_deg, 360.0);
    if (az_deg < 0) az_deg += 360.0;

    return az_deg;
}


// choose rotation direction that avoids the forbidden zone between AZ_MAX and AZ_MIN
// returns +1 for CW, -1 for CCW

int chooseDirection(float current, float target) {
    float cwDist  = fmod(target - current + 360.0, 360.0);
    float ccwDist = fmod(current - target + 360.0, 360.0);

    // check if CW path crosses forbidden zone
    bool cwClear  = !((current < AZ_MAX && current + cwDist  > AZ_MAX) ||
                      (current > AZ_MIN && current - ccwDist < AZ_MIN));
    bool ccwClear = !((current > AZ_MIN && current - ccwDist < AZ_MIN) ||
                      (current < AZ_MAX && current + cwDist  > AZ_MAX));

    // prefer shorter path, fall back to the clear one
    if (cwDist <= ccwDist && cwClear)  return  1;
    if (ccwDist < cwDist  && ccwClear) return -1;
    if (cwClear)  return  1;
    if (ccwClear) return -1;

    // if both blocked just go shortest — should not happen if limits are sane
    return (cwDist <= ccwDist) ? 1 : -1;
}


// network

void connectWifi() {
    WiFi.begin(ssid, password);
    Serial.print("Connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(50);
        Serial.print(".");
    }
    Serial.println("\nConnected: " + WiFi.localIP().toString());
}

void syncNTP() {
    configTime(0, 0, ntpServer);  // UTC, no offset
    Serial.print("Waiting for NTP");
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo)) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nNTP synced");
}

void postReadings() {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    http.begin(readingsUrl);
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;
    doc["telescope_id"]    = telescopeId;
    doc["azimuth_angle"]   = azimuthAngle;
    doc["elevation_angle"] = elevationAngle;
    doc["latitude"]        = latitude;
    doc["longitude"]       = longitude;
    doc["altitude"]        = altitude;
    doc["gyroscope_x"]     = 0;
    doc["gyroscope_y"]     = 0;
    doc["gyroscope_z"]     = 0;
    doc["acceleration_x"]  = 0;
    doc["acceleration_y"]  = 0;
    doc["acceleration_z"]  = 0;
    doc["magnetic_field_x"] = 0;
    doc["magnetic_field_y"] = 0;
    doc["magnetic_field_z"] = 0;
    doc["health_status"]   = healthStatus;
    doc["movement_status"] = movementStatus;

    String payload;
    serializeJson(doc, payload);

    int code = http.POST(payload);
    Serial.printf("POST => %d\n", code);
    http.end();
}

void pollCommands() {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    http.begin(commandsUrl);

    int code = http.GET();
    if (code != 200) {
        Serial.printf("GET failed: %d\n", code);
        http.end();
        return;
    }

    String response = http.getString();
    JsonDocument doc;

    if (deserializeJson(doc, response)) {
        Serial.println("JSON parse error");
        http.end();
        return;
    }

    const char* cmdType = doc["command_type"] | "";

    if (strcmp(cmdType, "point") == 0) {
        // point command — read az directly
        float newAz = doc["point"]["target_az_angle"] | -1.0f;
        if (newAz >= 0 && newAz <= 360) {
            apiTarget   = newAz;
            lastApiTime = millis();
            isTracking  = false;
            Serial.printf("POINT target az: %.2f\n", apiTarget);
        }

    } else if (strcmp(cmdType, "track") == 0) {
        // track command — store source params, compute az each loop
        track_m1    = doc["track"]["source"]["m_1"]   | 0.0f;
        track_m2    = doc["track"]["source"]["m_2"]   | 0.0f;
        track_c1    = doc["track"]["source"]["c_1"]   | 0.0f;
        track_c2    = doc["track"]["source"]["c_2"]   | 0.0f;
        track_T_ra  = doc["track"]["source"]["T_ra"]  | 1.0f;
        track_A     = doc["track"]["source"]["A"]     | 0.0f;
        track_phi   = doc["track"]["source"]["phi"]   | 0.0f;
        track_D     = doc["track"]["source"]["D"]     | 0.0f;
        track_T_dec = doc["track"]["source"]["T_dec"] | 1.0f;

        isTracking  = true;
        lastApiTime = millis();
        Serial.println("TRACK command received");
    }

    http.end();
}


// setup

void setup() {
    Serial.begin(115200);

    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    pinMode(ENA, OUTPUT);
    pinMode(potPin,   INPUT);
    pinMode(encPin,   INPUT);
    pinMode(estopPin, INPUT_PULLUP);

    stopMotor();

    GPS.begin(0x10);
    GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
    GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);
    GPS.sendCommand(PMTK_API_SET_FIX_CTL_1HZ);
    GPS.sendCommand(PGCMD_ANTENNA);
    delay(10);

    connectWifi();
    syncNTP();

    Wire.begin();
    if (myMag.begin() == false) {
        Serial.println("MMC5983MA did not respond. Freezing.");
        while (true);
    }
    myMag.softReset();

    calibStartTime = millis();
    Serial.println("Ready");
}


// loop

void loop() {
    unsigned long now = millis();

    readGPS();

    // estop toggle
    bool button = digitalRead(estopPin);
    if (button == LOW && lastButton == HIGH) {
        estopState = !estopState;
        delay(15);
    }
    lastButton = button;

    if (estopState) {
        stopMotor();
        movementStatus = "ESTOP";
        healthStatus   = "FAULT";
        Serial.println("ESTOP");
        delay(10);
        return;
    }

    healthStatus = "OK";

    // read encoder
    az = readEncoderAngle();
    azimuthAngle = az;

    // read pot — if user turns pot it overrides everything
    float rawPot = map(analogRead(potPin), 0, 1023, 0, 360);
    if (abs(rawPot - potTarget) > 2.0) {
        potTarget   = rawPot;
        lastPotTime = now;
        isTracking  = false;  // pot overrides tracking
    }

    // if tracking, recompute api target each loop using UTC time
    if (isTracking) {
        float computedAz = computeAzFromSource(
            track_m1, track_m2, track_c1, track_c2,
            track_T_ra, track_A, track_phi, track_D, track_T_dec
        );
        apiTarget   = computedAz;
        lastApiTime = now;
    }

    // source arbitration — most recent wins
    bool apiRecent = (apiTarget >= 0) && (now - lastApiTime < SOURCE_TIMEOUT);
    bool potRecent = (now - lastPotTime < SOURCE_TIMEOUT);
    float target;

    if (apiRecent && (!potRecent || lastApiTime > lastPotTime)) {
        target = apiTarget;
    } else {
        target = potTarget;
    }

    // clamp target to software limits
    target = constrain(target, AZ_MIN, AZ_MAX);

    // software limit — stop if already at edge and error pushes further out
    if ((az <= AZ_MIN && target <= az) || (az >= AZ_MAX && target >= az)) {
        stopMotor();
        movementStatus = "LIMIT";
        Serial.println("Software limit");
    } else {
        // choose direction that avoids forbidden zone
        int dir   = chooseDirection(az, target);
        float error = target - az;

        // any error at all drives motor — no deadband
        if (abs(error) < 0.5) {
            stopMotor();
            movementStatus = "IDLE";
        } else {
            float power = kP * (abs(error) / 180.0);
            power = constrain(power, 0.0, 1.0);
            applyMotor(power * dir);
            movementStatus = isTracking ? "TRACKING" : "MOVING";
        }
    }

    Serial.printf("AZ=%.1f  TGT=%.1f  ERR=%.1f  SRC=%s  MODE=%s\n",
        az, target, target - az,
        (apiRecent && lastApiTime > lastPotTime) ? "API" : "POT",
        isTracking ? "TRACK" : "POINT"
    );

    // network
    if (now - lastPostTime >= POST_INTERVAL) {
        lastPostTime = now;
        postReadings();
    }

    if (now - lastPollTime >= POLL_INTERVAL) {
        lastPollTime = now;
        pollCommands();
    }

    // magnetometer calibration and heading (unchanged from your code)
    uint32_t rawX, rawY, rawZ;
    myMag.getMeasurementXYZ(&rawX, &rawY, &rawZ);

    if (!calibrated) {
        if (rawX < minX) minX = rawX;
        if (rawY < minY) minY = rawY;
        if (rawZ < minZ) minZ = rawZ;
        if (rawX > maxX) maxX = rawX;
        if (rawY > maxY) maxY = rawY;
        if (rawZ > maxZ) maxZ = rawZ;

        if (millis() - calibStartTime > 20000) {
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

    float cx = ((float)rawX - offX) / scaleX;
    float cy = ((float)rawY - offY) / scaleY;

    float heading = atan2(cx, -cy) * 180.0 / PI + 180 + headingOffset;
    if (heading >= 360) heading -= 360;
    if (heading < 0)    heading += 360;

    float trueHeading = heading + magneticDeclination;
    if (trueHeading >= 360) trueHeading -= 360;
    if (trueHeading < 0)    trueHeading += 360;

    Serial.printf("Mag: %.1f  True: %.1f\n", heading, trueHeading);

    delay(20);
}
