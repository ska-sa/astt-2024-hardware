#include <Arduino.h>
const int IN1 = 7;
const int IN2 = 6;

const int ENA = 10;

const float refValue = 0.5;

void setup() {
  Serial.begin(115200);
  
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  
  pinMode(ENA, OUTPUT);
}

void loop() {
   float potValue = analogRead(A0) / 1024.0;
   
   float power = potValue - refValue;
   Serial.print("Power: ");
   Serial.print(abs(power) * 200.0);
   Serial.println("%");
   analogWrite(ENA, int(abs(power * 255.0) * 2.0));
   if(power == 0.0) {
    digitalWrite(IN1,LOW);
    digitalWrite(IN2,LOW);
   }else if(power > 0) {
    digitalWrite(IN1,HIGH);
    digitalWrite(IN2,LOW);
   }else {
    digitalWrite(IN1,LOW);
    digitalWrite(IN2,HIGH);
   }
   
}
