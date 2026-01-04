#include <Arduino.h>
#include <Ticker.h>
#include <Adafruit_NeoPixel.h>
#include "LedControl.h"
#include "Config.h"
#include "Context.h"

Ticker ledTicker;

// Adafruit QT Py ESP32-S3 NeoPixel Setup
// PIN_NEOPIXEL is usually 39, NEOPIXEL_POWER is usually 38
#ifndef PIN_NEOPIXEL
  #define PIN_NEOPIXEL 39
#endif

Adafruit_NeoPixel pixels(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

void turnLedOff() {
  pixels.clear();
  pixels.show();
  gpsData.ledState = false;
}

void initLed() {
    // 1. Turn on power to the NeoPixel (Specific to QT Py S3)
    #if defined(NEOPIXEL_POWER)
      pinMode(NEOPIXEL_POWER, OUTPUT);
      digitalWrite(NEOPIXEL_POWER, HIGH);
    #endif

    // 2. Initialize the strip
    pixels.begin();
    pixels.setBrightness(20); // Low brightness to save power
    pixels.clear();
    pixels.show();
}

void triggerLed() {
  if (gpsData.ledMode == LED_OFF) {
      turnLedOff();
      return;
  }
  if (gpsData.ledMode == LED_ON) {
      // Steady Green
      pixels.setPixelColor(0, pixels.Color(0, 255, 0));
      pixels.show();
      return;
  }
  
  if (gpsData.ledState) return; // Already blinking

  // Color Coding for Status
  uint32_t color = pixels.Color(0, 0, 255); // Default Blue

  if (gpsData.hasFix) {
      color = pixels.Color(0, 255, 0); // Green if Fixed
  } else {
      color = pixels.Color(255, 0, 0); // Red if No Fix
  }

  pixels.setPixelColor(0, color);
  pixels.show();
  
  gpsData.ledState = true;
  ledTicker.once_ms(LED_BLINK_DURATION_MS, turnLedOff);
}
