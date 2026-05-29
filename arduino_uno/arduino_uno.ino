#include <Arduino.h>
#include <SoftwareSerial.h>

SoftwareSerial esp(2, 3); // RX, TX

const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

void sendCommand(String cmd, int waitMs = 2000) {
    esp.println(cmd);

    long start = millis();

    while (millis() - start < waitMs) {
        while (esp.available()) {
            Serial.write(esp.read());
        }
    }
}

void setup() {
    Serial.begin(115200);
    esp.begin(115200);

    delay(2000);

    Serial.println("ESP-01 WiFi Test");

    // Test AT
    sendCommand("AT");

    // Set WiFi mode
    sendCommand("AT+CWMODE=1");

    // Connect to WiFi
    String joinCmd =
        "AT+CWJAP=\"" +
        String(WIFI_SSID) +
        "\",\"" +
        String(WIFI_PASS) +
        "\"";

    sendCommand(joinCmd, 10000);

    // Open TCP connection
    sendCommand("AT+CIPSTART=\"TCP\",\"google.com\",80", 5000);

    // HTTP GET request
    String httpRequest =
        "GET / HTTP/1.1\r\n"
        "Host: google.com\r\n"
        "Connection: close\r\n\r\n";

    // Tell ESP request length
    sendCommand("AT+CIPSEND=" + String(httpRequest.length()));

    delay(1000);

    // Send request
    esp.print(httpRequest);

    Serial.println("HTTP request sent");
}

void loop() {
    while (esp.available()) {
        Serial.write(esp.read());
    }
}
