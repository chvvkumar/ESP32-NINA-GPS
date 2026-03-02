#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "NtpServer.h"
#include "Context.h"   // To access gpsData
#include "WebServer.h" // For logging

#define NTP_PORT 123
#define NTP_PACKET_SIZE 48
#define SEVENTY_YEARS 2208988800UL

WiFiUDP udp;
byte packetBuffer[NTP_PACKET_SIZE];

void setupNtp() {
  udp.begin(NTP_PORT);
  webSerialLog("NTP Server started on port 123 (Stratum 1 - GPS)");
}

void loopNtp() {
  int packetSize = udp.parsePacket();
  if (!packetSize) return;

  // We've received a packet, read the data from it
  udp.read(packetBuffer, NTP_PACKET_SIZE);

  // Only handle standard NTP client requests (Mode 3)
  // LI, VN, Mode. Mode is in the last 3 bits of the first byte.
  byte mode = packetBuffer[0] & 0x07;
  if (mode != 3) return; 

  // Prepare the response (Mode 4 - Server)
  // LI=0 (No warning), VN=4 (Version), Mode=4 (Server) -> 00 100 100 -> 0x24
  packetBuffer[0] = 0x24; 
  packetBuffer[1] = 1;     // Stratum 1 (Primary Reference via GPS)
  packetBuffer[2] = 6;     // Polling interval (2^6 = 64s)
  packetBuffer[3] = 0xEC;  // Precision (-20 = ~1 microsecond)
  
  // Root Delay & Dispersion (Zero for Stratum 1 usually, or very small)
  memset(&packetBuffer[4], 0, 8); 
  
  // Reference Identifier: "GPS\0" (ASCII)
  packetBuffer[12] = 'G';
  packetBuffer[13] = 'P';
  packetBuffer[14] = 'S';
  packetBuffer[15] = 0;

  // Timestamps
  // We need to construct the current NTP time
  // 1. Get Unix Time (Seconds since 1970)
  struct tm tm;
  tm.tm_year = gpsData.year - 1900;
  tm.tm_mon  = gpsData.month - 1;
  tm.tm_mday = gpsData.day;
  tm.tm_hour = gpsData.hour;
  tm.tm_min  = gpsData.minute;
  tm.tm_sec  = gpsData.second;
  time_t unixTime = mktime(&tm);

  // 2. Convert to NTP Time (Seconds since 1900)
  unsigned long highWord = unixTime + SEVENTY_YEARS;
  
  // 3. Convert Milliseconds to Fractional Part (32-bit)
  // Formula: (ms / 1000.0) * 0xFFFFFFFF
  // More precise: (ms * 4294967296ULL) / 1000
  unsigned long lowWord = (gpsData.millisecond * 4294967296ULL) / 1000;

  // Reference Timestamp (Time last set) - Use current GPS time
  packetBuffer[16] = (highWord >> 24) & 0xFF;
  packetBuffer[17] = (highWord >> 16) & 0xFF;
  packetBuffer[18] = (highWord >> 8) & 0xFF;
  packetBuffer[19] = highWord & 0xFF;
  packetBuffer[20] = (lowWord >> 24) & 0xFF;
  packetBuffer[21] = (lowWord >> 16) & 0xFF;
  packetBuffer[22] = (lowWord >> 8) & 0xFF;
  packetBuffer[23] = lowWord & 0xFF;

  // Origin Timestamp (T1) - Copy Transmit Timestamp from request (bytes 40-47) 
  // to Origin Timestamp in response (bytes 24-31)
  for (int i = 0; i < 8; i++) {
    packetBuffer[24 + i] = packetBuffer[40 + i];
  }

  // Receive Timestamp (T2) - Time request received (current GPS time)
  packetBuffer[32] = (highWord >> 24) & 0xFF;
  packetBuffer[33] = (highWord >> 16) & 0xFF;
  packetBuffer[34] = (highWord >> 8) & 0xFF;
  packetBuffer[35] = highWord & 0xFF;
  packetBuffer[36] = (lowWord >> 24) & 0xFF;
  packetBuffer[37] = (lowWord >> 16) & 0xFF;
  packetBuffer[38] = (lowWord >> 8) & 0xFF;
  packetBuffer[39] = lowWord & 0xFF;

  // Transmit Timestamp (T3) - Time reply sent (current GPS time)
  // In a more precise implementation, you would capture the exact moment
  // before transmission, but for simplicity we use the same time
  packetBuffer[40] = (highWord >> 24) & 0xFF;
  packetBuffer[41] = (highWord >> 16) & 0xFF;
  packetBuffer[42] = (highWord >> 8) & 0xFF;
  packetBuffer[43] = highWord & 0xFF;
  packetBuffer[44] = (lowWord >> 24) & 0xFF;
  packetBuffer[45] = (lowWord >> 16) & 0xFF;
  packetBuffer[46] = (lowWord >> 8) & 0xFF;
  packetBuffer[47] = lowWord & 0xFF;

  // Send the reply
  udp.beginPacket(udp.remoteIP(), udp.remotePort());
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
  
  // Optional: Log to WebSerial sparingly (every 10th request to avoid spam)
  static int requestCount = 0;
  requestCount++;
  if (requestCount % 10 == 0) {
    webSerialLog("NTP: " + String(requestCount) + " requests served (last: " + udp.remoteIP().toString() + ")");
  }
}
