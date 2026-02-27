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

void rotateInAzimuthSetup() {
  // Serial communication
  Serial.begin(115200);
  // Wi-Fi
  WiFi.begin(ssid, password);

  // Wi-Fi connection
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // Log Wi-Fi credentials
  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());
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

void rotateInAzimuthLoop() {

  // 1. Get latest sensor readings
  az_angle = 120.5;
  el_angle = 45.2;
  movement_status = "TRACKING";

  // 1️. Send current reading
  postReading();

  // 2️. Check for new commands (future integration)
  getLatestCommand();

  // 3. Log values
  Serial.print("POST Response: ");
  Serial.println(responseCode);


  // 4. Later:
  // if (newCommandAvailable) {
  //     applyCommand();
  // }

  delay(2000);  // control how often you hit backend
}

void setup() {
  rotateInAzimuthSetup();
}

void loop() {
  rotateInAzimuthLoop();
  delay(5000);  // send every 5 seconds
}