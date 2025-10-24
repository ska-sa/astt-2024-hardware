// --- Motor control pins ---
const int IN1 = 7;
const int IN2 = 6;
const int ENA = 10;

// --- Inputs ---
const int potPin = A0;  // Potentiometer for target azimuth
const int encPin = 8;   // PWM encoder input (MAE3-P12 output)

// --- Control parameters ---
const float kP = 1.0;        // proportional gain (tune this)
const float deadband = 0.05;  // motor stop threshold

// --- Encoder constants ---
const int ENCODER_RES = 4096;      // 12-bit encoder
const float FULL_ROTATION = 360.0; // degrees per revolution
const int MAX_TURNS = 3;           // allowed ±3 turns

// --- Limits (for local movement) ---
const float MIN_AZIMUTH = -130.0;  // degrees
const float MAX_AZIMUTH = 130.0;   // degrees

// --- State variables ---
float currentAzAngle = 0.0;  // absolute azimuth (degrees)
float prevRawAngle = 0.0;    // previous encoder angle for rollover detection
long turnCount = 0;          // number of full rotations (positive/negative)

void setup() {
  Serial.begin(115200);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(potPin, INPUT);
  pinMode(encPin, INPUT);

  Serial.println("MAE3-P12 Encoder Control Initialized");
}

void loop() {
  // --- Read target azimuth from potentiometer ---
  float targetAzAngle = map(analogRead(potPin), 0, 1023, MIN_AZIMUTH, MAX_AZIMUTH);
  Serial.print("Target Azimuth: ");
  Serial.print(targetAzAngle, 2);
  Serial.println("°");

  // --- Measure PWM duty cycle from encoder ---
  unsigned long highTime = pulseIn(encPin, HIGH, 25000); // µs
  unsigned long lowTime  = pulseIn(encPin, LOW, 25000);  // µs
  float dutyCycle = 0.0;

  if (highTime + lowTime > 0) {
    dutyCycle = (float)highTime / (float)(highTime + lowTime);
  }

  // --- Convert duty cycle (0–4095 counts -> 0–360°) ---
  float rawAngle = dutyCycle * FULL_ROTATION; // 0–360°

  // --- Detect rollover for continuous tracking ---
  float delta = rawAngle - prevRawAngle;
  if (delta > 180.0) {
    turnCount--;
  } else if (delta < -180.0) {
    turnCount++;
  }
  prevRawAngle = rawAngle;

  // --- Compute continuous angle ---
  currentAzAngle = turnCount * FULL_ROTATION + rawAngle;

  // --- Auto-reset if more than ±3 rotations ---
  if (abs(turnCount) > MAX_TURNS) {
    Serial.println("⚠️ Exceeded 3-turn limit! Resetting encoder tracking to zero...");
    stopMotor();
    delay(300);
    turnCount = 0;
    prevRawAngle = rawAngle;
    currentAzAngle = rawAngle;
  }

  Serial.print("Turn Count: ");
  Serial.println(turnCount);
  Serial.print("Current Azimuth: ");
  Serial.print(currentAzAngle, 2);
  Serial.println("°");

  // --- Safety: restrict movement within local bounds ---
  if (currentAzAngle <= MIN_AZIMUTH && targetAzAngle < currentAzAngle) {
    Serial.println("⚠️ At minimum limit — cannot move further negative.");
    stopMotor();
    delay(200);
    return;
  }

  if (currentAzAngle >= MAX_AZIMUTH && targetAzAngle > currentAzAngle) {
    Serial.println("⚠️ At maximum limit — cannot move further positive.");
    stopMotor();
    delay(200);
    return;
  }

  // --- Compute error and proportional power ---
  float error = targetAzAngle - currentAzAngle;
  float power = kP * (error / 180.0);  // normalize error roughly to -1 to 1
  power = constrain(power, -1.0, 1.0) * ;

  Serial.print("Error: ");
  Serial.print(error, 2);
  Serial.println("°");

  Serial.print("Power Output: ");
  Serial.print(power * 100.0, 1);
  Serial.println(" %");

  // --- Motor control ---
  if (abs(power) < deadband) {
    stopMotor();
  } else if (power > 0) {
    rotateForward(abs(power));
  } else {
    rotateBackward(abs(power));
  }

  Serial.println("------------------------");
  delay(200);
}

// --- Helper Functions ---
void stopMotor() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  analogWrite(ENA, 0);
}

void rotateForward(float pwmVal) {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  analogWrite(ENA, int(pwmVal * 255.0));
}

void rotateBackward(float pwmVal) {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  analogWrite(ENA, int(pwmVal * 255.0));
}
