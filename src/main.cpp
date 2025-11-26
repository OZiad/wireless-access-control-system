#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include <SPI.h>
#include <hal/hal.h>
#include <lmic.h>

#include "secrets.hpp" // contains wifi credentials + ttn application keys

// ---------------- Pins ----------------
constexpr uint8_t RED_LED_PIN = 21;
constexpr uint8_t GREEN_LED_PIN = 13;
constexpr uint8_t SERVO_PIN = 12;
constexpr uint8_t IR_SENSOR_PIN = 34;

// --------------- Servo -----------------
constexpr int SERVO_CHANNEL = 0;
constexpr int SERVO_MIN_PWM = 3276; // ~1ms
constexpr int SERVO_MAX_PWM = 6553; // ~2ms
constexpr int SERVO_LOCK_ANGLE = 0;
constexpr int SERVO_UNLOCK_ANGLE = 90;

void setServoAngle(int angle) {
  angle = constrain(angle, 0, 180);
  int pwmValue = map(angle, 0, 180, SERVO_MIN_PWM, SERVO_MAX_PWM);
  ledcWrite(SERVO_CHANNEL, pwmValue);
}

constexpr int IR_THRESHOLD = 1000 ;

bool isPersonDetected() {
  const int samples = 10;
  long total = 0;

  analogRead(IR_SENSOR_PIN); // discard first value in case of fake readings

  for (int i = 0; i < samples; i++) {
    int value = analogRead(IR_SENSOR_PIN);
    total += value;
    delay(10);
  }

  int avg = total / samples;

  Serial.print("IR avg value: ");
  Serial.println(avg);

  return avg > IR_THRESHOLD;
}


const char *ROOM_USER = "roomuser";
const char *ROOM_PASS = "roompass";

// ------------- Web server --------------
WebServer server(80);

// static const u1_t PROGMEM APPEUI[8] = {0x00, 0x00, 0x00, 0x00,
//                                        0x00, 0x00, 0x00, 0x00};
//
// static const u1_t PROGMEM DEVEUI[8] = {0x84, 0x44, 0x07, 0xD0,
//                                        0x7E, 0xD5, 0xB3, 0x70};
//
// static const u1_t PROGMEM APPKEY[16] = {0x4C, 0x68, 0x4C, 0x79, 0xC9, 0x78,
//                                         0x5B, 0x23, 0xF5, 0x64, 0xFB, 0x02,
//                                         0x60, 0x8F, 0x0A, 0x79};

void os_getArtEui(u1_t *buf) { memcpy_P(buf, APPEUI, 8); }
void os_getDevEui(u1_t *buf) { memcpy_P(buf, DEVEUI, 8); }
void os_getDevKey(u1_t *buf) { memcpy_P(buf, APPKEY, 16); }

const lmic_pinmap lmic_pins = {
    .nss = 18, .rxtx = LMIC_UNUSED_PIN, .rst = 14, .dio = {26, 33, 32}};

static bool ttnJoined = false;

// --------- LMIC event handler ----------
void onEvent(ev_t ev) {
  Serial.print(os_getTime());
  Serial.print(": ");
  switch (ev) {
  case EV_JOINING:
    Serial.println(F("EV_JOINING"));
    break;
  case EV_JOINED:
    Serial.println(F("EV_JOINED"));
    ttnJoined = true;
    LMIC_setLinkCheckMode(0);
    break;
  case EV_JOIN_FAILED:
    Serial.println(F("EV_JOIN_FAILED"));
    break;
  case EV_TXCOMPLETE:
    Serial.println(F("EV_TXCOMPLETE"));
    break;
  default:
    Serial.print(F("Unknown event: "));
    Serial.println((unsigned)ev);
    break;
  }
}

// --------- send log payload to TTN -----
void sendLogToTTN(bool okCreds, bool okPresence, const String &user) {
  if (!ttnJoined) {
    Serial.println(F("[TTN] not joined yet, skip log"));
    return;
  }
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println(F("[TTN] TX pending, skip log"));
    return;
  }

  char payload[40];
  snprintf(payload, sizeof(payload), "U:%s C:%d P:%d", user.c_str(),
           okCreds ? 1 : 0, okPresence ? 1 : 0);

  uint8_t len = strlen(payload);
  LMIC_setTxData2(1, (uint8_t *)payload, len, 0);
  Serial.print(F("[TTN] queued: "));
  Serial.println(payload);
}

void indicateSuccess() {
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, HIGH);
  setServoAngle(SERVO_UNLOCK_ANGLE);
  delay(1500);
  setServoAngle(SERVO_LOCK_ANGLE);
  digitalWrite(GREEN_LED_PIN, LOW);
}

void indicateFailure() {
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, HIGH);
  delay(800);
  digitalWrite(RED_LED_PIN, LOW);
}

String loginPage(const String &msg = "") {
  String html = "<!DOCTYPE html><html><head><meta "
                "charset='utf-8'><title>Access</title></head><body>"
                "<h2>Room Access Control</h2>";
  if (msg.length() > 0) {
    html += "<p><b>" + msg + "</b></p>";
  }
  html += "<form action='/login' method='POST'>"
          "User:<br><input name='user'><br><br>"
          "Password:<br><input type='password' name='pass'><br><br>"
          "<input type='submit' value='Open door'>"
          "</form></body></html>";
  return html;
}

void handleRoot() { server.send(200, "text/html", loginPage()); }

void handleLogin() {
  if (!server.hasArg("user") || !server.hasArg("pass")) {
    server.send(400, "text/html", "<h3>Missing user or password</h3>");
    return;
  }

  String user = server.arg("user");
  String pass = server.arg("pass");

  bool okCreds = (user == ROOM_USER) && (pass == ROOM_PASS);
  bool okPresence = false;

  if (okCreds) {
    okPresence = isPersonDetected();
  }

  // log every attempt to TTN
  sendLogToTTN(okCreds, okPresence, user);

  if (okCreds && okPresence) {
    indicateSuccess();
    server.send(
        200, "text/html",
        "<h2>Access granted</h2><p>Door unlocked.</p><a href='/'>Back</a>");
  } else {
    indicateFailure();
    String reason;
    if (!okCreds)
      reason = "Invalid credentials.";
    else
      reason = "No person detected in front of device.";
    server.send(200, "text/html",
                "<h2>Access denied</h2><p>" + reason +
                    "</p><a href='/'>Back</a>");
  }
}

void handleNotFound() { server.send(404, "text/html", "<h3>Not found</h3>"); }

// ------------- WiFi --------------------
void connectWiFi() {
  Serial.print("WiFi SSID: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint8_t tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect FAILED (web login will not work).");
  }
}

void setupTTN() {
  os_init();
  LMIC_reset();
  LMIC_startJoining();
  Serial.println(F("[TTN] starting OTAA join"));
}

void setup() {
  Serial.begin(115200);
  analogSetPinAttenuation(IR_SENSOR_PIN, ADC_11db);
  delay(500);

  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, LOW);

  ledcSetup(SERVO_CHANNEL, 50, 16);
  ledcAttachPin(SERVO_PIN, SERVO_CHANNEL);
  setServoAngle(0);

  pinMode(IR_SENSOR_PIN, INPUT);

  connectWiFi();
  server.on("/", HTTP_GET, handleRoot);
  server.on("/login", HTTP_POST, handleLogin);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");

  setupTTN();
}

void loop() {
  server.handleClient(); // handle web requests
  os_runloop_once();     // run LMIC state machine
}
