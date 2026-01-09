#include <Arduino.h>
#include <Wire.h>
#include "GpsLogic.h"
#include "Config.h"
#include "Context.h"
#include "LedControl.h"
#include "Storage.h"
#include "WebServer.h"

void setupGPS() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  
  // Increase I2C buffer for large NAV-SAT messages
  myGNSS.setPacketCfgPayloadSize(1024);

  if (myGNSS.begin(Wire, 0x42) == false) {
    Serial.println(F("u-blox GNSS not detected. Check wiring!"));
    gpsData.isConnected = false;
  } else {
    Serial.println(F("u-blox GNSS connected"));
    gpsData.isConnected = true;
    
    myGNSS.setI2COutput(COM_TYPE_UBX); 
    myGNSS.setMeasurementRate(gpsData.gpsInterval);
    myGNSS.setAutoNAVSAT(true);
  }
}

void generateDemoData() {
  // Generate dummy GPS data that simulates movement
  static unsigned long lastUpdate = 0;
  static double demoLat = 30.2672;  // Austin, TX area
  static double demoLon = -97.7431;
  static float demoAlt = 150.0;
  static float demoSpeed = 5.5;
  static float demoHeading = 90.0;
  
  unsigned long now = millis();
  if (now - lastUpdate < 1000) return; // Update demo data every second
  lastUpdate = now;
  
  // Simulate movement in a circle/pattern
  demoHeading += 2.0;
  if (demoHeading >= 360.0) demoHeading = 0.0;
  
  // Move position slightly to simulate travel
  double headingRad = demoHeading * PI / 180.0;
  demoLat += (demoSpeed * cos(headingRad)) * 0.000001;
  demoLon += (demoSpeed * sin(headingRad)) * 0.000001;
  demoAlt += (random(-10, 10) / 10.0); // Random altitude variation
  demoSpeed = 5.0 + (random(-20, 20) / 10.0); // Speed variation
  
  // Update gpsData with demo values (but don't affect real min/max)
  gpsData.lat = demoLat;
  gpsData.lon = demoLon;
  gpsData.alt = demoAlt;
  gpsData.altMSL = demoAlt;
  gpsData.speed = demoSpeed;
  gpsData.heading = demoHeading;
  
  // Set demo GPS parameters
  gpsData.satellites = 12;
  gpsData.satellitesVisible = 15;
  gpsData.hasFix = true;
  gpsData.fixType = 3;
  gpsData.fixStatus = "3D Fix (Demo)";
  gpsData.pdop = 1.5;
  gpsData.hdop = 0.9;
  gpsData.vdop = 1.2;
  gpsData.hAcc = 2.5;
  gpsData.vAcc = 3.0;
  
  // Set time to current system time
  unsigned long seconds = (now / 1000) % 86400;
  gpsData.hour = (seconds / 3600) % 24;
  gpsData.minute = (seconds % 3600) / 60;
  gpsData.second = seconds % 60;
  
  char timeBuf[12];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", gpsData.hour, gpsData.minute, gpsData.second);
  gpsData.timeStr = String(timeBuf);
  gpsData.localTimeStr = String(timeBuf);
  
  // Trigger LED if appropriate
  if (gpsData.ledMode == LED_BLINK_ON_GPS_READ || gpsData.ledMode == LED_BLINK_ON_FIX) {
    triggerLed();
  }
}

