#include <Wire.h>
#include <math.h>
#include <WiFi.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GPS.h>
#include <time.h>
#include <SparkFun_MMC5983MA_Arduino_Library.h>


// ---------- wifi and api ----------
const char* ssid = "SARAO_Guest";
const char* password = "ska.2009";
const char* commandsUrl = "http://172.22.9.54:8000/api/v1/commands/7/latest";
const char* readingsUrl = "http://172.22.9.54:8000/api/v1/readings";
const char* ntpServer = "pool.ntp.org";
int telescopeId = 7;




// ---------- pins ----------
const int IN1 = 25;
const int IN2 = 26;
const int ENA = 27;
const int potPin = 34;  // adc1, input only, safe with wifi
const int encPin = 19;
const int estopPin = 23;


// ---------- azimuth range ----------
// full circle is allowed except the blocked zone, which is where cables
// or hardstops live. the blocked zone may wrap through 0.
const float AZ_MIN = 0.0;
const float AZ_MAX = 360.0;
const float BLOCK_START = 360.0;  // start of blocked zone, going cw
const float BLOCK_END = 139.00;   // end of blocked zone, going cw


// ---------- motor control ----------
const float TOLERANCE = 1.0;    // deg, closer than this counts as arrived
const float RAMP_RANGE = 60.0;  // deg of error that gives full power
const int MIN_PWM = 140;        // below this the motor just hums
const int MAX_PWM = 255;


// ---------- encoder ----------
const int ENC_SAMPLES = 7;        // odd number, we take the median
const float ENC_SMOOTHING = 0.5;  // 0 is no smoothing, 1 never updates
float encAngle = 0.0;


// ---------- potentiometer ----------
const float POT_THRESHOLD = 4.0;  // deg of movement before we call it a change
float potTarget = 0.0;


// ---------- command arbitration ----------
const unsigned long SOURCE_TIMEOUT = 10000;
float apiTarget = -1.0;
unsigned long lastPotTime = 0;
unsigned long lastApiTime = 0;


// ---------- tracking ----------
bool isTracking = false;
float track_m1 = 0, track_m2 = 0;
float track_c1 = 0, track_c2 = 0;
float track_T_ra = 0, track_T_dec = 0;
float track_A = 0, track_phi = 0, track_D = 0;


// ---------- estop ----------
bool estopState = false;
bool lastButton = HIGH;


// ---------- network timing ----------
const unsigned long POST_INTERVAL = 5000;
const unsigned long POLL_INTERVAL = 2000;
unsigned long lastPostTime = 0;
unsigned long lastPollTime = 0;


// ---------- readings sent to backend ----------
float azimuthAngle = 0.0;
float elevationAngle = 0.0;  // no elevation motor yet
// site defaults, readGPS overwrites these only when it gets a fix
float latitude = -33.944481;
float longitude = 18.478685;
float altitude = 50.0;
String healthStatus = "OK";
String movementStatus = "IDLE";


// ---------- sensors ----------
Adafruit_GPS GPS(&Wire);
SFE_MMC5983MA myMag;
bool magOk = false;

float magneticDeclination = -25.6;
float headingOffset = 90.0;
float DEFAULT_HEADING = 0.0;   // used when magnetometer is absent or not yet calibrated
float trueHeading = DEFAULT_HEADING;

bool calibrated = false;
unsigned long calibStartTime = 0;
uint32_t minX = 4294967295, minY = 4294967295, minZ = 4294967295;
uint32_t maxX = 0, maxY = 0, maxZ = 0;
float offX = 0, offY = 0, offZ = 0;
float scaleX = 1, scaleY = 1, scaleZ = 1;



// ================= tracking math =================


// the model gives ra in hours and dec in degrees for a day of the year.
// we then convert that to an az and el for our site at the current utc time.
float trackEl = 0.0;  // elevation of the source, degrees




// ================= angle helpers =================


// always return 0 to 360
float norm360(float a) {
  a = fmod(a, 360.0);
  if (a < 0) a += 360.0;
  return a;
}


// distance going clockwise from a to b
float cwDistance(float from, float to) {
  return norm360(to - from);
}


bool inBlockedZone(float angle) {
  float a = norm360(angle);
  if (BLOCK_START <= BLOCK_END) {
    return a >= BLOCK_START && a <= BLOCK_END;
  }
  // zone wraps through 0
  return a >= BLOCK_START || a <= BLOCK_END;
}


// walk the path one degree at a time and see if it enters the blocked zone
bool pathIsBlocked(float from, float to, int direction) {
  float dist = (direction > 0) ? cwDistance(from, to) : cwDistance(to, from);
  for (float step = 0; step <= dist; step += 1.0) {
    float here = (direction > 0) ? from + step : from - step;
    if (inBlockedZone(here)) return true;
  }
  return false;
}


