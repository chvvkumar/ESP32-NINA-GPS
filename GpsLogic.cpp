#include <Arduino.h>
#include <Wire.h>
#include "GpsLogic.h"
#include "Config.h"
#include "Context.h"
#include "LedControl.h"

void setupGPS() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  if (myGNSS.begin(Wire, 0x42) == false) {
    Serial.println(F("u-blox GNSS not detected. Check wiring!"));
    gpsData.isConnected = false;
  } else {
    Serial.println(F("u-blox GNSS connected"));
    gpsData.isConnected = true;
    
    myGNSS.setI2COutput(COM_TYPE_UBX); 
    myGNSS.setMeasurementRate(gpsData.gpsInterval);
  }
}

void pollGPS() {
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

  myGNSS.checkUblox(); 
  myGNSS.checkCallbacks();

  // If LED Mode is Blink on Read
  if (gpsData.ledMode == LED_BLINK_ON_GPS_READ) triggerLed();

  long lat = myGNSS.getLatitude();
  long lon = myGNSS.getLongitude();
  long alt = myGNSS.getAltitude();
  byte sats = myGNSS.getSIV();
  byte fixType = myGNSS.getFixType();
  bool gnssFixOk = myGNSS.getGnssFixOk();

  gpsData.satellites = sats;
  gpsData.fixType = fixType;
  gpsData.hasFix = (fixType >= 2 && gnssFixOk);

  if (gpsData.hasFix && !gpsData.hadFirstFix) {
    gpsData.hadFirstFix = true;
    gpsData.firstFixTime = millis();
    gpsData.ttffSeconds = (gpsData.firstFixTime - gpsData.startTime) / 1000;
    
    if (!gpsData.configSaved) {
      Serial.println("Fix Obtained! Saving Almanac to Battery Backup...");
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

    if (gpsData.ledMode == LED_BLINK_ON_MOVEMENT) {
      if (abs(gpsData.lat - gpsData.lastLat) > gpsData.movementThreshold || 
          abs(gpsData.lon - gpsData.lastLon) > gpsData.movementThreshold) {
        triggerLed();
      }
    }
    gpsData.lastLat = gpsData.lat;
    gpsData.lastLon = gpsData.lon;

    gpsData.speed = myGNSS.getGroundSpeed() / 1000.0;
    gpsData.heading = myGNSS.getHeading() / 100000.0; 
    
    gpsData.hAcc = myGNSS.getHorizontalAccEst() / 1000.0;
    gpsData.vAcc = myGNSS.getVerticalAccEst() / 1000.0;
  }

  if (myGNSS.getDOP()){
    gpsData.pdop = myGNSS.getPositionDOP() / 100.0;
    gpsData.hdop = myGNSS.getHorizontalDOP() / 100.0;
    gpsData.vdop = myGNSS.getVerticalDOP() / 100.0;
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
