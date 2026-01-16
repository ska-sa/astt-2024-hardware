#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

// ===== WiFi credentials =====
const char* ssid = "SARAO_Guest";
const char* password = "ska.2009";

// ===== Server info =====
const char* serverUrl = "http://196.24.39.242/api/v1/readings";

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {

    HTTPClient http;
    WiFiClient client;

    http.begin(client, serverUrl);
    http.addHeader("Content-Type", "application/json");

    // ===== Create JSON payload =====
    StaticJsonDocument<200> doc;
    doc["telescope_id"] = 1;
    doc["az_angle"] = 120.5;
    doc["el_angle"] = 45.2;
    doc["health_status"] = "OK";
    doc["movement_status"] = "TRACKING";

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // ===== POST request =====
    int httpResponseCode = http.POST(jsonPayload);

    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("Response:");
      Serial.println(response);
    }

    http.end();
  } else {
    Serial.println("WiFi not connected");
  }

  delay(5000); // send every 5 seconds
}
