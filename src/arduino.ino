// ---------------- Motor Pins ----------------
const int IN1 = 7;
const int IN2 = 6;
const int ENA = 10;

// ---------------- Inputs ----------------
const int potPin = A0;
const int encPin = 8;
const int estopPin = 11;   // active LOW toggle switch

// ---------------- Control ----------------
float kP = 20.0;
float deadband = 3;         // degrees
int minPWM = 150;              // to avoid "humming"

// ---------------- Encoder ----------------
const int ENCODER_RES = 4096;
const float FULL_ROT = 360.0;
const int MAX_TURNS = 3;

// ---------------- State ----------------
float prevRaw = 0;
long turns = 0;
float az = 0;
bool estopState = false;   // false = running, true = stopped

void setup() {
  Serial.begin(115200);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);

  pinMode(potPin, INPUT);
  pinMode(encPin, INPUT);
  pinMode(estopPin, INPUT_PULLUP); // GND when pressed

  stopMotor();
  Serial.println("AZ Motor Controller Ready");
}

void loop() {

  // --------- Emergency Stop Toggle ---------
  static bool lastButton = HIGH;
  bool button = digitalRead(estopPin);

  if (button == LOW && lastButton == HIGH) {  
    estopState = !estopState; // toggle
    delay(200); // debounce
  }
  lastButton = button;

  if (estopState) {
    stopMotor();
    Serial.println("EMERGENCY STOP ACTIVE");
    delay(100);
    return;
  }

  // --------- Potentiometer Target ----------
  float target = map(analogRead(potPin), 0, 1023, 0, 360);

  // --------- Encoder Measure ----------
  float raw = readEncoderAngle();
  updateRollover(raw);
  az = turns * FULL_ROT + raw;

  // --------- Bounds ---------
  if ((az <= 0 && target < az) || (az >= 360 && target > az)) {
    stopMotor();
    Serial.println("HARD LIMIT REACHED");
    return;
  }

  // --------- Control ---------
  // float error = target - az;
  int error = (int)az - (int)target;
  
  float power = kP * (error / 180.0);
  power = constrain(power, -1.0, 1.0);
  if (abs(error) < deadband) {
    stopMotor();
    Serial.println(" ");
  } else {
 // --------- Motor Direction + Anti-Stall Logic ---------
  applyMotor(power);
  }


 

  // ------ Debug ------
  Serial.print("AZ="); Serial.print(az);
  Serial.print("  TGT="); Serial.print(target);
  Serial.print("  ERR="); Serial.print(error);
  Serial.print("  PWR="); Serial.println(power, 3);

  delay(20);
}

// ----------------------------------------------------
// ----------------- Helper Functions -----------------
// ----------------------------------------------------

float readEncoderAngle() {

  const int samples = 5;
  float sum = 0;
  int validSamples = 0;

  // ----- Average multiple readings -----
  for (int i = 0; i < samples; i++) {

    unsigned long hi = pulseIn(encPin, HIGH, 25000);
    unsigned long lo = pulseIn(encPin, LOW, 25000);

    if ((hi + lo) == 0) continue;

    float duty = (float)hi / (float)(hi + lo);
    float angle = duty * FULL_ROT;

    // Clamp
    if (angle < 0.0) angle = 0.0;
    if (angle > 360.0) angle = 360.0;

    sum += angle;
    validSamples++;
  }

  // If no valid readings, keep previous value
  if (validSamples == 0) {
    return prevRaw;
  }

  // ----- Average result -----
  float avgAngle = sum / validSamples;

  // ----- Reject sudden impossible jumps -----
  if (abs(avgAngle - prevRaw) > 20.0) {
    avgAngle = prevRaw;
  }

  // ----- Low-pass filter -----
  float filtered = (0.7 * prevRaw) + (0.3 * avgAngle);

  prevRaw = filtered;

  return filtered;
}

void updateRollover(float raw) {
  float d = raw - prevRaw;
  if (d > 180) turns--;
  if (d < -180) turns++;
  prevRaw = raw;

  if (abs(turns) > MAX_TURNS) {
    turns = 0;
    prevRaw = raw;
    az = raw;
  }
}

void applyMotor(float power) {
  if (power == 0) {
    stopMotor();
    return;
  }

  int pwm = abs(power) * 255;

  // Avoid humming: enforce minimum PWM
  if (pwm < minPWM) pwm = minPWM;

  if (power > 0) {
    rotateCW(pwm);
  } else {
    rotateCCW(pwm);
  }
}

void stopMotor() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  analogWrite(ENA, 0);
}

void rotateCW(int pwm) {  // forward
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  analogWrite(ENA, pwm);
}

void rotateCCW(int pwm) { // backward
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  analogWrite(ENA, pwm);
}
