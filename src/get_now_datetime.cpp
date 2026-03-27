#include <ESP8266WiFi.h>
#include <time.h>         // built-in time functions

const char* ssid     = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0; // UTC offset is 0
const int   daylightOffset_sec = 0; // No daylight saving for UTC

void setup(){
    Serial.begin(115200);
    Serial.println();

    // Connect to WiFi
    Serial.printf("Connecting to %s ", ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println(" CONNECTED");

    // Configure NTP client for UTC time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    // Wait for time to be set
    while(!time(nullptr)){
        Serial.print(".");
        delay(100);
    }
    Serial.println("\nTime synchronized.");
}

void loop(){
  time_t now = time(nullptr); // Get current time (Unix epoch)

  // Use gmtime for UTC (Greenwich Mean Time)
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  // Format and print the time
  char timeString[50];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S UTC", &timeinfo);
  Serial.println(timeString);

  delay(5000); // Update every 5 seconds
}


