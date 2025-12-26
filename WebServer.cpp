#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
#include <ElegantOTA.h>
#include <ArduinoJson.h>
#include "WebServer.h"
#include "Config.h"
#include "Context.h"
#include "LedControl.h" 

AsyncWebServer webServer(WEB_PORT);

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 GPS Dashboard</title>
  <style>
    :root {
      --bg-color: #121212;
      --card-bg: #1e1e1e;
      --accent: #00e5ff;
      --text-main: #ffffff;
      --text-muted: #aaaaaa;
      --success: #00c853;
      --warning: #ffd600;
      --danger: #d50000;
    }
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body { font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background: var(--bg-color); color: var(--text-main); padding: 1rem; }
    
    /* Layout */
    .container { max-width: 1200px; margin: 0 auto; }
    .header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 1.5rem; padding: 0 0.5rem; }
    .status-badge { display: flex; align-items: center; gap: 8px; background: rgba(255,255,255,0.1); padding: 6px 12px; border-radius: 20px; font-size: 0.9rem; }
    .status-dot { width: 10px; height: 10px; border-radius: 50%; background: var(--text-muted); box-shadow: 0 0 8px currentColor; transition: all 0.3s; }
    
    /* Grid System */
    .dashboard { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 1rem; }
    .card { background: var(--card-bg); border-radius: 16px; padding: 1.5rem; box-shadow: 0 4px 6px rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.05); }
    .card-title { font-size: 0.85rem; text-transform: uppercase; letter-spacing: 1px; color: var(--text-muted); margin-bottom: 1rem; display: flex; justify-content: space-between; }
    
    /* Typography */
    .big-value { font-size: 2rem; font-weight: 300; letter-spacing: -0.5px; }
    .unit { font-size: 1rem; color: var(--text-muted); margin-left: 4px; }
    .mono { font-family: 'Courier New', monospace; }
    
    /* Specific Components */
    .coord-grid { display: grid; gap: 1rem; }
    .coord-item { position: relative; }
    .coord-label { display: block; font-size: 0.75rem; color: var(--text-muted); margin-bottom: 4px; }
    .copy-btn { position: absolute; right: 0; top: 0; background: transparent; border: 1px solid rgba(255,255,255,0.2); color: var(--text-muted); padding: 2px 8px; border-radius: 4px; cursor: pointer; font-size: 0.7rem; transition: all 0.2s; }
    .copy-btn:hover { border-color: var(--accent); color: var(--accent); }
    
    /* Compass & Speed */
    .nav-row { display: flex; align-items: center; justify-content: space-around; }
    .compass-container { position: relative; width: 120px; height: 120px; border: 2px solid rgba(255,255,255,0.1); border-radius: 50%; display: flex; justify-content: center; align-items: center; }
    .compass-mark { position: absolute; font-size: 0.8rem; font-weight: bold; color: var(--text-muted); }
    .mark-n { top: 5px; color: var(--accent); }
    .mark-s { bottom: 5px; }
    .mark-e { right: 8px; }
    .mark-w { left: 8px; }
    .needle { width: 4px; height: 100%; position: absolute; transition: transform 0.5s cubic-bezier(0.4, 0, 0.2, 1); }
    .needle::before { content: ''; position: absolute; top: 15px; left: 0; border-left: 6px solid transparent; border-right: 6px solid transparent; border-bottom: 30px solid var(--accent); transform: translateX(-4px); }
    .heading-text { position: absolute; font-size: 1.2rem; font-weight: bold; background: var(--card-bg); padding: 2px 6px; border-radius: 4px; }
    
    /* Stats Grid */
    .stats-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; text-align: center; }
    .stat-box { background: rgba(255,255,255,0.03); padding: 10px; border-radius: 8px; }
    .stat-val { font-size: 1.1rem; font-weight: 500; display: block; }
    .stat-lbl { font-size: 0.7rem; color: var(--text-muted); }

    /* Forms */
    input, select { width: 100%; background: rgba(0,0,0,0.2); border: 1px solid rgba(255,255,255,0.1); color: white; padding: 10px; border-radius: 8px; margin-top: 5px; }
    .btn { width: 100%; padding: 12px; border: none; border-radius: 8px; font-weight: 600; cursor: pointer; margin-top: 10px; transition: opacity 0.2s; }
    .btn-primary { background: var(--accent); color: #000; }
    .btn-outline { background: transparent; border: 1px solid rgba(255,255,255,0.2); color: white; }
    .btn:hover { opacity: 0.9; }

    /* WiFi List */
    .wifi-list { margin-top: 10px; max-height: 150px; overflow-y: auto; }
    .wifi-item { display: flex; justify-content: space-between; padding: 8px; border-bottom: 1px solid rgba(255,255,255,0.05); cursor: pointer; }
    .wifi-item:hover { background: rgba(255,255,255,0.05); }

    /* Info Rows */
    .info-row { display: flex; justify-content: space-between; align-items: center; padding: 6px 0; border-bottom: 1px solid rgba(255,255,255,0.05); }
    .info-label { font-size: 0.75rem; color: var(--text-muted); text-transform: uppercase; letter-spacing: 0.5px; }
    .info-val { font-family: 'Courier New', monospace; color: var(--accent); font-size: 0.9rem; }

    @media (max-width: 600px) {
      .big-value { font-size: 1.6rem; }
      .dashboard { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h2>ESP32 GPS Dashboard</h2>
    </div>

    <div class="dashboard">
      <div class="card">
        <div class="card-title">Position <span id="satsBadge">0 Sats</span></div>
        <div class="coord-grid">
          <div class="coord-item">
            <span class="coord-label">LATITUDE</span>
            <div class="big-value mono" id="lat">0.0000000</div>
            <button class="copy-btn" onclick="copy('lat', this)">COPY</button>
          </div>
          <div class="coord-item">
            <span class="coord-label">LONGITUDE</span>
            <div class="big-value mono" id="lon">0.0000000</div>
            <button class="copy-btn" onclick="copy('lon', this)">COPY</button>
          </div>
          <div class="coord-item">
            <span class="coord-label">ALTITUDE</span>
            <div class="big-value mono" id="alt">0.0 m</div>
            <button class="copy-btn" onclick="copy('alt', this)">COPY</button>
          </div>
          
          <div style="border-top: 1px solid rgba(255,255,255,0.1); padding-top: 1rem; margin-top: 0.5rem; display: flex; justify-content: space-between;">
             <div><span class="coord-label">LOCAL TIME</span><span class="stat-val" id="localTime">--:--:--</span></div>
             <div style="text-align: right;"><span class="coord-label">UTC TIME</span><span class="stat-val" id="time">--:--:--</span></div>
          </div>
        </div>
      </div>

      <div class="card">
        <div class="card-title">Navigation</div>
        <div class="nav-row">
          <div class="compass-container">
            <div class="compass-mark mark-n">N</div>
            <div class="compass-mark mark-e">E</div>
            <div class="compass-mark mark-s">S</div>
            <div class="compass-mark mark-w">W</div>
            <div class="needle" id="compassNeedle"></div>
            <div class="heading-text" id="heading">0&deg;</div>
          </div>
          <div style="text-align: center;">
            <div class="coord-label">SPEED</div>
            <div class="big-value" id="speed">0.0</div>
            <div class="unit">m/s</div>
          </div>
        </div>
        <div class="stats-grid" style="margin-top: 1.5rem;">
           <div class="stat-box">
             <span class="stat-val" id="hAcc">0 m</span>
             <span class="stat-lbl">H. Acc</span>
           </div>
           <div class="stat-box">
             <span class="stat-val" id="vAcc">0 m</span>
             <span class="stat-lbl">V. Acc</span>
           </div>
           <div class="stat-box">
             <span class="stat-val" id="ttff">--</span>
             <span class="stat-lbl">TTFF</span>
           </div>
        </div>
      </div>

      <div class="card">
        <div class="card-title" style="display: flex; justify-content: space-between; align-items: center;">
            Signal Quality
            <div class="status-badge">
                <div id="statusDot" class="status-dot"></div>
                <span id="fixStatus">Initializing...</span>
            </div>
        </div>
        <div class="stats-grid">
          <div class="stat-box">
            <span class="stat-val" id="pdop">--</span>
            <span class="stat-lbl">PDOP</span>
          </div>
          <div class="stat-box">
            <span class="stat-val" id="hdop">--</span>
            <span class="stat-lbl">HDOP</span>
          </div>
          <div class="stat-box">
            <span class="stat-val" id="vdop">--</span>
            <span class="stat-lbl">VDOP</span>
          </div>
        </div>
        
        <div style="margin-top: 1.5rem;">
           <div class="card-title">Configuration</div>
           <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px;">
             <div>
                <label class="coord-label">LED MODE</label>
                <select id="ledMode" onchange="setLed(this.value)">
                  <option value="0">Off</option> <option value="1">On</option>
                  <option value="2">Blink (Read)</option> <option value="3">Blink (Fix)</option>
                  <option value="4">Blink (Move)</option>
                </select>
             </div>
             <div>
                <label class="coord-label">UPDATE RATE</label>
                <select id="rate" onchange="setRate(this.value)">
                  <option value="1000">1s</option> <option value="5000">5s</option>
                  <option value="10000">10s</option> <option value="30000">30s</option>
                </select>
             </div>
           </div>
           
           <div style="margin-top: 15px;">
              <div class="info-row">
                  <span class="info-label">STATION IP</span>
                  <span class="info-val" id="stIp">--</span>
              </div>
              <div class="info-row">
                  <span class="info-label">AP IP</span>
                  <span class="info-val" id="apIp">--</span>
              </div>
              <div class="info-row">
                  <span class="info-label">NINA TCP PORT</span>
                  <span class="info-val" id="tcpPort">--</span>
              </div>
           </div>
        </div>
      </div>

      <div class="card">
        <div class="card-title">WiFi Setup</div>
        <button class="btn btn-outline" id="scanBtn" onclick="scanWifi()">Scan Networks</button>
        <div id="wifiList" class="wifi-list"></div>
        <div style="margin-top: 1rem;">
          <input type="text" id="ssid" placeholder="Network SSID">
          <input type="password" id="pass" placeholder="Password">
          <button class="btn btn-primary" id="saveBtn" onclick="saveWifi()">Connect & Save</button>
        </div>
        <div style="margin-top: 1rem; border-top: 1px solid rgba(255,255,255,0.1); padding-top: 1rem;">
            <button class="btn btn-outline" onclick="location.href='/update'">OTA Firmware Update</button>
        </div>
      </div>
    </div>
  </div>

  <script>
    let intervalId = null;
    let currentInterval = 5000;
    let isEditing = false;
    
    // UI Updates
    function updateData() {
      fetch('/api/status').then(r => r.json()).then(d => {
        // IDs
        const ids = ['lat','lon','alt','speed','hAcc','vAcc','pdop','hdop','vdop','time','localTime','fixStatus'];
        ids.forEach(id => {
          const el = document.getElementById(id);
          if(el) {
             let val = d[id];
             if(typeof val === 'number') {
                if(id.includes('dop')) val = val.toFixed(2);
                else if(id.includes('lat') || id.includes('lon')) val = val.toFixed(7);
                else if(id.includes('Acc')) val = val.toFixed(1) + ' m';
                else if(id === 'alt') val = val.toFixed(2) + ' m';
                else val = val.toFixed(2);
             }
             el.textContent = val;
          }
        });
        
        // Custom Logic
        document.getElementById('ttff').textContent = d.ttff >= 0 ? d.ttff + 's' : '--';
        document.getElementById('satsBadge').textContent = d.sats + ' Sats';
        document.getElementById('stIp').textContent = d.stationIp;
        document.getElementById('apIp').textContent = d.apIp;
        document.getElementById('tcpPort').textContent = d.tcpPort;
        
        // Compass
        document.getElementById('heading').innerHTML = Math.round(d.heading) + '&deg;';
        document.getElementById('compassNeedle').style.transform = `rotate(${d.heading}deg)`;
        
        // Status Dot
        const dot = document.getElementById('statusDot');
        if(d.fixStatus.includes("3D") || d.fixStatus.includes("2D")) {
            dot.style.background = "var(--success)";
            dot.style.boxShadow = "0 0 10px var(--success)";
        } else if(d.connected) {
            dot.style.background = "var(--warning)";
            dot.style.boxShadow = "0 0 10px var(--warning)";
        } else {
            dot.style.background = "var(--danger)";
        }

        // Inputs
        if(!isEditing) {
            if(document.activeElement !== document.getElementById('ledMode')) 
               document.getElementById('ledMode').value = d.ledMode;
            
            const rateEl = document.getElementById('rate');
            if(document.activeElement !== rateEl && d.rate) {
               rateEl.value = d.rate;
               if(d.rate != currentInterval) {
                   clearInterval(intervalId);
                   currentInterval = d.rate;
                   intervalId = setInterval(updateData, currentInterval);
               }
            }
        }
      }).catch(e => console.log(e));
    }

    // Actions
    function setLed(v) { fetch('/api/set_led?mode='+v); }
    function setRate(v) { 
        fetch('/api/set_interval?interval='+v);
        clearInterval(intervalId);
        currentInterval = parseInt(v);
        if(v > 0) intervalId = setInterval(updateData, v);
    }
    
    // Copy Function with Fallback
    function copy(id, btn) {
        const val = document.getElementById(id).textContent;
        // Try Clipboard API first
        if (navigator.clipboard && navigator.clipboard.writeText) {
            navigator.clipboard.writeText(val).then(() => showCopyFeedback(btn))
            .catch(err => copyFallback(val, btn));
        } else {
            copyFallback(val, btn);
        }
    }

    function copyFallback(text, btn) {
        const textArea = document.createElement("textarea");
        textArea.value = text;
        textArea.style.position = "fixed";  // Avoid scrolling to bottom
        textArea.style.opacity = "0";
        document.body.appendChild(textArea);
        textArea.focus();
        textArea.select();
        try {
            const successful = document.execCommand('copy');
            if(successful) showCopyFeedback(btn);
            else console.error('Fallback copy failed');
        } catch (err) {
            console.error('Fallback copy failed', err);
        }
        document.body.removeChild(textArea);
    }

    function showCopyFeedback(btn) {
        const orig = btn.textContent;
        btn.textContent = "COPIED";
        btn.style.borderColor = "var(--success)";
        btn.style.color = "var(--success)";
        setTimeout(() => {
            btn.textContent = orig;
            btn.style.borderColor = "";
            btn.style.color = "";
        }, 1500);
    }

    function scanWifi() {
        const list = document.getElementById('wifiList');
        const btn = document.getElementById('scanBtn');
        btn.textContent = "Scanning..."; btn.disabled = true;
        list.innerHTML = '';
        fetch('/api/scan').then(r => r.json()).then(nets => {
            btn.textContent = "Scan Networks"; btn.disabled = false;
            nets.forEach(n => {
                const div = document.createElement('div');
                div.className = 'wifi-item';
                div.innerHTML = `<span>${n.ssid}</span><span style="color:var(--text-muted)">${n.rssi}dB</span>`;
                div.onclick = () => {
                    document.getElementById('ssid').value = n.ssid;
                    document.getElementById('pass').focus();
                };
                list.appendChild(div);
            });
        });
    }

    function saveWifi() {
        const s = document.getElementById('ssid').value;
        const p = document.getElementById('pass').value;
        if(!s) return alert("SSID required");
        const btn = document.getElementById('saveBtn');
        btn.textContent = "Saving...";
        fetch(`/api/save_wifi?ssid=${encodeURIComponent(s)}&pass=${encodeURIComponent(p)}`)
        .then(r => { alert("Saved. Rebooting..."); location.reload(); });
    }

    // Init
    document.getElementById('ledMode').addEventListener('focus', () => isEditing = true);
    document.getElementById('ledMode').addEventListener('blur', () => isEditing = false);
    intervalId = setInterval(updateData, 5000);
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
    doc["stationIp"] = WiFi.localIP().toString();
    doc["apIp"] = WiFi.softAPIP().toString();
    doc["tcpPort"] = TCP_PORT;
    doc["connected"] = gpsData.isConnected;
    doc["fixStatus"] = gpsData.fixStatus;
    doc["sats"] = gpsData.satellites;
    doc["ttff"] = gpsData.hadFirstFix ? gpsData.ttffSeconds : -1;
    doc["pdop"] = gpsData.pdop;
    doc["hdop"] = gpsData.hdop;
    doc["vdop"] = gpsData.vdop;
    doc["time"] = gpsData.timeStr;
    doc["localTime"] = gpsData.localTimeStr;
    doc["lat"] = gpsData.lat;
    doc["lon"] = gpsData.lon;
    doc["alt"] = gpsData.alt;
    doc["speed"] = gpsData.speed;
    doc["heading"] = gpsData.heading;
    doc["hAcc"] = gpsData.hAcc;
    doc["vAcc"] = gpsData.vAcc;
    doc["ledMode"] = (int)gpsData.ledMode;
    doc["ledBlinkMs"] = LED_BLINK_DURATION_MS;
    doc["rate"] = gpsData.gpsInterval;
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  webServer.on("/api/set_led", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("mode")) {
      int mode = request->getParam("mode")->value().toInt();
      gpsData.ledMode = static_cast<LedMode>(mode);
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

  webServer.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *request){
    int n = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray array = doc.to<JsonArray>();
    for (int i = 0; i < n; ++i) {
        JsonObject obj = array.add<JsonObject>();
        obj["ssid"] = WiFi.SSID(i);
        obj["rssi"] = WiFi.RSSI(i);
        obj["enc"] = (int)WiFi.encryptionType(i);
    }
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
    WiFi.scanDelete();
  });

  webServer.on("/api/save_wifi", HTTP_GET, [](AsyncWebServerRequest *request){
      String ssid, pass;
      if (request->hasParam("ssid")) ssid = request->getParam("ssid")->value();
      if (request->hasParam("pass")) pass = request->getParam("pass")->value();
      
      if (ssid.length() > 0) {
          Preferences prefs;
          prefs.begin("wifi_config", false);
          prefs.putString("ssid", ssid);
          prefs.putString("pass", pass);
          prefs.end();
          request->send(200, "text/plain", "Saved. Rebooting...");
          delay(1000);
          ESP.restart();
      } else {
          request->send(400, "text/plain", "Missing SSID");
      }
  });
  
  ElegantOTA.begin(&webServer);
  webServer.begin();
}

void webLoop() {
  ElegantOTA.loop();
}