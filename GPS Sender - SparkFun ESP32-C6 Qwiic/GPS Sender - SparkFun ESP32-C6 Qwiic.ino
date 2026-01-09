/**
 * GPS-ESP32 Standalone Firmware (Refactored for SparkFun Qwiic Pocket ESP32-C6)
 * REQUIRED LIBRARIES:
 * 1. SparkFun u-blox GNSS Arduino Library (v2.2.28)
 * 2. ESPAsyncWebServer (Use the version by mathieucarbou for ESP32-C6 support)
 * 3. AsyncTCP (Handled by the library above)
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
#include "Storage.h"
#include "EspNowSender.h"

// Define Global Instances
SFE_UBLOX_GNSS myGNSS;
GPSData gpsData;

void setup() {
  Serial.begin(115200);
  
  // Storage Init (Load Saved Stats)
  storage.begin();

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
  setupEspNow();
}

void loop() {
  // PRIORITY 1: Handle OTA updates
  webLoop(); 
  // removed redundant calls to free up CPU for GPS polling
  
  // Skip all other activities during OTA update
  if (otaInProgress) {
    webLoop(); // Focus entirely on OTA
    delay(10); // Small delay to let OTA process
    return;
  }
  
  bool shouldBroadcast = false;

  // GPS Polling Loop
  if (gpsData.gpsInterval > 0 && (millis() - gpsData.lastGPSPoll >= gpsData.gpsInterval)) {
    gpsData.lastGPSPoll = millis();
    pollGPS();
    gpsData.cpuTemp = temperatureRead();
    sendGpsDataViaEspNow();
    checkEspNowClientTimeouts();  // Check for client timeouts after sending
    shouldBroadcast = true;
  }
  
  // Check for new TCP clients to send immediate data
  if (hasNewConnections()) {
    shouldBroadcast = true;
  }

  if (shouldBroadcast) {
    broadcastData();
  }
  
  // WiFi connection management
  static bool connectedPrinted = false;
  static bool apDisabled = false;
  static unsigned long lastReconnectAttempt = 0;
  
  if (WiFi.status() == WL_CONNECTED) {
    // First time connected - print IP and disable AP
    if (!connectedPrinted) {
      Serial.print("Station IP: ");
      Serial.println(WiFi.localIP());
      Serial.println("WiFi connected - disabling AP mode");
      WiFi.softAPdisconnect(true);
      connectedPrinted = true;
      apDisabled = true;
    }
  } else if (apDisabled) {
    // Lost connection - attempt reconnect every 30 seconds
    if (millis() - lastReconnectAttempt >= 30000) {
      lastReconnectAttempt = millis();
      Serial.println("WiFi disconnected - attempting reconnect...");
      WiFi.disconnect();
      WiFi.reconnect();
    }
  }
  
  // Call webLoop again at the end for responsiveness
  webLoop();
  
  // Minimal delay to prevent watchdog issues
  yield();
}
