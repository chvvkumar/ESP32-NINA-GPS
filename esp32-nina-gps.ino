/**
 * GPS-ESP32 Standalone Firmware (SparkFun Lib v2.2.28 Compatible)
 * * REQUIRED LIBRARIES:
 * 1. SparkFun u-blox GNSS Arduino Library (v2.2.28)
 * 2. ESPAsyncWebServer
 * 3. AsyncTCP
 * 4. ArduinoJson
 * 5. Ticker
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h> // v2 Header
#include <ArduinoJson.h>
#include <vector>
#include <Ticker.h>

// ================= USER CONFIGURATION =================
const char* WIFI_SSID = "IoT";
const char* WIFI_PASS = "kkkkkkkk";

const char* AP_SSID = "GPS-ESP32-AP";
const char* AP_PASS = "kkkkkkkk";

#define LED_PIN 2            
#define LED_BLINK_DURATION_MS 100 
#define I2C_SDA 21
#define I2C_SCL 22
#define TCP_PORT 2947

// ================= GLOBALS =================
SFE_UBLOX_GNSS myGNSS;
AsyncWebServer webServer(80);
AsyncServer tcpServer(TCP_PORT);
Ticker ledTicker; 

// LED Mode Enumeration
enum LedMode {
  LED_OFF = 0,
  LED_ON = 1,
  LED_BLINK_ON_GPS_READ = 2,
  LED_BLINK_ON_FIX = 3,
  LED_BLINK_ON_MOVEMENT = 4
};

// ================= DATA STRUCTURE =================
struct GPSData {
  // CHANGED DEFAULT: Blink on Read instead of Blink on Fix
  LedMode ledMode = LED_BLINK_ON_GPS_READ;
  
  bool isConnected = false;
  bool hasFix = false;
  bool hadFirstFix = false;
  String fixStatus = "Initializing";
  int satellites = 0;
  unsigned long startTime = 0;
  unsigned long firstFixTime = 0;
  int ttffSeconds = 0;
  
  // CHANGED DEFAULT: 5000ms (5 seconds)
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
} gpsData;

// ================= LED CALLBACK =================
void turnLedOff() {
  digitalWrite(LED_PIN, LOW);
  gpsData.ledState = false;
}

// ================= HELPER FUNCTIONS =================
String getChecksum(String content) {
  int xorResult = 0;
  for (int i = 0; i < content.length(); i++) {
    xorResult ^= content.charAt(i);
  }
  char hex[3];
  sprintf(hex, "%02X", xorResult);
  return String(hex);
}

// Handles 3-digit Longitude (DDDMM.mm) vs 2-digit Latitude (DDMM.mm)
String toNMEA(double deg, bool isLon) {
  int d = (int)abs(deg);
  double m = (abs(deg) - d) * 60.0;
  char buf[20];
  if (isLon) {
    sprintf(buf, "%03d%07.4f", d, m); 
  } else {
    sprintf(buf, "%02d%07.4f", d, m); 
  }
  return String(buf);
}

// ================= LED CONTROL =================
void triggerLed() {
  if (gpsData.ledMode == LED_OFF || gpsData.ledMode == LED_ON) return;
  if (gpsData.ledState) return;
  
  digitalWrite(LED_PIN, HIGH);
  gpsData.ledState = true;
  ledTicker.once_ms(LED_BLINK_DURATION_MS, turnLedOff);
}

// ================= GPS LOGIC =================
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
    myGNSS.setNavigationFrequency(1);
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

  myGNSS.checkUblox(); 
  myGNSS.checkCallbacks();

  // If LED Mode is Blink on Read (Default now), trigger here
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
      case 2: gpsData.fixStatus = "2D Fix"; break;
      case 3: gpsData.fixStatus = "3D Fix"; break;
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

  if (myGNSS.getDOP()) {
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

// ================= WEB SERVER =================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>GPS Node Status</title>
  <style>
    :root {
      --md-sys-color-background: #1a1a1a;
      --md-sys-color-surface: #2a2a2a;
      --md-sys-color-secondary: #90caf9;
    }
    * {
      margin: 0;
      padding: 0;
      box-sizing: border-box;
    }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
      background: var(--md-sys-color-background);
      color: #fff;
      padding: 20px;
      margin: 0;
    }
    .container {
      max-width: 900px;
      margin: 0 auto;
    }
    h1 {
      text-align: center;
      margin-bottom: 20px;
      font-size: 2em;
    }
    h3 {
      margin-bottom: 12px;
    }
    .md-card {
      background: var(--md-sys-color-surface);
      border-radius: 12px;
      box-shadow: 0 2px 8px rgba(0,0,0,0.3);
      padding: 20px;
      margin-bottom: 16px;
    }
    .md-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
      gap: 12px;
      margin-top: 12px;
    }
    .md-item {
      background: #232323;
      border-radius: 8px;
      padding: 12px;
      text-align: center;
      box-shadow: 0 1px 4px rgba(0,0,0,0.2);
    }
    .md-label {
      font-size: 0.9em;
      color: #bbb;
      margin-bottom: 4px;
      display: block;
    }
    .md-value {
      font-size: 1.3em;
      font-weight: 500;
      color: var(--md-sys-color-secondary);
    }
    .md-select {
      width: 100%;
      background: #232323;
      color: #fff;
      border: none;
      border-radius: 8px;
      padding: 10px;
      font-size: 1em;
      margin-top: 8px;
    }
    label {
      display: block;
      margin-bottom: 4px;
      color: #bbb;
    }
    .copy-btn {
      background: #404040;
      color: #90caf9;
      border: none;
      border-radius: 6px;
      padding: 6px 12px;
      font-size: 0.85em;
      cursor: pointer;
      margin-top: 8px;
      transition: background 0.2s;
    }
    .copy-btn:hover {
      background: #505050;
    }
    .copy-btn:active {
      background: #60606;
    }
    .copy-btn.copied {
      background: #2e7d32;
      color: #fff;
    }
    .md-item-with-copy {
      display: flex;
      flex-direction: column;
      align-items: center;
    }
    @media (max-width: 600px) {
      .container { 
        max-width: 98vw;
        padding: 4vw; 
      }
      .md-grid { 
        grid-template-columns: 1fr;
      }
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>GPS Node Status</h1>
    
    <div class="md-card">
      <h3>Configuration</h3>
      <label for="ledMode">LED Mode:</label>
      <select class="md-select" id="ledMode" onchange="setLedMode(this.value)">
        <option value="0">Off</option>
        <option value="1">On</option>
        <option value="2">Blink on Read</option>
        <option value="3">Blink on Fix</option>
        <option value="4">Blink on Movement</option>
      </select>
      
      <label for="updateInterval" style="margin-top: 16px;">GPS Update Interval:</label>
      <select class="md-select" id="updateInterval" onchange="changeUpdateInterval(this.value)">
        <option value="1000">1 second</option>
        <option value="2000">2 seconds</option>
        <option value="5000">5 seconds</option>
        <option value="10000">10 seconds</option>
        <option value="30000">30 seconds</option>
        <option value="0">Paused</option>
      </select>
    </div>
    
    <div class="md-card">
      <h3>Fix Info</h3>
      <div class="md-grid">
        <div class="md-item">
          <span class="md-label">Status</span>
          <span class="md-value" id="fixStatus">--</span>
        </div>
        <div class="md-item">
          <span class="md-label">Visible Satellites</span>
          <span class="md-value" id="sats">0</span>
        </div>
        <div class="md-item">
          <span class="md-label">TTFF</span>
          <span class="md-value" id="ttff">--</span>
        </div>
        <div class="md-item">
          <span class="md-label">Time (UTC)</span>
          <span class="md-value" id="time">--</span>
        </div>
        <div class="md-item">
          <span class="md-label">Local Time</span>
          <span class="md-value" id="localTime">--:--:--</span>
        </div>
      </div>
    </div>
    
    <div class="md-card">
      <h3>Position</h3>
      <div class="md-grid">
        <div class="md-item md-item-with-copy">
          <span class="md-label">Latitude</span>
          <span class="md-value" id="lat">0.0000000</span>
          <button class="copy-btn" onclick="copyValue('lat', this)">Copy</button>
        </div>
        <div class="md-item md-item-with-copy">
          <span class="md-label">Longitude</span>
          <span class="md-value" id="lon">0.0000000</span>
          <button class="copy-btn" onclick="copyValue('lon', this)">Copy</button>
        </div>
        <div class="md-item md-item-with-copy">
          <span class="md-label">Altitude</span>
          <span class="md-value" id="alt">0.0 m</span>
          <button class="copy-btn" onclick="copyValue('alt', this)">Copy</button>
        </div>
        <div class="md-item">
          <span class="md-label">Speed</span>
          <span class="md-value" id="speed">0.0 m/s</span>
        </div>
      </div>
    </div>
  </div>
  
  <script>
    let ledModeActive = false;
    let updateIntervalId = null;
    // CHANGED JS DEFAULT: 5000ms
    let currentUpdateInterval = 5000;
    
    const ledModeSelect = document.getElementById('ledMode');
    const updateIntervalSelect = document.getElementById('updateInterval');
    
    // Ensure the dropdown matches the default 5000ms on load
    updateIntervalSelect.value = "5000";
    
    ledModeSelect.addEventListener('focus', () => { ledModeActive = true; });
    ledModeSelect.addEventListener('blur', () => { ledModeActive = false; });

    function updateData() {
      fetch('/api/status')
        .then(response => response.json())
        .then(data => {
          document.getElementById('fixStatus').textContent = data.fixStatus;
          document.getElementById('sats').textContent = data.sats;
          document.getElementById('ttff').textContent = data.ttff >= 0 ? data.ttff + ' s' : '--';
          document.getElementById('time').textContent = data.time;
          document.getElementById('localTime').textContent = data.localTime;
          document.getElementById('lat').textContent = Number(data.lat).toFixed(7);
          document.getElementById('lon').textContent = Number(data.lon).toFixed(7);
          document.getElementById('alt').textContent = Number(data.alt).toFixed(2) + ' m';
          document.getElementById('speed').textContent = Number(data.speed).toFixed(2) + " m/s";
          
          if (!ledModeActive && ledModeSelect.value != data.ledMode) {
            ledModeSelect.value = data.ledMode;
          }
        })
        .catch(err => console.error('Update failed:', err));
    }
    
    function setLedMode(mode) { 
      fetch('/api/set_led?mode=' + mode)
        .catch(err => console.error('Set LED failed:', err));
    }
    
    function changeUpdateInterval(interval) {
      const intervalMs = parseInt(interval);
      currentUpdateInterval = intervalMs;
      fetch('/api/set_interval?interval=' + intervalMs)
        .catch(err => console.error('Set interval failed:', err));
      if (updateIntervalId) {
        clearInterval(updateIntervalId);
        updateIntervalId = null;
      }
      if (intervalMs > 0) {
        updateIntervalId = setInterval(updateData, intervalMs);
        updateData(); 
      }
    }
    
    function copyValue(elementId, button) {
      const element = document.getElementById(elementId);
      const fullText = element.textContent;
      const numericValue = fullText.replace(/\s*[a-zA-Z\/]+\s*$/, '').trim();

      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(numericValue).then(() => {
          button.textContent = 'Copied!';
          button.classList.add('copied');
          setTimeout(() => {
            button.textContent = 'Copy';
            button.classList.remove('copied');
          }, 2000);
        }).catch(err => {
          copyFallback(numericValue, button);
        });
      } else {
        copyFallback(numericValue, button);
      }
    }
    
    function copyFallback(text, button) {
      const textarea = document.createElement('textarea');
      textarea.value = text;
      textarea.style.position = 'fixed';
      textarea.style.opacity = '0';
      document.body.appendChild(textarea);
      textarea.select();
      
      try {
        const successful = document.execCommand('copy');
        if (successful) {
          button.textContent = 'Copied!';
          button.classList.add('copied');
          setTimeout(() => {
            button.textContent = 'Copy';
            button.classList.remove('copied');
          }, 2000);
        } else {
          button.textContent = 'Failed';
          setTimeout(() => {
            button.textContent = 'Copy';
          }, 2000);
        }
      } catch (err) {
        button.textContent = 'Failed';
        setTimeout(() => {
          button.textContent = 'Copy';
        }, 2000);
      } finally {
        document.body.removeChild(textarea);
      }
    }
    
    updateIntervalId = setInterval(updateData, currentUpdateInterval);
    updateData();
  </script>
</body>
</html>
)rawliteral";

void setupWeb() {
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", index_html);
  });

  webServer.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    doc["connected"] = gpsData.isConnected;
    doc["fixStatus"] = gpsData.fixStatus;
    doc["sats"] = gpsData.satellites;
    doc["ttff"] = gpsData.hadFirstFix ? gpsData.ttffSeconds : -1;
    doc["time"] = gpsData.timeStr;
    doc["localTime"] = gpsData.localTimeStr;
    doc["lat"] = gpsData.lat;
    doc["lon"] = gpsData.lon;
    doc["alt"] = gpsData.alt;
    doc["speed"] = gpsData.speed;
    doc["ledMode"] = (int)gpsData.ledMode;
    doc["ledBlinkMs"] = LED_BLINK_DURATION_MS;
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  webServer.on("/api/set_led", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("mode")) {
      int mode = request->getParam("mode")->value().toInt();
      gpsData.ledMode = static_cast<LedMode>(mode);
      ledTicker.detach();
      gpsData.ledState = false;
      if (gpsData.ledMode == LED_OFF) {
        digitalWrite(LED_PIN, LOW);
      } else if (gpsData.ledMode == LED_ON) {
        digitalWrite(LED_PIN, HIGH);
      } else {
        digitalWrite(LED_PIN, LOW);
      }
    }
    request->send(200, "text/plain", "OK");
  });

  webServer.on("/api/set_interval", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("interval")) {
      unsigned long interval = request->getParam("interval")->value().toInt();
      gpsData.gpsInterval = interval;
    }
    request->send(200, "text/plain", "OK");
  });
  
  webServer.begin();
}

// ================= TCP SERVER WITH NINA SUPPORT =================
struct ClientContext {
  AsyncClient* client;
  bool isGpsd = false; 
};
std::vector<ClientContext> clients;

static void handleClientData(void* arg, AsyncClient* client, void* data, size_t len) {
  String cmd = String((char*)data).substring(0, len);
  
  if (cmd.indexOf("?WATCH") != -1) {
    for (auto& ctx : clients) {
      if (ctx.client == client) {
        ctx.isGpsd = true;
        String ack = "{\"class\":\"VERSION\",\"release\":\"3.23\",\"rev\":\"ESP32\",\"proto_major\":3,\"proto_minor\":14}\n";
        ack += "{\"class\":\"DEVICES\",\"devices\":[{\"class\":\"DEVICE\",\"path\":\"/dev/i2c\",\"driver\":\"u-blox\",\"activated\":\"" + gpsData.dateStr + "T" + gpsData.timeStr + "Z\"}]}\n";
        ack += "{\"class\":\"WATCH\",\"enable\":true,\"json\":true}\n";
        client->write(ack.c_str());
        break;
      }
    }
  }
}

static void handleNewClient(void* arg, AsyncClient* client) {
  ClientContext ctx;
  ctx.client = client;
  ctx.isGpsd = false; 
  clients.push_back(ctx);

  client->onDisconnect([](void* arg, AsyncClient* c) {
    for (auto it = clients.begin(); it != clients.end(); ++it) {
      if (it->client == c) {
        clients.erase(it);
        break;
      }
    }
  }, NULL);
  
  client->onData(&handleClientData, NULL);
}

void setupTCP() {
  tcpServer.onClient(&handleNewClient, NULL);
  tcpServer.begin();
}

void broadcastData() {
  if (clients.empty()) return;

  // --- PREPARE NMEA ---
  String rmc = "GPRMC,";
  char timeBuf[15];
  sprintf(timeBuf, "%02d%02d%02d.00,", gpsData.hour, gpsData.minute, gpsData.second);
  rmc += timeBuf;
  rmc += (gpsData.hasFix ? "A," : "V,");
  rmc += toNMEA(gpsData.lat, false) + (gpsData.lat >= 0 ? ",N," : ",S,");
  rmc += toNMEA(gpsData.lon, true) + (gpsData.lon >= 0 ? ",E," : ",W,");
  rmc += String(gpsData.speed * 1.94384) + ",";
  rmc += String(gpsData.heading) + ",";
  char dateBuf[10];
  sprintf(dateBuf, "%02d%02d%02d,", gpsData.day, gpsData.month, gpsData.year % 100);
  rmc += dateBuf;
  rmc += ",,";
  String rmcFull = "$" + rmc + "*" + getChecksum(rmc) + "\r\n";

  String gga = "GPGGA,";
  gga += String(timeBuf);
  gga += toNMEA(gpsData.lat, false) + (gpsData.lat >= 0 ? ",N," : ",S,");
  gga += toNMEA(gpsData.lon, true) + (gpsData.lon >= 0 ? ",E," : ",W,");
  gga += (gpsData.hasFix ? "1," : "0,");
  gga += String(gpsData.satellites) + ",";
  gga += String(gpsData.hdop) + ",";
  gga += String(gpsData.alt) + ",M,";
  gga += "0.0,M,,";
  String ggaFull = "$" + gga + "*" + getChecksum(gga) + "\r\n";

  // Mode Logic: Use actual fix type from u-blox
  // u-blox 3 = 3D Fix. GPSD 3 = 3D Fix. 
  int mode = 1; // Default No Fix
  if (gpsData.fixType == 2) mode = 2; // 2D
  if (gpsData.fixType == 3) mode = 3; // 3D
  
  String gsa = "GPGSA,A," + String(mode) + ",,,,,,,,,,,,,"; 
  gsa += String(gpsData.pdop) + "," + String(gpsData.hdop) + "," + String(gpsData.vdop);
  String gsaFull = "$" + gsa + "*" + getChecksum(gsa) + "\r\n";

  // --- PREPARE GPSD JSON ---
  String tpv = "";
  if (gpsData.hasFix) {
    tpv = "{\"class\":\"TPV\",\"device\":\"/dev/i2c\"";
    tpv += ",\"status\":1"; 
    tpv += ",\"mode\":" + String(mode);
    tpv += ",\"time\":\"" + gpsData.dateStr + "T" + gpsData.timeStr + "Z\"";
    tpv += ",\"lat\":" + String(gpsData.lat, 7);
    tpv += ",\"lon\":" + String(gpsData.lon, 7);
    
    // SEND ALTITUDE IN ALL COMMON FORMATS
    tpv += ",\"alt\":" + String(gpsData.alt, 3);
    tpv += ",\"altHAE\":" + String(gpsData.alt, 3);
    tpv += ",\"altMSL\":" + String(gpsData.alt, 3);
    
    tpv += ",\"speed\":" + String(gpsData.speed, 3);
    tpv += ",\"track\":" + String(gpsData.heading, 2);
    // Accuracy fields 
    tpv += ",\"epx\":" + String(gpsData.hAcc, 2);
    tpv += ",\"epy\":" + String(gpsData.hAcc, 2);
    tpv += ",\"epv\":" + String(gpsData.vAcc, 2); 
    tpv += "}\n";
  }

  for (auto& ctx : clients) {
    if (ctx.client->connected() && ctx.client->canSend()) {
      if (ctx.isGpsd && gpsData.hasFix) {
        ctx.client->write(tpv.c_str());
      } else {
        ctx.client->write(rmcFull.c_str());
        ctx.client->write(ggaFull.c_str());
        ctx.client->write(gsaFull.c_str());
      }
    }
  }
}

// ================= MAIN SETUP =================
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  gpsData.startTime = millis();

  setupGPS();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
  
  setupWeb();
  setupTCP();
}

void loop() {
  if (gpsData.gpsInterval > 0 && (millis() - gpsData.lastGPSPoll >= gpsData.gpsInterval)) {
    gpsData.lastGPSPoll = millis();
    pollGPS();
    broadcastData(); 
  }
  
  static bool connectedPrinted = false;
  if (WiFi.status() == WL_CONNECTED && !connectedPrinted) {
    Serial.print("Station IP: ");
    Serial.println(WiFi.localIP());
    connectedPrinted = true;
  }
  
  delay(1);
}
