#include <Arduino.h>
#include "secrets.hpp"

constexpr uint8_t RED_LED_PIN = 21;
constexpr uint8_t GREEN_LED_PIN = 13;
constexpr uint8_t SERVO_PIN = 12;

constexpr int SERVO_CHANNEL = 0;
constexpr int SERVO_MIN_PWM = 3276;   // 5% duty → ~1ms pulse → 0°
constexpr int SERVO_MAX_PWM = 6553;

void setServoAngle(int angle) {
  angle = constrain(angle, 0, 180);

  int pwmValue = map(angle, 0, 180, SERVO_MIN_PWM, SERVO_MAX_PWM);

  ledcWrite(SERVO_CHANNEL, pwmValue);
}

void setup() {
  Serial.begin(115200);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  ledcSetup(0, 50, 16);
  ledcAttachPin(SERVO_PIN, 0);
}

void loop() {
  digitalWrite(RED_LED_PIN, HIGH);
  digitalWrite(GREEN_LED_PIN, HIGH);

  setServoAngle(0);

  delay(1000);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, LOW);

  setServoAngle(90);

  delay(1000);

  setServoAngle(260);
  delay(1000);
}