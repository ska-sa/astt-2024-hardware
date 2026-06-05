#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    Serial.println("Initializing...");
    // ...
    Serial.println("Initialization complete.");
}

void loop() {
    delay(10);
}
