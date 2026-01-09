#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "EspNowSender.h"
#include "Context.h" // To access global gpsData
#include "WebServer.h" // For webSerialLog

// ESP-NOW Direct Point-to-Point Configuration
// REPLACE WITH YOUR ESPHOME RECEIVER MAC ADDRESS (get from ESPHome device)
// Example: {0x34, 0x85, 0x18, 0x7B, 0x56, 0x24}
// To find MAC: check ESPHome logs or web_server interface

// Multiple receiver MAC addresses
uint8_t broadcastAddress1[] = {0xCC, 0xBA, 0x97, 0xF3, 0xC7, 0x28};  // Original receiver
uint8_t broadcastAddress2[] = {0x30, 0xED, 0xA0, 0xAE, 0x0D, 0x60};  // AMOLED display
uint8_t broadcastAddress3[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // Reserved for future

// Array of all receivers for easy iteration
uint8_t* receiverMacs[] = {broadcastAddress1, broadcastAddress2, broadcastAddress3};
const int numReceivers = 2; // Currently using 2 receivers (adjust when adding more) 

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
  uint32_t pingCounter;  // Ping counter for connection tracking
} GpsEspNowPacket;

// Pong response packet structure
typedef struct __attribute__((packed)) {
  uint32_t pingCounter;  // Echo back the ping counter
} PongPacket;

// Track which client we're currently sending to
static int currentSendIndex = -1;
static unsigned long lastTransmitTimes[3] = {0, 0, 0}; // Track per-client transmission times

// Callback when data is received (pong response from receivers)
void OnDataReceived(const esp_now_recv_info_t *recv_info, const uint8_t *data, int size) {
  if (size != sizeof(PongPacket)) {
    Serial.printf("Received unexpected packet size: %d\n", size);
    webSerialLog("ESP-NOW: Received unexpected packet size: " + String(size));
    return;
  }
  
  PongPacket pong;
  memcpy(&pong, data, sizeof(PongPacket));
  
  // Debug: Log the MAC that sent this pong
  Serial.printf("Pong from MAC %02X:%02X:%02X:%02X:%02X:%02X (ping #%u)\n",
                recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
                recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5],
                pong.pingCounter);
  
  // Find which client sent this pong by matching MAC address
  for (int i = 0; i < numReceivers; i++) {
    if (memcmp(recv_info->src_addr, receiverMacs[i], 6) == 0) {
      gpsData.espNowClients[i].lastResponseTime = millis();
      gpsData.espNowClients[i].lastPingReceived = pong.pingCounter;
      gpsData.espNowClients[i].isActive = true;
      
      Serial.printf("ESP-NOW: Pong received from client %d (ping #%u)\n", i + 1, pong.pingCounter);
      // webSerialLog("ESP-NOW: Pong received from client " + String(i + 1) + " (ping #" + String(pong.pingCounter) + ")");
      return;
    }
  }
  
  // If we get here, MAC didn't match any known client
  Serial.printf("WARNING: Unrecognized pong from %02X:%02X:%02X:%02X:%02X:%02X\n",
                recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
                recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);
}

// Callback when data is sent
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  // Update global status only
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
  
  // Print sender's MAC address for receiver configuration
  Serial.print("ESP-NOW Sender MAC: ");
  Serial.println(WiFi.macAddress());
  
  Serial.printf("Configured %d receiver(s):\n", numReceivers);
  for (int i = 0; i < numReceivers; i++) {
    Serial.printf("  Receiver %d: ", i + 1);
    for (int j = 0; j < 6; j++) {
      Serial.printf("%02X", receiverMacs[i][j]);
      if (j < 5) Serial.print(":");
    }
    Serial.println();
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    gpsData.espNowStatus = "Init Failed";
    gpsData.espNowError = "esp_now_init failed";
    return;
  }

  // Register Send and Receive Callbacks
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataReceived);

  // Register all receivers as peers and initialize metrics
  int peersAdded = 0;
  for (int i = 0; i < numReceivers; i++) {
    // Initialize client metrics
    memcpy(gpsData.espNowClients[i].macAddr, receiverMacs[i], 6);
    gpsData.espNowClients[i].isActive = false;
    gpsData.espNowClients[i].lastResponseTime = 0;
    gpsData.espNowClients[i].lastPingReceived = 0;
    
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, receiverMacs[i], 6);
    peerInfo.channel = 0;  // 0 = use current WiFi channel (works with AP_STA mode)
    peerInfo.encrypt = false; // No encryption (can be enabled with a shared key)

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.printf("Failed to add peer %d\n", i + 1);
      gpsData.espNowClients[i].isActive = false;
    } else {
      Serial.printf("Peer %d registered successfully\n", i + 1);
      gpsData.espNowClients[i].isActive = true;
      peersAdded++;
    }
  }
  
  if (peersAdded > 0) {
    gpsData.espNowStatus = "Ready";
    Serial.printf("ESP-NOW ready with %d peer(s)\n", peersAdded);
  } else {
    gpsData.espNowStatus = "Peer Error";
    gpsData.espNowError = "No peers added";
  }
}

