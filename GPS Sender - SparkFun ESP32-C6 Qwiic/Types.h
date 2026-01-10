#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>

enum LedMode {
  LED_OFF = 0,
  LED_ON = 1,
  LED_BLINK_ON_GPS_READ = 2,
  LED_BLINK_ON_FIX = 3,
  LED_BLINK_ON_MOVEMENT = 4
};

struct GPSData {
  LedMode ledMode = LED_BLINK_ON_GPS_READ; // Default
  bool isConnected = false;
  bool hasFix = false;
  bool hadFirstFix = false;
  String fixStatus = "Initializing";
  int satellites = 0;
  int satellitesVisible = 0;
  
  unsigned long startTime = 0;
  unsigned long firstFixTime = 0;
  int ttffSeconds = 0;
  
  unsigned long gpsInterval = 5000;
  unsigned long lastGPSPoll = 0;
  
  double lat = 0.0;
  double lon = 0.0;
  double alt = 0.0; 
  double altMSL = 0.0;
  
  float speed = 0.0; 
  float heading = 0.0;
  
  float pdop = 0.0;
  float hdop = 0.0;
  float vdop = 0.0;
  float hAcc = 0.0;
  float vAcc = 0.0;
  
  String timeStr = "00:00:00";
  String dateStr = "1970-01-01";
  String localTimeStr = "--:--:--";
  uint8_t hour, minute, second;
  uint16_t year;
  uint8_t month, day;
  int timezoneOffsetMinutes = 0;
  
  byte fixType = 0;

  double lastLat = 0.0;
  double lastLon = 0.0;
  const double movementThreshold = 0.00001; 
  
  bool ledState = false; 
  bool configSaved = false;
  bool timeSynced = false;  // Track if system time has been synced from GPS
  
  // Demo Mode (sends fake GPS data)
  bool demoMode = false;

  float cpuTemp = 0.0;
  
  // ESP-NOW Status
  String espNowStatus = "Disabled";
  String espNowError = "";
  unsigned long espNowLastTxTime = 0;
  
  // Per-client ESP-NOW Metrics (up to 3 clients)
  struct EspNowClientMetrics {
    uint8_t macAddr[6];
    bool isActive;
    unsigned long lastResponseTime;  // Last time we got a pong response
    unsigned long lastTransmitTime;  // Last time we successfully transmitted to this client
    uint32_t lastPingReceived;       // Last ping value received back
  };
  EspNowClientMetrics espNowClients[3];
  
  // Ping-pong mechanism for connection tracking
  uint32_t espNowPingCounter = 0;    // Incremented with each send
  const unsigned long espNowTimeoutMs = 30000; // 30 seconds timeout (client must pong within this time)

  // Min/Max Statistics
  double altMin = 99999.0;
  double altMax = -99999.0;
  float speedMax = 0.0;
  int satellitesMax = 0;
  int satellitesVisibleMax = 0;
  float pdopMin = 100.0;
  float hdopMin = 100.0;
  float vdopMin = 100.0;
  float hAccMin = 99999.0;
  float vAccMin = 99999.0;
};

#endif
