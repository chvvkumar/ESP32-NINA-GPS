#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "EspNowSender.h"
#include "Context.h" // To access global gpsData

// ESP-NOW Direct Point-to-Point Configuration
// REPLACE WITH YOUR ESPHOME RECEIVER MAC ADDRESS (get from ESPHome device)
// Example: {0x34, 0x85, 0x18, 0x7B, 0x56, 0x24}
// To find MAC: check ESPHome logs or web_server interface
static uint8_t receiverMac[] = {0xCC, 0xBA, 0x97, 0xF3, 0xC7, 0x28}; 

// Define the struct matching the receiver
typedef struct __attribute__((packed)) {
  double lat;
  double lon;
  float alt;
  float speed;
  float heading;
  uint8_t sats;
  uint8_t satsVisible;
  uint8_t fixType;
  char localTime[10];
  float pdop;
  float hdop;
  float vdop;
  float hAcc;
  float vAcc;
  uint32_t stationIp;
} GpsEspNowPacket;

// Callback when data is sent
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    gpsData.espNowStatus = "Active (Sent)";
    gpsData.espNowError = ""; // Clear error
  } else {
    gpsData.espNowStatus = "Delivery Failed";
    gpsData.espNowError = "Packet delivery failed";
  }
}

void setupEspNow() {
  // ESP-NOW works independently of WiFi connection
  // It can operate even when WiFi is connected to a router (WIFI_AP_STA mode)
  // No need to change WiFi mode - ESP-NOW uses the WiFi radio directly
  
  Serial0.println("[ESP-NOW] Initializing...");
  
  // Print sender's MAC address for receiver configuration
  Serial0.print("[ESP-NOW] Sender MAC: ");
  Serial0.println(WiFi.macAddress());
  Serial0.print("[ESP-NOW] Receiver MAC: ");
  for (int i = 0; i < 6; i++) {
    Serial0.printf("%02X", receiverMac[i]);
    if (i < 5) Serial0.print(":");
  }
  Serial0.println();

  if (esp_now_init() != ESP_OK) {
    Serial0.println("[ESP-NOW] \u2717 ERROR: Initialization failed");
    gpsData.espNowStatus = "Init Failed";
    gpsData.espNowError = "esp_now_init failed";
    return;
  }

  // Register Send Callback
  esp_now_register_send_cb(OnDataSent);

  // Register receiver as peer for direct point-to-point communication
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, receiverMac, 6);
  peerInfo.channel = 0;  // 0 = use current WiFi channel (works with AP_STA mode)
  peerInfo.encrypt = false; // No encryption (can be enabled with a shared key)

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial0.println("[ESP-NOW] \u2717 ERROR: Failed to add peer");
    gpsData.espNowStatus = "Peer Error";
    gpsData.espNowError = "Failed to add peer";
  } else {
    Serial0.println("[ESP-NOW] \u2713 Peer registered successfully");
    gpsData.espNowStatus = "Ready";
  }
}

void sendGpsDataViaEspNow() {
  if (!gpsData.hasFix) return; // Optional: Only send if we have a fix

  GpsEspNowPacket packet;
  packet.lat = gpsData.lat;
  packet.lon = gpsData.lon;
  packet.alt = (float)gpsData.alt;
  packet.speed = gpsData.speed;
  packet.heading = gpsData.heading;
  packet.sats = (uint8_t)gpsData.satellites;
  packet.satsVisible = (uint8_t)gpsData.satellitesVisible;
  packet.fixType = gpsData.fixType;
  strncpy(packet.localTime, gpsData.localTimeStr.c_str(), sizeof(packet.localTime));
  
  packet.pdop = gpsData.pdop;
  packet.hdop = gpsData.hdop;
  packet.vdop = gpsData.vdop;
  packet.hAcc = gpsData.hAcc;
  packet.vAcc = gpsData.vAcc;
  
  packet.stationIp = WiFi.localIP();
//  packet.displayPage = gpsData.displayPage; // Use current display page from global state

  esp_err_t result = esp_now_send(receiverMac, (uint8_t *) &packet, sizeof(packet));
  
  if (result == ESP_OK) {
      gpsData.espNowLastTxTime = millis();
  } else {
      gpsData.espNowStatus = "Send Error";
      gpsData.espNowError = "esp_now_send returned error";
  }
}

// // Set the display page and immediately send an update
// void setDisplayPage(uint8_t page) {
//   if (page > 1) page = 0; // Validate: only pages 0 and 1 exist
//   gpsData.displayPage = page;
//   sendGpsDataViaEspNow(); // Immediately send update to receiver
// }