void pollGPS() {
  // If demo mode is active, generate fake data instead
  if (gpsData.demoMode) {
    generateDemoData();
    return;
  }
  
  if (!gpsData.isConnected) {
    static unsigned long lastRetry = 0;
    if (millis() - lastRetry > 5000) {
      lastRetry = millis();
      setupGPS();
    }
    return;
  }
  
  static unsigned long lastInterval = 0;
  if (lastInterval != gpsData.gpsInterval) {
    myGNSS.setMeasurementRate(gpsData.gpsInterval);
    lastInterval = gpsData.gpsInterval;
    Serial.print(F("GPS Rate updated to: "));
    Serial.println(lastInterval);
  }

  while(myGNSS.checkUblox()); 
  myGNSS.checkCallbacks();

  // If LED Mode is Blink on Read
  if (gpsData.ledMode == LED_BLINK_ON_GPS_READ) triggerLed();

  long lat = myGNSS.getLatitude();
  long lon = myGNSS.getLongitude();
  long alt = myGNSS.getAltitude();
  long altMSL = myGNSS.getAltitudeMSL();
  byte sats = myGNSS.getSIV();
  byte fixType = myGNSS.getFixType();
  bool gnssFixOk = myGNSS.getGnssFixOk();

  gpsData.satellites = sats;
  storage.updateSats(sats);

  if (myGNSS.getDOP()){
    gpsData.pdop = myGNSS.getPositionDOP() / 100.0;
    gpsData.hdop = myGNSS.getHorizontalDOP() / 100.0;
    gpsData.vdop = myGNSS.getVerticalDOP() / 100.0;
    storage.updateDOP(gpsData.pdop, gpsData.hdop, gpsData.vdop);
  }
  
  // Get Visible Satellites (from NAV SAT)
  if (myGNSS.getNAVSAT()) {
    gpsData.satellitesVisible = myGNSS.packetUBXNAVSAT->data.header.numSvs;
    storage.updateVisibleSats(gpsData.satellitesVisible);
  }

  gpsData.fixType = fixType;
  gpsData.hasFix = (fixType >= 2 && gnssFixOk);

  if (gpsData.hasFix && !gpsData.hadFirstFix) {
    gpsData.hadFirstFix = true;
    gpsData.firstFixTime = millis();
    gpsData.ttffSeconds = (gpsData.firstFixTime - gpsData.startTime) / 1000;
    
    webSerialLog("GPS fix acquired");
    webSerialLog("Satellites: " + String(sats));
    webSerialLog("TTFF: " + String(gpsData.ttffSeconds) + "s");
    
    if (!gpsData.configSaved) {
      Serial.println("Fix Obtained! Saving Almanac to Battery Backup...");
      webSerialLog("Saving GPS configuration to backup...");
      myGNSS.saveConfiguration(); 
      gpsData.configSaved = true;
    }
  }
  
  switch(fixType) {
      case 0: gpsData.fixStatus = "No Fix"; break;
      case 1: gpsData.fixStatus = "Dead Reckoning"; break;
      case 2: gpsData.fixStatus = gnssFixOk ? "2D Fix" : "2D Fix (Low Acc)"; break;
      case 3: gpsData.fixStatus = gnssFixOk ? "3D Fix" : "3D Fix (Low Acc)"; break;
      default: gpsData.fixStatus = "Unknown"; break;
  }

  if (gpsData.hasFix) {
    if (gpsData.ledMode == LED_BLINK_ON_FIX) triggerLed();

    gpsData.lat = lat / 10000000.0;
    gpsData.lon = lon / 10000000.0;
    gpsData.alt = alt / 1000.0;
    gpsData.altMSL = altMSL / 1000.0;
    
    storage.updateAlt(gpsData.alt);

    if (gpsData.ledMode == LED_BLINK_ON_MOVEMENT) {
      if (abs(gpsData.lat - gpsData.lastLat) > gpsData.movementThreshold || 
          abs(gpsData.lon - gpsData.lastLon) > gpsData.movementThreshold) {
        triggerLed();
      }
    }
    gpsData.lastLat = gpsData.lat;
    gpsData.lastLon = gpsData.lon;

    gpsData.speed = myGNSS.getGroundSpeed() / 1000.0;
    storage.updateSpeed(gpsData.speed);
    
    gpsData.heading = myGNSS.getHeading() / 100000.0; 
    
    gpsData.hAcc = myGNSS.getHorizontalAccEst() / 1000.0;
    gpsData.vAcc = myGNSS.getVerticalAccEst() / 1000.0;
    storage.updateAcc(gpsData.hAcc, gpsData.vAcc);
  }

  if (myGNSS.getTimeValid()) {
    gpsData.hour = myGNSS.getHour();
    gpsData.minute = myGNSS.getMinute();
    gpsData.second = myGNSS.getSecond();
    char timeBuf[12];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", gpsData.hour, gpsData.minute, gpsData.second);
    gpsData.timeStr = String(timeBuf);

    if (gpsData.hasFix) {
      float timezoneOffsetHours = gpsData.lon / 15.0;
      int timezoneOffsetRounded = (int)round(timezoneOffsetHours);
      gpsData.timezoneOffsetMinutes = timezoneOffsetRounded * 60;
      
      int localMinutes = gpsData.hour * 60 + gpsData.minute + gpsData.timezoneOffsetMinutes;
      while (localMinutes < 0) localMinutes += 1440;
      while (localMinutes >= 1440) localMinutes -= 1440;
      int localHour = localMinutes / 60;
      int localMin = localMinutes % 60;
      
      char localTimeBuf[12];
      snprintf(localTimeBuf, sizeof(localTimeBuf), "%02d:%02d:%02d", localHour, localMin, gpsData.second);
      gpsData.localTimeStr = String(localTimeBuf);
    } else {
      gpsData.localTimeStr = "--:--:--";
    }
  }
  
  if (myGNSS.getDateValid()) {
    gpsData.year = myGNSS.getYear();
    gpsData.month = myGNSS.getMonth();
    gpsData.day = myGNSS.getDay();
    char dateBuf[12];
    snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", gpsData.year, gpsData.month, gpsData.day);
    gpsData.dateStr = String(dateBuf);
  }
}
