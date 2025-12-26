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
#include "LedControl.h" // For Ticker/LED control

AsyncWebServer webServer(WEB_PORT);

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>GPS Node Status</title>
  <style>
    :root { --md-sys-color-background: #1a1a1a; --md-sys-color-surface: #2a2a2a; --md-sys-color-secondary: #90caf9; }
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: var(--md-sys-color-background); color: #fff; padding: 20px; }
    .container { max-width: 900px; margin: 0 auto; }
    h1 { text-align: center; margin-bottom: 20px; font-size: 2em; }
    h3 { margin-bottom: 12px; }
    .md-card { background: var(--md-sys-color-surface); border-radius: 12px; box-shadow: 0 2px 8px rgba(0,0,0,0.3); padding: 20px; margin-bottom: 16px; }
    .md-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 12px; margin-top: 12px; }
    .md-item { background: #232323; border-radius: 8px; padding: 12px; text-align: center; box-shadow: 0 1px 4px rgba(0,0,0,0.2); }
    .md-label { font-size: 0.9em; color: #bbb; margin-bottom: 4px; display: block; }
    .md-value { font-size: 1.3em; font-weight: 500; color: var(--md-sys-color-secondary); }
    .md-select, .md-input { width: 100%; background: #232323; color: #fff; border: none; border-radius: 8px; padding: 10px; font-size: 1em; margin-top: 8px; }
    .md-btn { width: 100%; background: var(--md-sys-color-secondary); color: #000; border: none; border-radius: 8px; padding: 12px; font-size: 1em; margin-top: 12px; cursor: pointer; font-weight: bold; }
    .md-btn:hover { background: #64b5f6; }
    .md-btn:disabled { background: #555; cursor: not-allowed; }
    .wifi-list { max-height: 200px; overflow-y: auto; background: #232323; border-radius: 8px; margin-top: 12px; }
    .wifi-item { padding: 10px; border-bottom: 1px solid #333; cursor: pointer; display: flex; justify-content: space-between; }
    .wifi-item:hover { background: #333; }
    .wifi-rssi { font-size: 0.8em; color: #bbb; }
    label { display: block; margin-bottom: 4px; color: #bbb; }
    .copy-btn { background: #404040; color: #90caf9; border: none; border-radius: 6px; padding: 6px 12px; font-size: 0.85em; cursor: pointer; margin-top: 8px; transition: background 0.2s; }
    .copy-btn:hover { background: #505050; }
    .copy-btn.copied { background: #2e7d32; color: #fff; }
    .md-item-with-copy { display: flex; flex-direction: column; align-items: center; }
    @media (max-width: 600px) { 
      .container { max-width: 100%; padding: 8px; } 
      .md-grid { grid-template-columns: repeat(2, 1fr); gap: 8px; }
      .md-card { padding: 12px; }
      .md-value { font-size: 1.15em; }
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>GPS Node Status</h1>
    
    <div class="md-card">
       <h3>Time</h3>
       <div class="md-grid">
        <div class="md-item"><span class="md-label">Time (UTC)</span><span class="md-value" id="time">--</span></div>
        <div class="md-item"><span class="md-label">Local Time</span><span class="md-value" id="localTime">--:--:--</span></div>
       </div>
    </div>
    
    <div class="md-card">
      <h3>Position</h3>
      <div class="md-grid">
        <div class="md-item md-item-with-copy"><span class="md-label">Latitude</span><span class="md-value" id="lat">0.0000000</span><button class="copy-btn" onclick="copyValue('lat', this)">Copy</button></div>
        <div class="md-item md-item-with-copy"><span class="md-label">Longitude</span><span class="md-value" id="lon">0.0000000</span><button class="copy-btn" onclick="copyValue('lon', this)">Copy</button></div>
        <div class="md-item md-item-with-copy"><span class="md-label">Altitude</span><span class="md-value" id="alt">0.0 m</span><button class="copy-btn" onclick="copyValue('alt', this)">Copy</button></div>
      </div>
    </div>

    <div class="md-card">
      <h3>Fix Info</h3>
      <div class="md-grid">
        <div class="md-item"><span class="md-label">Status</span><span class="md-value" id="fixStatus">--</span></div>
        <div class="md-item"><span class="md-label">Visible Satellites</span><span class="md-value" id="sats">0</span></div>
        <div class="md-item"><span class="md-label">TTFF</span><span class="md-value" id="ttff">--</span></div>
        <div class="md-item"><span class="md-label">PDOP</span><span class="md-value" id="pdop">--</span></div>
        <div class="md-item"><span class="md-label">HDOP</span><span class="md-value" id="hdop">--</span></div>
        <div class="md-item"><span class="md-label">VDOP</span><span class="md-value" id="vdop">--</span></div>
        <div class="md-item"><span class="md-label">Speed</span><span class="md-value" id="speed">0.0 m/s</span></div>
        <div class="md-item"><span class="md-label">Heading</span><span class="md-value" id="heading">0.0 &deg;</span></div>
        <div class="md-item"><span class="md-label">H. Acc</span><span class="md-value" id="hAcc">0.0 m</span></div>
        <div class="md-item"><span class="md-label">V. Acc</span><span class="md-value" id="vAcc">0.0 m</span></div>
      </div>
    </div>
    
    <div class="md-card">
      <h3>WiFi Settings</h3>
      <button class="md-btn" onclick="scanWifi()" id="scanBtn">Scan Networks</button>
      <div id="wifiList" class="wifi-list" style="display:none;"></div>
      <div style="margin-top: 16px;">
        <label for="wifiSsid">SSID:</label>
        <input type="text" id="wifiSsid" class="md-input" placeholder="SSID">
        <label for="wifiPass" style="margin-top: 12px;">Password:</label>
        <input type="password" id="wifiPass" class="md-input" placeholder="Password">
        <button class="md-btn" onclick="saveWifi()" id="saveBtn">Connect & Save</button>
      </div>
    </div>

    <div class="md-card">
      <h3>System & Configuration</h3>
      <div class="md-grid" style="margin-bottom: 16px;">
          <div class="md-item"><span class="md-label">Station IP</span><span class="md-value" id="stationIp">--</span></div>
          <div class="md-item"><span class="md-label">AP IP</span><span class="md-value" id="apIp">--</span></div>
      </div>
      <label for="ledMode">LED Mode:</label>
      <select class="md-select" id="ledMode" onchange="setLedMode(this.value)">
        <option value="0">Off</option> <option value="1">On</option> <option value="2">Blink on Read</option> <option value="3">Blink on Fix</option> <option value="4">Blink on Movement</option>
      </select>
      <label for="updateInterval" style="margin-top: 16px;">GPS Update Interval:</label>
      <select class="md-select" id="updateInterval" onchange="changeUpdateInterval(this.value)">
        <option value="1000">1 second</option> <option value="2000">2 seconds</option> <option value="5000">5 seconds</option> <option value="10000">10 seconds</option> <option value="30000">30 seconds</option> <option value="0">Paused</option>
      </select>
      <button class="md-btn" onclick="window.location.href='/update'" style="margin-top: 16px;">OTA Update</button>
    </div>
  </div>
  <script>
    let ledModeActive=false, updateIntervalId=null, currentUpdateInterval=5000;
    const ledModeSelect=document.getElementById('ledMode'), updateIntervalSelect=document.getElementById('updateInterval');
    updateIntervalSelect.value="5000";
    ledModeSelect.addEventListener('focus',()=>{ledModeActive=true;});
    ledModeSelect.addEventListener('blur',()=>{ledModeActive=false;});
    function updateData(){
      fetch('/api/status').then(r=>r.json()).then(d=>{
          if(d.stationIp) document.getElementById('stationIp').textContent=d.stationIp;
          if(d.apIp) document.getElementById('apIp').textContent=d.apIp;
          document.getElementById('fixStatus').textContent=d.fixStatus;
          document.getElementById('sats').textContent=d.sats;
          document.getElementById('ttff').textContent=d.ttff>=0?d.ttff+' s':'--';
          document.getElementById('pdop').textContent=Number(d.pdop).toFixed(2);
          document.getElementById('hdop').textContent=Number(d.hdop).toFixed(2);
          document.getElementById('vdop').textContent=Number(d.vdop).toFixed(2);
          
          document.getElementById('time').textContent=d.time;
          document.getElementById('localTime').textContent=d.localTime;
          document.getElementById('lat').textContent=Number(d.lat).toFixed(7);
          document.getElementById('lon').textContent=Number(d.lon).toFixed(7);
          document.getElementById('alt').textContent=Number(d.alt).toFixed(2)+' m';
          document.getElementById('speed').textContent=Number(d.speed).toFixed(2)+" m/s";
          document.getElementById('heading').textContent=Number(d.heading).toFixed(1)+" \u00B0";
          document.getElementById('hAcc').textContent=Number(d.hAcc).toFixed(2)+" m";
          document.getElementById('vAcc').textContent=Number(d.vAcc).toFixed(2)+" m";
          
          if(!ledModeActive && ledModeSelect.value!=d.ledMode) ledModeSelect.value=d.ledMode;
        }).catch(e=>console.error(e));
    }
    function setLedMode(m){fetch('/api/set_led?mode='+m).catch(e=>console.error(e));}
    function changeUpdateInterval(i){
      const ms=parseInt(i); currentUpdateInterval=ms;
      fetch('/api/set_interval?interval='+ms).catch(e=>console.error(e));
      if(updateIntervalId){clearInterval(updateIntervalId);updateIntervalId=null;}
      if(ms>0){updateIntervalId=setInterval(updateData,ms);updateData();}
    }
    function scanWifi(){
      const btn = document.getElementById('scanBtn');
      const list = document.getElementById('wifiList');
      btn.disabled = true; btn.textContent = 'Scanning...';
      list.style.display = 'none'; list.innerHTML = '';
      
      fetch('/api/scan').then(r=>r.json()).then(networks=>{
        btn.disabled = false; btn.textContent = 'Scan Networks';
        if(networks.length===0) return;
        list.style.display = 'block';
        networks.forEach(n=>{
          const div=document.createElement('div');
          div.className='wifi-item';
          div.innerHTML=`<span>${n.ssid}</span><span class="wifi-rssi">${n.rssi} dBm</span>`;
          div.onclick=()=>{document.getElementById('wifiSsid').value=n.ssid; document.getElementById('wifiPass').focus();}
          list.appendChild(div);
        });
      }).catch(e=>{console.error(e); btn.disabled=false; btn.textContent='Scan Failed';});
    }
    function saveWifi(){
      const ssid = document.getElementById('wifiSsid').value;
      const pass = document.getElementById('wifiPass').value;
      const btn = document.getElementById('saveBtn');
      if(!ssid) return alert('SSID Required');
      btn.disabled=true; btn.textContent='Saving...';
      fetch(`/api/save_wifi?ssid=${encodeURIComponent(ssid)}&pass=${encodeURIComponent(pass)}`).then(r=>r.text()).then(t=>{
        alert(t); btn.textContent='Saved';
      }).catch(e=>{console.error(e); btn.textContent='Error'; btn.disabled=false;});
    }
    function copyValue(id,btn){
      const txt=document.getElementById(id).textContent.replace(/\s*[a-zA-Z\/]+\s*$/,'').trim();
      if(navigator.clipboard&&navigator.clipboard.writeText){
        navigator.clipboard.writeText(txt).then(()=>{btn.textContent='Copied!';btn.classList.add('copied');setTimeout(()=>{btn.textContent='Copy';btn.classList.remove('copied');},2000);})
        .catch(()=>copyFallback(txt,btn));
      }else copyFallback(txt,btn);
    }
    function copyFallback(t,btn){
      const ta=document.createElement('textarea');ta.value=t;ta.style.position='fixed';ta.style.opacity='0';document.body.appendChild(ta);ta.select();
      try{
        if(document.execCommand('copy')){btn.textContent='Copied!';btn.classList.add('copied');setTimeout(()=>{btn.textContent='Copy';btn.classList.remove('copied');},2000);}
        else{btn.textContent='Failed';setTimeout(()=>{btn.textContent='Copy';},2000);}
      }catch(e){btn.textContent='Failed';setTimeout(()=>{btn.textContent='Copy';},2000);}finally{document.body.removeChild(ta);}
    }
    updateIntervalId=setInterval(updateData,currentUpdateInterval);updateData();
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
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  webServer.on("/api/set_led", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("mode")) {
      int mode = request->getParam("mode")->value().toInt();
      gpsData.ledMode = static_cast<LedMode>(mode);
      // We need to stop the ticker if we change modes, this is done via LedControl logic implies
      // But we can reset state here
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
