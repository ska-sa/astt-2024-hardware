#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GPS.h>

// Create GPS object using I2C
Adafruit_GPS GPS(&Wire);

void GPSSetup() {
  GPS.begin(0x10); // Default I2C address for PA1010D

  // Optional: configure GPS output
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA); // GPS Recommended minimum + fix data
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ); // 1 Hz update rate
  GPS.sendCommand(PMTK_API_SET_FIX_CTL_1HZ); // Requests and checks the fix status (whether the GPS is locked)
  GPS.sendCommand(PGCMD_ANTENNA); // Request antenna status
  delay(1000);
  GPS.println(PMTK_Q_RELEASE); // Ask for firmware version
  return;
}

void GPSLoop() {
  char c = GPS.read();
  if (c) Serial.print(c);

  if (GPS.newNMEAreceived()) {
    if (!GPS.parse(GPS.lastNMEA())) return;
    // Print parsed GPS data
    Serial.print("Time: "); Serial.print(GPS.hour); Serial.print(":");
    Serial.print(GPS.minute); Serial.print(":"); Serial.println(GPS.seconds);
    Serial.print("Date: "); Serial.print(GPS.day); Serial.print("/");
    Serial.print(GPS.month); Serial.print("/"); Serial.println(GPS.year);
    Serial.print("Lat: "); Serial.println(GPS.latitude, 4);
    Serial.print("Long: "); Serial.println(GPS.longitude, 4);
    Serial.print("Speed (knots): "); Serial.println(GPS.speed);
    Serial.print("Altitude: "); Serial.println(GPS.altitude);
    if (GPS.fix) {
      Serial.println("GPS has a fix!");
    } else {
      Serial.println("No GPS fix yet.");
    }
  }
}

void setup() {
    Serial.begin(115200);
    Serial.println("Initializing...");
    
    // Find the ASTT Setup
    GPSSetup();

    Serial.println("Initialization complete.");
}

void loop() {
    
    // Find the ASTT Loop
    GPSLoop();

    delay(10);
}
