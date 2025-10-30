#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
//#include <ArduinoJson.h>


#define WIFI_SSID "SARAO_GUEST"
#define WIFI_PASSWORD "ska.2009"
#define WIFI_CHANNEL 6


String makeGetRequest(String url) {
 if (WiFi.status() == WL_CONNECTED) {
   HTTPClient http;
   http.begin(url);
   int httpResponseCode = http.GET();


   if (httpResponseCode > 0) {
     String payload = http.getString();
     http.end();
     return payload;
   } else {
     Serial.print("HTTP GET request failed with code: ");
     Serial.println(httpResponseCode);
     http.end();
     return "";
   }
 } else {
   Serial.println("WiFi not connected");
   return "";
 }
}


String makePostRequest(String url, String payload) {
 if (WiFi.status() == WL_CONNECTED) {
   HTTPClient http;
   http.begin(url);
   http.addHeader("Content-Type", "application/json");
   int httpResponseCode = http.POST(payload);


   if (httpResponseCode > 0) {
     String response = http.getString();
     http.end();
     return response;
   } else {
     Serial.print("HTTP POST request failed with code: ");
     Serial.println(httpResponseCode);
     http.end();
     return "";
   }
 } else {
   Serial.println("WiFi not connected");
   return "";
 }
}


String makePutRequest(String url, String payload) {
 if (WiFi.status() == WL_CONNECTED) {
   HTTPClient http;
   http.begin(url);
   http.addHeader("Content-Type", "application/json");
   int httpResponseCode = http.PUT(payload);


   if (httpResponseCode > 0) {
     String response = http.getString();
     http.end();
     return response;
   } else {
     Serial.print("HTTP PUT request failed with code: ");
     Serial.println(httpResponseCode);
     http.end();
     return "";
   }
 } else {
   Serial.println("WiFi not connected");
   return "";
 }
}


/*
String makeDeleteRequest(String url) {
 if (WiFi.status() == WL_CONNECTED) {
   HTTPClient http;
   http.begin(url);
   int httpResponseCode = http.DELETE();


   if (httpResponseCode > 0) {
     String response = http.getString();
     http.end();
     return response;
   } else {
     Serial.print("HTTP DELETE request failed with code: ");
     Serial.println(httpResponseCode);
     http.end();
     return "";
   }
 } else {
   Serial.println("WiFi not connected");
   return "";
 }
}
*/


void fetchTodos() {
 String response = makeGetRequest("https://dummyjson.com/todos");
 if (!response.isEmpty()) {
   Serial.println("Response from dummyjson.com (GET /todos):");
   Serial.println(response);


   // Parse the JSON response
   parseTodosResponse(response);
 }
}


void parseTodosResponse(String payload) {/*
 DynamicJsonDocument doc(2048);
 DeserializationError error = deserializeJson(doc, payload);
 if (error) {
   Serial.print("JSON parsing error: ");
   Serial.println(error.c_str());
 } else {
   JsonObject todos = doc.as<JsonObject>();
   JsonArray todosArray = todos["todos"];
   for (JsonVariant todo : todosArray) {
     Serial.println("Todo:");
     Serial.print("  ID: ");
     Serial.println(todo["id"].as<int>());
     Serial.print("  Todo: ");
     Serial.println(todo["todo"].as<String>());
     Serial.print("  Completed: ");
     Serial.println(todo["completed"].as<bool>());
     Serial.print("  User ID: ");
     Serial.println(todo["userId"].as<int>());
     Serial.println();
   }
 }*/
 Serial.println(payload);
}


void setup() {
 Serial.begin(115200);


 WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
 Serial.print("Connecting to WiFi ");
 Serial.print(WIFI_SSID);
 while (WiFi.status() != WL_CONNECTED) {
   delay(100);
   Serial.print(".");
 }
 Serial.println(" Connected!");


 Serial.print("IP address: ");
 Serial.println(WiFi.localIP());


 // Fetch todos from dummyjson.com
 fetchTodos();
}


void loop() {
 // No need to do anything in the loop
}





