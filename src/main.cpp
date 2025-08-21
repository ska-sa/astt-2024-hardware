#include <Arduino.h>

// Define the Potentiometer pin
const int POT_PIN = 36;

// Define the Button pin
const int BTN_PIN = 23;

// Define the Encoder pin
const int ENC_PIN = 39;

// Define the Driver Direction pins
#define IN1 32
#define IN2 33

// Define the Driver Enable pin
#define ENA 25

void setup() {
  // Initialize the serial communication
  Serial.begin(115200);
  
  // Initialize the alanolgue potentiometer pin
  setupPotentiometer(POT_PIN); 

  // Initialize the digital input pin
  setupButton(BTN_PIN);

  // Initialize the analogue encoder pin
  setupEncoder(ENC_PIN); 

  // Initialize the driver pin
  setupDriver(IN1, IN2, ENA);

  return;
}

void loop() {
  // Reading potentiometer value
  int potValue = readPotentiometer(POT_PIN); // Get potentiomenter value
  Serial.print("Potentiometer Reading: ");
  Serial.println(360.0 * potValue / 4095.00); 

  // Reading encoder value
  int encValue = readEncoder(ENC_PIN); // Get encoder value
  float position = (360.0 / 5.0) * 5.0 * (encValue / 4095.00);
  Serial.print("Encoder Reading: ");
  Serial.println(position); 
  
  // Reading botton value
  int btnValue = readButton(BTN_PIN);
  Serial.print("Button Reading: ");
  Serial.println(btnValue);

  // Controlling the motor
  float deltaPower = position - potValue;
  controlDriver(deltaPower);
  
  // Print line end
  Serial.println("\n\n\n");

  // Delay before the next reading
  delay(500);
  return;
}

void setupPotentiometer(int pinNumber) {
  pinMode(pinNumber, INPUT);
  return;
}

int readPotentiometer(int pinNumber) {
  return analogRead(pinNumber);  // Read from predefined pin
}

void setupEncoder(int pinNumber) {
  pinMode(pinNumber, INPUT);
  return;
}

int readEncoder(int pinNumber) {
  return analogRead(pinNumber);
}

void setupButton(int pinNumber) {
  pinMode(pinNumber, INPUT_PULLUP);
  return;
}

int readButton(int pinNumber) {
  return digitalRead(pinNumber) == LOW;
}

void setupDriver(int in1, int in2, int ena) {
  pinMode(in1, OUTPUT);
  pinMode(in2, OUTPUT);
  pinMode(ena, OUTPUT);
  return;
}

void controlDriver(float deltaPower) {
  analogWrite(ENA, int(abs(deltaPower * 255.0) * 2.0));
  if (deltaPower == 0.0) {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
  } else if (deltaPower > 0) {
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
  } else {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
  }
  return;
}