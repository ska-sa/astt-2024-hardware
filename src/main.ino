#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "SARAO_Guest";
const char* password = "ska.2009";

// Server info
const char* ip = "172.22.9.145";
const char* port = "8000";
const char* readingsUrl = "http://172.22.9.145:8000/api/v1/readings";
const char* commandsUrl = "http://172.22.9.145:8000/api/v1/commands/2/latest";

// User varibles
int cmd_user_id = 0;

// Telescope variable
int telescope_id = 2;

// Readings variables
float az_angle = 0.0;
float el_angle = 0.0;
String health_status = "OK";
String movement_status = "IDLE";

// Command variables
float cmd_target_az = 0.0;
float cmd_target_el = 0.0;
String cmd_created_at = "";
bool newCommandAvailable = false;

// Motor Pins
const int IN1 = 12; // 7;
const int IN2 = 14; // 6;
const int ENA = 2; // 10;

// Input Pins
const int potPin = A0;
const int encPin = 15; // 8;
const int estopPin = 13; // 11;   // active LOW toggle switch

// Control
float kP = 20.0;
float deadband = 3;         // degrees
int minPWM = 150;              // to avoid "humming"

// Encoder
const int ENCODER_RES = 4096;
const float FULL_ROT = 360.0;
const int MAX_TURNS = 3;

// State
float prevRaw = 0;
long turns = 0;
float az = 0;
float prev_az = 0;
bool estopState = false;   // false = running, true = stopped

void rotateInAzimuthSetup() {
  // Serial communication
  Serial.begin(115200);

  // Motor Pins
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);

  // Potentiometer
  pinMode(potPin, INPUT);

  // Encoder
  pinMode(encPin, INPUT);

  // Emergency stop button
  pinMode(estopPin, INPUT_PULLUP); // GND when pressed

  // Wi-Fi connection
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // Wi-Fi credentials
  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());

  // Esnure more is stoped
  stopMotor();
  Serial.println("AZ Motor Controller Ready");
  return;
}

void postReading() {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return;
  }

  HTTPClient http;
  WiFiClient client;

  http.begin(client, readingsUrl);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<200> doc;
  doc["telescope_id"] = telescope_id;
  doc["az_angle"] = az_angle;
  doc["el_angle"] = el_angle;
  doc["health_status"] = health_status;
  doc["movement_status"] = movement_status;

  String payload;
  serializeJson(doc, payload);

  int responseCode = http.POST(payload);

  if (responseCode > 0) {
    Serial.println(http.getString());
  }

  http.end();
}

void getLatestCommand() {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return;
  }

  HTTPClient http;
  WiFiClient client;

  http.begin(client, commandsUrl);

  int responseCode = http.GET();

  Serial.print("GET Response: ");
  Serial.println(responseCode);

  if (responseCode == 200) {

    String response = http.getString();

    StaticJsonDocument<300> doc;
    DeserializationError error = deserializeJson(doc, response);

    if (!error) {
      cmd_user_id = doc["user_id"];
      cmd_target_az = doc["target_az_angle"];
      cmd_target_el = doc["target_el_angle"];
      newCommandAvailable = true;

      Serial.println("New command parsed successfully");
    }
  }

  http.end();
}

// ----------------------------------------------------
// ----------------- Helper Functions -----------------
// ----------------------------------------------------

float readEncoderAngle() {
  unsigned long hi = pulseIn(encPin, HIGH, 25000);
  unsigned long lo = pulseIn(encPin, LOW, 25000);
  if (hi + lo == 0) return prevRaw;

  float duty = float(hi) / float(hi + lo);
  return duty * FULL_ROT;
}

void updateRollover(float raw) {
  float d = raw - prevRaw;
  if (d > 180) turns--;
  if (d < -180) turns++;
  prevRaw = raw;

  if (abs(turns) > MAX_TURNS) {
    turns = 0;
    prevRaw = raw;
    az = raw;
  }
}

void applyMotor(float power) {
  if (power == 0) {
    stopMotor();
    return;
  }

  int pwm = abs(power) * 255;

  // Avoid humming: enforce minimum PWM
  if (pwm < minPWM) pwm = minPWM;

  if (power > 0) {
    rotateCW(pwm);
  } else {
    rotateCCW(pwm);
  }
}

void stopMotor() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  analogWrite(ENA, 0);
}

void rotateCW(int pwm) {  // forward
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  analogWrite(ENA, pwm);
}

void rotateCCW(int pwm) { // backward
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  analogWrite(ENA, pwm);
}


void rotateInAzimuthLoop() {

  // 1. Get latest sensor readings

  // 1.1 Read emergency stop
  static bool lastButton = HIGH;
  bool button = digitalRead(estopPin);

  if (button == LOW && lastButton == HIGH) {  
    estopState = !estopState; // toggle
    delay(200); // debounce
  }
  lastButton = button;

  if (estopState) {
    stopMotor();
    Serial.println("EMERGENCY STOP ACTIVE");
    delay(100);
    return;
  }

  // 1.2 Potentiometer
  float target = map(analogRead(potPin), 0, 1023, 0, 360);

  // 1.3 Encoder
  float raw = readEncoderAngle();
  updateRollover(raw);
  az = turns * FULL_ROT + raw;

  if ((az <= 0 && target < az) || (az >= 360 && target > az)) { // Bound
    stopMotor();
    Serial.println("HARD LIMIT REACHED");
    return;
  }

  // 1.4 Control
    float error = target - az;

  float power = kP * (error / 180.0);
  power = constrain(power, -1.0, 1.0);
  if (abs(error) < deadband) {
    stopMotor();
    Serial.println(" ");
  } else { // Motor Direction + Anti-Stall Logic
  applyMotor(power);
  }


  az_angle = az;
  el_angle = 0;
  movement_status = (az == prev_az) ? "TRACKING" : "STOPPED";
  prev_az = az;

  // 2. Send current reading
  postReading();

  // 3. Check for new commands (future integration)
  getLatestCommand();

  // 4. Log values
  Serial.print("POST Response: ");
  Serial.println(responseCode);


  // 5. Later:
  // if (newCommandAvailable) {
  //     applyCommand();
  // }

  delay(200);  // control how often you hit backend
}

void setup() {
  rotateInAzimuthSetup();
}

void loop() {
  rotateInAzimuthLoop();
  delay(5000);  // send every 5 seconds
}