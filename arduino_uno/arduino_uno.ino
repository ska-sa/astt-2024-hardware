#include <Arduino.h>
#include "find_astt.h"
#include "rotate_in_azimuth.h"

void setup() {
    Serial.begin(115200);
    Serial.println("Initializing...");
    
    // Find the ASTT Setup
    findASTTSetup();

    // Rotate in Azimuth Setup
    rotateInAzimuthSetup();

    Serial.println("Initialization complete.");
}

void loop() {
    
    // Find the ASTT Loop
    findASTTLoop();

    // Rotate in Azimuth Loop
    rotateInAzimuthLoop();

    delay(10);
}