// +1 is clockwise, -1 is counter clockwise, 0 means no safe path
int chooseDirection(float current, float target) {
  float cw = cwDistance(current, target);
  float ccw = cwDistance(target, current);


  bool cwOk = !pathIsBlocked(current, target, 1);
  bool ccwOk = !pathIsBlocked(current, target, -1);


  if (cwOk && ccwOk) return (cw <= ccw) ? 1 : -1;  // both clear, take shorter
  if (cwOk) return 1;
  if (ccwOk) return -1;
  return 0;
}




// ================= encoder =================


// read the pwm duty cycle and turn it into an angle.
// takes the median of several samples so one bad pulse cannot move the value.
float readEncoderAngle() {
  float samples[ENC_SAMPLES];
  int valid = 0;


  for (int i = 0; i < ENC_SAMPLES; i++) {
    unsigned long hi = pulseIn(encPin, HIGH, 25000);
    unsigned long lo = pulseIn(encPin, LOW, 25000);
    if (hi + lo == 0) continue;
    samples[valid] = ((float)hi / (float)(hi + lo)) * 360.0;
    valid++;
  }


  if (valid == 0) return encAngle;


  // sort so we can pick the middle value
  for (int i = 0; i < valid - 1; i++) {
    for (int j = i + 1; j < valid; j++) {
      if (samples[j] < samples[i]) {
        float tmp = samples[i];
        samples[i] = samples[j];
        samples[j] = tmp;
      }
    }
  }
  float median = samples[valid / 2];


  // light smoothing, keeps motion continuous without staircasing
  encAngle = (ENC_SMOOTHING * encAngle) + ((1.0 - ENC_SMOOTHING) * median);
  return norm360(encAngle);
}


// ================= magnetometer =================

// returns the current true heading in degrees.
// if the sensor is missing or still calibrating, returns the default
// instead of stalling the rest of the system.
float computeTrueHeading() {
  if (!magOk) return DEFAULT_HEADING;

  uint32_t rawX, rawY, rawZ;
  myMag.getMeasurementXYZ(&rawX, &rawY, &rawZ);

  if (!calibrated) {
    if (rawX < minX) minX = rawX;
    if (rawY < minY) minY = rawY;
    if (rawZ < minZ) minZ = rawZ;
    if (rawX > maxX) maxX = rawX;
    if (rawY > maxY) maxY = rawY;
    if (rawZ > maxZ) maxZ = rawZ;

    if (millis() - calibStartTime > 20000) {
      offX = (maxX + minX) / 2.0;
      offY = (maxY + minY) / 2.0;
      offZ = (maxZ + minZ) / 2.0;
      scaleX = (maxX - minX) / 2.0;
      scaleY = (maxY - minY) / 2.0;
      scaleZ = (maxZ - minZ) / 2.0;
      calibrated = true;
      Serial.println("mag calibrated");
    }
    return DEFAULT_HEADING;  // still calibrating, no reading yet
  }


  float cx = ((float)rawX - offX) / scaleX;
  float cy = ((float)rawY - offY) / scaleY;
  float heading = norm360(atan2(cx, -cy) * 180.0 / PI + 180 + headingOffset);
  return norm360(heading + magneticDeclination);
}


// ================= potentiometer =================


// esp32 adc is 12 bit, so the raw range is 0 to 4095
float readPotAngle() {
  long sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(potPin);
  float raw = sum / 8.0;
  return (raw / 4095.0) * 360.0;
}




// ================= motor =================


void stopMotor() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  analogWrite(ENA, 0);
}


void driveMotor(int pwm, int direction) {
  if (direction < 0) {
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
  } else {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
  }
  analogWrite(ENA, pwm);
}


// bigger error means more power, but never below MIN_PWM or we just hum
int computePwm(float errorSize) {
  if (errorSize < TOLERANCE) return 0;
  float scale = errorSize / RAMP_RANGE;
  if (scale > 1.0) scale = 1.0;
  return MIN_PWM + (int)((MAX_PWM - MIN_PWM) * scale);
}




// ================= gps =================


void readGPS() {
  GPS.read();
  if (!GPS.newNMEAreceived()) return;
  if (!GPS.parse(GPS.lastNMEA())) return;
  if (GPS.fix) {
    latitude = GPS.latitudeDegrees;
    longitude = GPS.longitudeDegrees;
    altitude = GPS.altitude;
  }
}




// ================= tracking math =================




