/**
 * GPS-ESP32 Standalone Firmware (Refactored)
 * 
 * REQUIRED LIBRARIES:
 * 1. SparkFun u-blox GNSS Arduino Library (v2.2.28)
 * 2. ESPAsyncWebServer
 * 3. AsyncTCP
 * 4. ArduinoJson
 * 5. Ticker
 * 6. ElegantOTA
 * 
 * ESP32-S3 USB CDC CONFIGURATION:
 * This firmware follows ESP32 CDC/DFU flashing guidelines for ESP32-S3.
 * Reference: https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/cdc_dfu_flash.html
 * 
 * Arduino IDE/CLI Board Settings:
 * - USB Mode: USB-OTG (TinyUSB)
 * - USB CDC On Boot: Enabled
 * - Upload Mode: UART0 / Hardware CDC
 * 
 * Important: After the first flash, manually reset the device (press RESET button).
 * The compile-and-upload.ps1 script automatically applies these settings.
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
  Serial0.begin(115200);
  
  // Storage Init (Load Saved Stats)
  Serial0.println("[INIT] Initializing storage...");
  storage.begin();

  // Hardware Init
  Serial0.println("[INIT] Initializing LED...");
  initLed();
  gpsData.startTime = millis();
  Serial0.println("[INIT] Initializing GPS on Wire1 (SDA:41, SCL:40)...");
  setupGPS();

  // Network Init
  Serial0.println("[INIT] Configuring WiFi (AP+STA mode)...");
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);

  Preferences prefs;
  prefs.begin("wifi_config", true); // Read-only
  String savedSSID = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  prefs.end();

  if (savedSSID.length() > 0) {
    Serial0.print("[WIFI] Attempting to connect to SSID: "); Serial0.println(savedSSID);
    WiFi.begin(savedSSID.c_str(), savedPass.c_str());
  } else {
    Serial0.print("[WIFI] Attempting to connect to SSID: "); Serial0.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
  
  // Wait for WiFi connection (following Adafruit example pattern)
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) { // 20 second timeout
    delay(500);
    Serial0.print(".");
    attempts++;
  }
  Serial0.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial0.println("[WIFI] ✓ Connected to WiFi!");
    Serial0.print("[WIFI] Station IP: ");
    Serial0.println(WiFi.localIP());
    Serial0.print("[WIFI] SSID: ");
    Serial0.println(WiFi.SSID());
    Serial0.print("[WIFI] Signal Strength: ");
    Serial0.print(WiFi.RSSI());
    Serial0.println(" dBm");
  } else {
    Serial0.println("[WIFI] ✗ Failed to connect to WiFi (timeout)");
    Serial0.println("[WIFI] Device will operate in AP-only mode");
  }
  
  Serial0.print("[AP] Access Point SSID: "); Serial0.println(AP_SSID ? AP_SSID : "ESP32-GPS");
  Serial0.print("[AP] Access Point IP: "); Serial0.println(WiFi.softAPIP());
  
  Serial0.println("[INIT] Starting Web Server...");
  setupWeb();
  Serial0.println("[INIT] Starting TCP Server on port " + String(TCP_PORT) + "...");
  setupTCP();
  Serial0.println("[INIT] Initializing ESP-NOW...");
  setupEspNow();
  Serial0.println("\n=== Setup Complete ===");
  Serial0.println("Waiting for WiFi connection...");
}

void loop() {
  bool shouldBroadcast = false;

  // GPS Polling Loop
  if (gpsData.gpsInterval > 0 && (millis() - gpsData.lastGPSPoll >= gpsData.gpsInterval)) {
    gpsData.lastGPSPoll = millis();
    pollGPS();
    gpsData.cpuTemp = temperatureRead();
    sendGpsDataViaEspNow();
    shouldBroadcast = true;
  }
  
  // Check for new TCP clients to send immediate data
  if (hasNewConnections()) {
    shouldBroadcast = true;
  }

  if (shouldBroadcast) {
    broadcastData();
  }
  
  webLoop();
  delay(1);
}
