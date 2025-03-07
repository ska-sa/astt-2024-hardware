#include <Arduino.h>

// Define the DIP switch pins
const int DIP_SWITCH_PINS[] = {35, 32, 33, 25, 26, 27, 14, 12, 17, 5, 18, 19};
const int NUM_DIP_SWITCHES = sizeof(DIP_SWITCH_PINS) / sizeof(DIP_SWITCH_PINS[0]);

// Define the Potentiometer pin
const int POT_PIN = 36;

// Define the Button pin
const int BTN_PIN = 23;

// Functions
// ...
void setupDipSwitch(const int* dipSwitchPins) {
  // Initialize the DIP switch pins as inputs
  for (int i = 0; i < NUM_DIP_SWITCHES; i++) {
    pinMode(dipSwitchPins[i], INPUT_PULLUP);
  }
}

int readBinaryValue(const int* dipSwitchPins) {
  int dip_switch_value = 0;
  for (int i = 0; i < NUM_DIP_SWITCHES; i++) {
    dip_switch_value |= (digitalRead(dipSwitchPins[i]) == LOW) << i;
  }
  return dip_switch_value;
}

int convertBinaryToDecimal(int binary_value) {
  int decimal_value = 0;
  for (int i = 0; i < NUM_DIP_SWITCHES; i++) {
    decimal_value += (binary_value & (1 << i)) ? (1 << i) : 0;
  }
  return decimal_value;
}

void printBinaryValue(int binary_value) {
  for (int i = NUM_DIP_SWITCHES - 1; i >= 0; i--) {
    Serial.print((binary_value >> i) & 1);
  }
}

void setupPotentiometer(int pinNumber) {
  pinMode(pinNumber, INPUT);
  return;
}

int readPotentiometer(int pinNumber) {
  return analogRead(pinNumber);  // Read from predefined pin
}

void setupButton(int pinNumber) {
  pinMode(pinNumber, INPUT_PULLUP);
  return;
}

int readButton(int pinNumber) {
  return digitalRead(pinNumber) == LOW;
}

void setup() {
  // Initialize the serial communication
  Serial.begin(115200);
  
  // Initialize the alanolgue input pin
  setupPotentiometer(POT_PIN); 

  // Initialize the digital input pins
  setupDipSwitch(DIP_SWITCH_PINS);

  // Initialize the digital input pin
  setupButton(BTN_PIN);

  return;
}

void loop() {
  // Read the DIP switch values
  int dip_switch_value = readBinaryValue(DIP_SWITCH_PINS);
  int decimal_value = convertBinaryToDecimal(dip_switch_value);
  //Serial.print("DIP Switch Binary Value: ");
  //printBinaryValue(dip_switch_value);
  //Serial.println();
  Serial.print("DIP Switch Decimal Value: ");
  Serial.println(decimal_value);

  // Reading potentiometer value
  int potValue = readPotentiometer(POT_PIN); // Get potentiomenter value
  Serial.print("Potentiometer Reading: ");
  Serial.println(360.0 * potValue / 4095.00); 

  // Reading botton value
  int btnValue = readButton(BTN_PIN);
  Serial.print("Button Reading: ");
  Serial.println(btnValue);
  
  // Print line end
  Serial.println("=============================================");

  // Delay before the next reading
  delay(100);
  return;
}