void sendGpsDataViaEspNow() {
  if (!gpsData.hasFix) return; // Optional: Only send if we have a fix

  // Increment ping counter for this transmission
  gpsData.espNowPingCounter++;

  // webSerialLog("ESP-NOW: Sending ping #" + String(gpsData.espNowPingCounter) + " to " + String(numReceivers) + " receiver(s)");

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
  packet.pingCounter = gpsData.espNowPingCounter;
//  packet.displayPage = gpsData.displayPage; // Use current display page from global state

  // Send to all registered receivers
  bool anySuccess = false;
  unsigned long currentTime = millis();
  
  for (int i = 0; i < numReceivers; i++) {
    // Always send to allow auto-reconnection, regardless of current status
    esp_err_t result = esp_now_send(receiverMacs[i], (uint8_t *) &packet, sizeof(packet));
    
    if (result == ESP_OK) {
      anySuccess = true;
      // Track successful transmission time for this specific client
      gpsData.espNowClients[i].lastTransmitTime = currentTime;
      lastTransmitTimes[i] = currentTime;
      // webSerialLog("ESP-NOW: Ping sent successfully to client " + String(i + 1));
    } else {
      webSerialLog("ESP-NOW: Failed to send ping to client " + String(i + 1));
    }
  }
  
  if (anySuccess) {
    gpsData.espNowLastTxTime = currentTime;
  } else {
    gpsData.espNowStatus = "Send Error";
    gpsData.espNowError = "esp_now_send returned error";
    webSerialLog("ESP-NOW: All transmissions failed");
  }
}

// Check for client timeouts and update connection status
// Only considers a client connected if a pong was received within the last 30 seconds
void checkEspNowClientTimeouts() {
  unsigned long currentTime = millis();
  int activeClients = 0;
  
  for (int i = 0; i < numReceivers; i++) {
    bool wasActive = gpsData.espNowClients[i].isActive;
    
    // Client is only considered connected if we received a pong within timeout period
    if (gpsData.espNowClients[i].lastResponseTime > 0) {
      unsigned long timeSinceResponse = currentTime - gpsData.espNowClients[i].lastResponseTime;
      
      if (timeSinceResponse <= gpsData.espNowTimeoutMs) {
        // Client is active - pong received within timeout
        gpsData.espNowClients[i].isActive = true;
        activeClients++;
        
        // Log when client becomes active
        if (!wasActive) {
          Serial.printf("Client %d connected (pong received)\n", i + 1);
          webSerialLog("ESP-NOW: Client " + String(i + 1) + " connected (pong received)");
        }
      } else {
        // Client timed out - no pong within timeout period
        gpsData.espNowClients[i].isActive = false;
        
        // Log when client becomes inactive
        if (wasActive) {
          Serial.printf("Client %d disconnected (no pong for %lu ms)\n", i + 1, timeSinceResponse);
          webSerialLog("ESP-NOW: Client " + String(i + 1) + " disconnected (no pong for " + String(timeSinceResponse / 1000) + "s)");
        }
      }
    } else {
      // Never received a pong from this client - mark as inactive
      gpsData.espNowClients[i].isActive = false;
    }
  }
  
  // Update overall status
  if (activeClients > 0) {
    char statusBuf[32];
    snprintf(statusBuf, sizeof(statusBuf), "Connected (%d/%d)", activeClients, numReceivers);
    gpsData.espNowStatus = statusBuf;
  } else {
    gpsData.espNowStatus = "No Clients";
  }
}

// // Set the display page and immediately send an update
// void setDisplayPage(uint8_t page) {
//   if (page > 1) page = 0; // Validate: only pages 0 and 1 exist
//   gpsData.displayPage = page;
//   sendGpsDataViaEspNow(); // Immediately send update to receiver
// }
