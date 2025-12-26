/**
 * GPS-ESP32 Standalone Firmware (Refactored)
 * REQUIRED LIBRARIES:
 * 1. SparkFun u-blox GNSS Arduino Library (v2.2.28)
 * 2. ESPAsyncWebServer
 * 3. AsyncTCP
 * 4. ArduinoJson
 * 5. Ticker
 * 6. ElegantOTA
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

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

  Preferences prefs;
  prefs.begin("wifi_config", true); // Read-only
  String savedSSID = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  prefs.end();

  if (savedSSID.length() > 0) {
    Serial.print("Connecting to Saved WiFi: "); Serial.println(savedSSID);
    WiFi.begin(savedSSID.c_str(), savedPass.c_str());
  } else {
    Serial.print("Connecting to Config WiFi: "); Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
  
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
  
  setupWeb();
  setupTCP();
}

void loop() {
  bool shouldBroadcast = false;

  // GPS Polling Loop
  if (gpsData.gpsInterval > 0 && (millis() - gpsData.lastGPSPoll >= gpsData.gpsInterval)) {
    gpsData.lastGPSPoll = millis();
    pollGPS();
    shouldBroadcast = true;
  }
  
  // Check for new TCP clients to send immediate data
  if (hasNewConnections()) {
    shouldBroadcast = true;
  }

  if (shouldBroadcast) {
    broadcastData();
  }
  
  // Print Station IP once connected
  static bool connectedPrinted = false;
  if (WiFi.status() == WL_CONNECTED && !connectedPrinted) {
    Serial.print("Station IP: ");
    Serial.println(WiFi.localIP());
    connectedPrinted = true;
  }
  
  webLoop();
  delay(1);
}
