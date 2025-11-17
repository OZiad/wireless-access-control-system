#include "secrets.hpp"
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

// --------- Hardware pins ----------
constexpr uint8_t RED_LED_PIN = 21;
constexpr uint8_t GREEN_LED_PIN = 13;
constexpr uint8_t SERVO_PIN = 12;

// --------- Servo PWM config ----------
constexpr int SERVO_CHANNEL = 0;
constexpr int SERVO_MIN_PWM = 3276; // ~1 ms pulse -> 0°
constexpr int SERVO_MAX_PWM = 6553; // ~2 ms pulse -> 180°
constexpr int SERVO_LOCK_ANGLE = 0;
constexpr int SERVO_UNLOCK_ANGLE = 90;

// --------- Access control config ----------
const char *ROOM_USER = "roomuser";
const char *ROOM_PASS = "roompass";

constexpr uint8_t MAX_FAILED_ATTEMPTS = 3;
constexpr unsigned long SUSPEND_DURATION_MS = 2UL * 60UL * 1000UL; // 2 minutes

// --------- State ----------
WebServer server(80);

uint8_t failedAttempts = 0;
bool isSuspended = false;
unsigned long suspendUntil = 0;

// --------- Servo helper ----------
void setServoAngle(int angle) {
  angle = constrain(angle, 0, 180);
  int pwmValue = map(angle, 0, 180, SERVO_MIN_PWM, SERVO_MAX_PWM);
  ledcWrite(SERVO_CHANNEL, pwmValue);
}

// --------- LED + servo behaviours ----------
void indicateSuccess() {
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, HIGH);

  setServoAngle(SERVO_UNLOCK_ANGLE);
  delay(2500);

  // lock again
  setServoAngle(SERVO_LOCK_ANGLE);
  digitalWrite(GREEN_LED_PIN, LOW);
}

void indicateFailure() {
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, HIGH);
  delay(1000);
  digitalWrite(RED_LED_PIN, LOW);
}

// --------- HTML helpers ----------
String getLoginPage(const String &message = "") {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>Room Access Control</title>
</head>
<body>
  <h2>Room Access Control - Login</h2>
)rawliteral";

  if (message.length() > 0) {
    html += "<p><b>" + message + "</b></p>";
  }

  html += R"rawliteral(
  <form action="/login" method="POST">
    <label>User:</label><br>
    <input type="text" name="user"><br><br>
    <label>Password:</label><br>
    <input type="password" name="pass"><br><br>
    <input type="submit" value="Open Door">
  </form>
</body>
</html>
)rawliteral";

  return html;
}

String getSuspendedPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>Access Suspended</title>
</head>
<body>
  <h2>Access temporarily suspended</h2>
  <p>Too many incorrect attempts. Please wait 2 minutes and try again.</p>
</body>
</html>
)rawliteral";
  return html;
}

String getSuccessPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>Access Granted</title>
</head>
<body>
  <h2>Access Granted :) </h2>
  <p>The door lock has been released.</p>
  <a href="/">Back to login</a>
</body>
</html>
)rawliteral";
  return html;
}

String getFailurePage(uint8_t remainingAttempts) {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>Access Denied</title>
</head>
<body>
  <h2>Access Denied :( </h2>
)rawliteral";

  html += "<p>Invalid user or password.</p>";
  html += "<p>Remaining attempts before lockout: ";
  html += String(remainingAttempts);
  html += "</p><a href=\"/\">Back to login</a></body></html>";

  return html;
}

// --------- Web handlers ----------
void handleRoot() {
  if (isSuspended && millis() < suspendUntil) {
    server.send(200, "text/html", getSuspendedPage());
    return;
  }

  server.send(200, "text/html", getLoginPage());
}

void handleLogin() {
  if (isSuspended && millis() < suspendUntil) {
    server.send(200, "text/html", getSuspendedPage());
    return;
  }

  if (!server.hasArg("user") || !server.hasArg("pass")) {
    server.send(400, "text/html",
                "<h2>Bad Request</h2><p>Missing user or pass.</p>");
    return;
  }

  String user = server.arg("user");
  String pass = server.arg("pass");

  bool okUser = (user == ROOM_USER);
  bool okPass = (pass == ROOM_PASS);

  if (okUser && okPass) {
    failedAttempts = 0;
    isSuspended = false;
    suspendUntil = 0;

    indicateSuccess();
    server.send(200, "text/html", getSuccessPage());
  } else {
    failedAttempts++;

    if (failedAttempts >= MAX_FAILED_ATTEMPTS) {
      isSuspended = true;
      suspendUntil = millis() + SUSPEND_DURATION_MS;
      failedAttempts = 0;
      server.send(200, "text/html", getSuspendedPage());
    } else {
      uint8_t remaining = MAX_FAILED_ATTEMPTS - failedAttempts;
      indicateFailure();
      server.send(200, "text/html", getFailurePage(remaining));
    }
  }
}

void handleNotFound() {
  server.send(404, "text/html", "<h2>404 Not Found</h2>");
}

// --------- WiFi setup ----------
void connectWiFi() {
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFiClass::mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint8_t retries = 0;
  while (WiFiClass::status() != WL_CONNECTED && retries < 60) { // ~30 seconds
    delay(500);
    Serial.print(".");
    retries++;
  }

  Serial.println();

  if (WiFiClass::status() == WL_CONNECTED) {
    Serial.print("Connected! IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Failed to connect to WiFi.");
  }
}

// --------- Arduino setup/loop ----------
void setup() {
  Serial.begin(115200);

  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);

  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, LOW);

  // Servo PWM
  ledcSetup(SERVO_CHANNEL, 50, 16);
  ledcAttachPin(SERVO_PIN, SERVO_CHANNEL);

  // Start in locked position
  setServoAngle(SERVO_LOCK_ANGLE);

  connectWiFi();

  // Setup HTTP routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/login", HTTP_POST, handleLogin);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();

  // If suspension time is over, clear suspension flag
  if (isSuspended && millis() >= suspendUntil) {
    isSuspended = false;
    failedAttempts = 0;
  }
}