float computeAzFromSource() {
  time_t nowUtc = time(nullptr);
  if (nowUtc < 1700000000) return -1.0;  // clock not synced yet
  if (track_T_ra <= 0 || track_T_dec == 0) return -1.0;


  struct tm utc;
  gmtime_r(&nowUtc, &utc);


  // day of year, with a fraction for the time of day
  float dayOfYear = utc.tm_yday + 1
                    + (utc.tm_hour * 3600.0 + utc.tm_min * 60.0 + utc.tm_sec) / 86400.0;


  // ra is piecewise linear in hours, one slope per half of the year
  float raHours;
  if (dayOfYear < track_T_ra) {
    raHours = track_m1 * dayOfYear + track_c1;
  } else {
    raHours = track_m2 * dayOfYear + track_c2;
  }
  raHours = fmod(raHours, 24.0);
  if (raHours < 0) raHours += 24.0;
  float raDeg = raHours * 15.0;


  // dec is a sinusoid over the year, T_dec is already 365.25 / 2pi
  float decDeg = track_A * sin(dayOfYear / track_T_dec + track_phi) + track_D;


  // sidereal time, needs double or we lose half a degree
  double d = ((double)nowUtc - 946728000.0) / 86400.0;
  double gmst = fmod(280.46061837 + 360.98564736629 * d, 360.0);
  if (gmst < 0) gmst += 360.0;


  float lst = fmod(gmst + longitude, 360.0);
  float ha = fmod(lst - raDeg, 360.0);


  float haRad = ha * PI / 180.0;
  float decRad = decDeg * PI / 180.0;
  float latRad = latitude * PI / 180.0;


  float sinAlt = sin(decRad) * sin(latRad) + cos(decRad) * cos(latRad) * cos(haRad);
  sinAlt = max(-1.0f, min(1.0f, sinAlt));
  float altRad = asin(sinAlt);
  trackEl = altRad * 180.0 / PI;


  float cosAz = (sin(decRad) - sinAlt * sin(latRad)) / (cos(altRad) * cos(latRad));
  cosAz = max(-1.0f, min(1.0f, cosAz));
  float azDeg = acos(cosAz) * 180.0 / PI;
  if (sin(haRad) > 0) azDeg = 360.0 - azDeg;


  if (isnan(azDeg)) return -1.0;
  return norm360(azDeg);
}




// ================= network =================


void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("wifi");


  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(10);
    Serial.print(".");
  }


  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" ok " + WiFi.localIP().toString());
  } else {
    Serial.println(" failed, running offline");
  }
}


void syncNTP() {
  if (WiFi.status() != WL_CONNECTED) return;
  configTime(0, 0, ntpServer);
  struct tm timeinfo;
  unsigned long start = millis();
  while (!getLocalTime(&timeinfo, 1000) && millis() - start < 15000) {}
  Serial.println("time synced");
}


void postReadings() {
  if (WiFi.status() != WL_CONNECTED) return;


  WiFiClient client;
  HTTPClient http;
  http.begin(client, readingsUrl);
  http.addHeader("Content-Type", "application/json");


  JsonDocument doc;
  doc["telescope_id"] = telescopeId;
  doc["azimuth_angle"] = azimuthAngle;
  doc["elevation_angle"] = elevationAngle;
  doc["latitude"] = latitude;
  doc["longitude"] = longitude;
  doc["altitude"] = altitude;
  doc["gyroscope_x"] = 0;
  doc["gyroscope_y"] = 0;
  doc["gyroscope_z"] = 0;
  doc["acceleration_x"] = 0;
  doc["acceleration_y"] = 0;
  doc["acceleration_z"] = 0;
  doc["magnetic_field_x"] = trueHeading;   // reusing this field until a dedicated heading field exists
  doc["magnetic_field_y"] = 0;
  doc["magnetic_field_z"] = 0;
  doc["health_status"] = healthStatus;
  doc["movement_status"] = movementStatus;


  String payload;
  serializeJson(doc, payload);
  http.POST(payload);
  http.end();
}


