#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

// WiFi
const char* ssid = "SARAO_Guest";
const char* password = "ska.2009";
const char* readingsUrl = "http://172.22.9.145:8000/api/v1/readings";
const char* commandsUrl = "http://172.22.9.145:8000/api/v1/commands/7/latest";

// Telescope
int telescope_id = 7;

// Motor pins
const int IN1 = 12;
const int IN2 = 14;
const int ENA = 2;

// Sensor pins
const int potPin    = A0;
const int encPin    = 15;
const int estopPin  = 13;

// Control
float kP       = 20.0;
float deadband = 3.0;
int   minPWM   = 150;

// State
float az             = 0.0;
float target         = 0.0;
bool  estopState     = false;
bool  lastButton     = HIGH;

// Command source tracking
float apiTarget      = -1.0;  // -1 means no api command yet
float potTarget      = 0.0;
unsigned long lastApiCommandTime = 0;
unsigned long lastPotChangeTime  = 0;
const unsigned long SOURCE_TIMEOUT = 10000;  // ms, after this other source can take over

// Network timing
unsigned long lastNetworkTime = 0;
const unsigned long networkInterval = 5000;

// Readings to post
float az_angle       = 0.0;
float el_angle       = 0.0;
String health_status   = "OK";
String movement_status = "IDLE";

void setup() {
  Serial.begin(115200);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(potPin,   INPUT);
  pinMode(encPin,   INPUT);
  pinMode(estopPin, INPUT_PULLUP);

  stopMotor();

  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected: " + WiFi.localIP().toString());
}

void loop() {

  // Emergency stop toggle
  bool button = digitalRead(estopPin);
  if (button == LOW && lastButton == HIGH) {
    estopState = !estopState;
    delay(150);
  }
  lastButton = button;

  if (estopState) {
    stopMotor();
    movement_status = "ESTOP";
    health_status   = "FAULT";
    Serial.println("ESTOP");
    delay(100);
    return;
  }

  health_status = "OK";

  // Read encoder, pwm absolute so no rollover needed
  az = readEncoderAngle();
  az_angle = az;

  // Read pot
  float rawPot = map(analogRead(potPin), 0, 1023, 0, 360);

  // Detect pot movement (>2 deg change = user is turning it)
  if (abs(rawPot - potTarget) > 2.0) {
    potTarget = rawPot;
    lastPotChangeTime = millis();
  }

  // Decide target: whichever source was updated most recently wins
  unsigned long now = millis();
  bool apiRecent = (apiTarget >= 0) && (now - lastApiCommandTime < SOURCE_TIMEOUT);
  bool potRecent = (now - lastPotChangeTime < SOURCE_TIMEOUT);

  if (apiRecent && (!potRecent || lastApiCommandTime > lastPotChangeTime)) {
    target = apiTarget;
  } else {
    target = potTarget;
  }

  // Hard limits
  if (az <= 0 && target < az) {
    stopMotor();
    movement_status = "LIMIT";
    Serial.println("Hard limit min");
    return;
  }
  if (az >= 360 && target > az) {
    stopMotor();
    movement_status = "LIMIT";
    Serial.println("Hard limit max");
    return;
  }

  // P controller
  float error = target - az;
  float power = kP * (error / 180.0);
  power = constrain(power, -1.0, 1.0);

  if (abs(error) < deadband) {
    stopMotor();
    movement_status = "IDLE";
  } else {
    applyMotor(power);
    movement_status = "MOVING";
  }

  // Debug
  Serial.print("AZ=");     Serial.print(az);
  Serial.print(" TGT=");   Serial.print(target);
  Serial.print(" ERR=");   Serial.print(error);
  Serial.print(" SRC=");   Serial.println(apiRecent && lastApiCommandTime > lastPotChangeTime ? "API" : "POT");

  // Network every 5s
  if (now - lastNetworkTime >= networkInterval) {
    lastNetworkTime = now;
    postReading();
    getLatestCommand();
  }

  delay(20);  // motor loop at 50hz
}

float readEncoderAngle() {
  unsigned long hi = pulseIn(encPin, HIGH, 25000);
  unsigned long lo = pulseIn(encPin, LOW,  25000);
  if (hi + lo == 0) return az;  // return last known if no pulse
  return (float(hi) / float(hi + lo)) * 360.0;
}

void applyMotor(float power) {
  int pwm = abs(power) * 255;
  if (pwm < minPWM) pwm = minPWM;
  if (power > 0) rotateCW(pwm);
  else           rotateCCW(pwm);
}

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

void postReading() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  WiFiClient client;
  http.begin(client, readingsUrl);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<300> doc;
  doc["telescope_id"]     = telescope_id;
  doc["azimuth_angle"]    = az_angle;
  doc["elevation_angle"]  = el_angle;
  doc["latitude"]         = 0;
  doc["longitude"]        = 0;
  doc["altitude"]         = 0;
  doc["gyroscope_x"]      = 0;
  doc["gyroscope_y"]      = 0;
  doc["gyroscope_z"]      = 0;
  doc["acceleration_x"]   = 0;
  doc["acceleration_y"]   = 0;
  doc["acceleration_z"]   = 0;
  doc["magnetic_field_x"] = 0;
  doc["magnetic_field_y"] = 0;
  doc["magnetic_field_z"] = 0;
  doc["health_status"]    = health_status;
  doc["movement_status"]  = movement_status;

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  Serial.print("POST: "); Serial.println(code);
  http.end();
}

void getLatestCommand() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  WiFiClient client;
  http.begin(client, commandsUrl);

  int code = http.GET();
  Serial.print("GET: "); Serial.println(code);

  if (code == 200) {
    String response = http.getString();
    StaticJsonDocument<300> doc;
    if (!deserializeJson(doc, response)) {
      float newAz = doc["target_az_angle"];
      // Only treat as new command if value actually changed
      if (abs(newAz - apiTarget) > 0.5) {
        apiTarget = newAz;
        lastApiCommandTime = millis();
        Serial.print("New API target: "); Serial.println(apiTarget);
      }
    }
  }

  http.end();
}
