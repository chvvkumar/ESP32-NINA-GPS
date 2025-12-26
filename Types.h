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
};

#endif