void pollCommands() {
  if (WiFi.status() != WL_CONNECTED) return;


  WiFiClient client;
  HTTPClient http;
  http.begin(client, commandsUrl);


  if (http.GET() != 200) {
    http.end();
    return;
  }


  JsonDocument doc;
  if (deserializeJson(doc, http.getString())) {
    http.end();
    return;
  }


  const char* cmdType = doc["command_type"] | "";


  if (strcmp(cmdType, "point") == 0) {
    float newAz = doc["point"]["target_az_angle"] | -1.0f;
    if (newAz >= 0 && newAz <= 360) {
      apiTarget = norm360(newAz);
      lastApiTime = millis();
      isTracking = false;
      Serial.printf("point %.1f\n", apiTarget);
    }
  } else if (strcmp(cmdType, "track") == 0) {
    track_m1 = doc["track"]["source"]["m_1"] | 0.0f;
    track_m2 = doc["track"]["source"]["m_2"] | 0.0f;
    track_c1 = doc["track"]["source"]["c_1"] | 0.0f;
    track_c2 = doc["track"]["source"]["c_2"] | 0.0f;
    track_T_ra = doc["track"]["source"]["T_ra"] | 1.0f;
    track_A = doc["track"]["source"]["A"] | 0.0f;
    track_phi = doc["track"]["source"]["phi"] | 0.0f;
    track_D = doc["track"]["source"]["D"] | 0.0f;
    track_T_dec = doc["track"]["source"]["T_dec"] | 1.0f;
    isTracking = true;
    lastApiTime = millis();
    Serial.println("track");
  }


  http.end();
}




// ================= setup =================


void setup() {
  Serial.begin(115200);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(potPin, INPUT);
  pinMode(encPin, INPUT);
  pinMode(estopPin, INPUT_PULLUP);
  stopMotor();


  Wire.begin();


  GPS.begin(0x10);
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);


  magOk = myMag.begin();
  if (magOk) myMag.softReset();
  else Serial.println("no magnetometer");


  connectWifi();
  syncNTP();


  // start the pot target where the knob already is, so we do not
  // immediately think the user turned it
  potTarget = readPotAngle();
  encAngle = readEncoderAngle();
  lastPotTime = millis();


  calibStartTime = millis();
  Serial.println("ready");
}




// ================= loop =================


void loop() {
  unsigned long now = millis();


  readGPS();


  // estop
  bool button = digitalRead(estopPin);
  if (button == LOW && lastButton == HIGH) {
    estopState = !estopState;
    delay(10);
  }
  lastButton = button;


  if (estopState) {
    stopMotor();
    movementStatus = "ESTOP";
    healthStatus = "FAULT";
    Serial.println("estop");
    delay(10);
    return;
  }
  healthStatus = "OK";


  // where we are
  float current = readEncoderAngle();
  azimuthAngle = current;

  // magnetometer heading, used as a cross check reference only
  trueHeading = computeTrueHeading();


  // did the user turn the knob
  float pot = readPotAngle();
  pot = 180;
  if (fabs(pot - potTarget) > POT_THRESHOLD) {
    potTarget = pot;
    lastPotTime = now;
    isTracking = false;
  }


  // tracking keeps recomputing its own target
  if (isTracking) {
    float computed = computeAzFromSource();
    if (computed < 0) {
      isTracking = false;
      Serial.println("track failed, clock or params bad");
    } else {
      apiTarget = computed;
      lastApiTime = now;
      if (trackEl < 0) Serial.printf("source below horizon el %.1f\n", trackEl);
    }
  }


  // whichever source spoke most recently wins
  bool apiFresh = (apiTarget >= 0) && (now - lastApiTime < SOURCE_TIMEOUT);
  bool potFresh = (now - lastPotTime < SOURCE_TIMEOUT);


  float target;
  const char* source;
  if (apiFresh && (!potFresh || lastApiTime > lastPotTime)) {
    target = apiTarget;
    source = "api";
  } else {
    target = potTarget;
    source = "pot";
  }
  target = norm360(target);
  /*
    // never aim into the blocked zone
    if (inBlockedZone(target)) {
    stopMotor();
    movementStatus = "BLOCKED";
    Serial.printf("az %.1f tgt %.1f blocked\n", current, target);
    } else {*/
  float error = fabs(cwDistance(current, target));
  if (error > 180.0) error = 360.0 - error;


  int pwm = computePwm(error);
  int dir = chooseDirection(current, target);


  if (pwm == 0) {
    stopMotor();
    movementStatus = "IDLE";
  } else if (dir == 0) {
    stopMotor();
    movementStatus = "NO PATH";
  } else {
    driveMotor(pwm, dir);
    movementStatus = isTracking ? "TRACKING" : "MOVING";
  }


  Serial.printf("az %.1f tgt %.1f mag %.1f el %.1f err %.1f pwm %d %s %s\n", current, target, trueHeading, trackEl, error, pwm, source, movementStatus.c_str());  /*
  }
*/


  // network
  if (now - lastPostTime >= POST_INTERVAL) {
    lastPostTime = now;
    postReadings();
  }
  if (now - lastPollTime >= POLL_INTERVAL) {
    lastPollTime = now;
    pollCommands();
  }



}
