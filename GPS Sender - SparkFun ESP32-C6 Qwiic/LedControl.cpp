#include <Arduino.h>
#include <Ticker.h>
#include "LedControl.h"
#include "Config.h"
#include "Context.h"

Ticker ledTicker;

void turnLedOff() {
  digitalWrite(LED_PIN, LOW); // LOW = OFF on SparkFun Qwiic Pocket C6
  gpsData.ledState = false;
}

void initLed() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW); // LOW = OFF on SparkFun Qwiic Pocket C6
}

void triggerLed() {
  if (gpsData.ledMode == LED_OFF || gpsData.ledMode == LED_ON) return;
  if (gpsData.ledState) return;

  digitalWrite(LED_PIN, HIGH); // HIGH = ON on SparkFun Qwiic Pocket C6
  gpsData.ledState = true;
  ledTicker.once_ms(LED_BLINK_DURATION_MS, turnLedOff);
}
