/**
 * GPS-ESP32 Standalone Firmware (Refactored)
 * REQUIRED LIBRARIES:
 * 1. SparkFun u-blox GNSS Arduino Library (v2.2.28)
 * 2. ESPAsyncWebServer
 * 3. AsyncTCP
 * 4. ArduinoJson
 * 5. Ticker
 */

#include <Arduino.h>
#include <WiFi.h>

// Include Custom Modules
#include "Config.h"
#include "Types.h"
#include "Context.h"
#include "LedControl.h"
#include "GpsLogic.h"
#include "WebServer.h"
#include "TcpServer.h"

// Define Global Instances
SFE_UBLOX_GNSS myGNSS;
GPSData gpsData;

void setup() {
  Serial.begin(115200);
  
  // Hardware Init
  initLed();
  gpsData.startTime = millis();
  setupGPS();

  // Network Init
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
  
  setupWeb();
  setupTCP();
}

void loop() {
  // GPS Polling Loop
  if (gpsData.gpsInterval > 0 && (millis() - gpsData.lastGPSPoll >= gpsData.gpsInterval)) {
    gpsData.lastGPSPoll = millis();
    pollGPS();
    broadcastData(); 
  }
  
  // Print Station IP once connected
  static bool connectedPrinted = false;
  if (WiFi.status() == WL_CONNECTED && !connectedPrinted) {
    Serial.print("Station IP: ");
    Serial.println(WiFi.localIP());
    connectedPrinted = true;
  }
  
  delay(1);
}
