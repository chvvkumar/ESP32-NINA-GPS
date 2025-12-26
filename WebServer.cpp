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
      /* Map Specific Colors */
      --map-water: #161616;
      --map-land: #2f2f2f;
      --map-border: #3d3d3d;
      --map-grid: rgba(0, 229, 255, 0.05);
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

    /* Interactive Map */
    .globe-container { width: 100%; height: 350px; background: var(--map-water); border-radius: 8px; position: relative; overflow: hidden; touch-action: none; cursor: grab; }
    .globe-container:active { cursor: grabbing; }
    canvas { width: 100%; height: 100%; display: block; }
    
    .map-controls { position: absolute; bottom: 10px; right: 10px; display: flex; gap: 5px; }
    .map-btn { width: 30px; height: 30px; background: rgba(30,30,30,0.9); border: 1px solid rgba(255,255,255,0.2); color: white; border-radius: 4px; font-weight: bold; cursor: pointer; display: flex; justify-content: center; align-items: center; transition: all 0.2s; }
    .map-btn:hover { background: var(--accent); color: black; border-color: var(--accent); }
    
    .map-info-overlay { position: absolute; top: 10px; left: 10px; background: rgba(0,0,0,0.6); padding: 4px 8px; border-radius: 4px; font-size: 0.7rem; color: var(--text-muted); pointer-events: none; border: 1px solid rgba(255,255,255,0.1); }

    @media (max-width: 800px) {
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
        <div class="card-title" style="display:flex; justify-content:space-between; align-items:center;">
          <span>Position</span>
          <div style="display:flex; gap:8px; align-items:center;">
            <div class="status-badge" style="margin-left:8px;">
                <div id="statusDot" class="status-dot"></div>
                <span id="fixStatus">Initializing...</span>
            </div>
          </div>
        </div>
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
        <div class="card-title">Signal Quality</div>
        
        <!-- Satellite Counts -->
        <div class="stats-grid" style="grid-template-columns: 1fr 1fr; margin-bottom: 10px;">
          <div class="stat-box">
            <span class="stat-val" id="satsUsed">--</span>
            <span class="stat-lbl">Used Sats</span>
          </div>
          <div class="stat-box">
            <span class="stat-val" id="satsVisible">--</span>
            <span class="stat-lbl">Visible Sats</span>
          </div>
        </div>

        <!-- DOP Values -->
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
        <div class="card-title">Map</div>
        <div class="globe-container" id="mapContainer">
           <canvas id="mapCanvas"></canvas>
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
    
    function updateData() {
      fetch('/api/status').then(r => r.json()).then(d => {
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
        
        document.getElementById('ttff').textContent = d.ttff >= 0 ? d.ttff + 's' : '--';
        document.getElementById('satsUsed').textContent = d.sats;
        document.getElementById('satsVisible').textContent = d.satsVisible;
        document.getElementById('stIp').textContent = d.stationIp;
        document.getElementById('apIp').textContent = d.apIp;
        document.getElementById('tcpPort').textContent = d.tcpPort;
        
        document.getElementById('heading').innerHTML = Math.round(d.heading) + '&deg;';
        document.getElementById('compassNeedle').style.transform = `rotate(${d.heading}deg)`;
        
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
        if(window.updateMapPos) window.updateMapPos(d.lat, d.lon);
      }).catch(e => console.log(e));
    }

    function setLed(v) { fetch('/api/set_led?mode='+v); }
    function setRate(v) { 
        fetch('/api/set_interval?interval='+v);
        clearInterval(intervalId);
        currentInterval = parseInt(v);
        if(v > 0) intervalId = setInterval(updateData, v);
    }
    
    function copy(id, btn) {
        const val = document.getElementById(id).textContent;
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
        textArea.style.position = "fixed"; 
        textArea.style.opacity = "0";
        document.body.appendChild(textArea);
        textArea.focus();
        textArea.select();
        try {
            const successful = document.execCommand('copy');
            if(successful) showCopyFeedback(btn);
        } catch (err) { console.error('Copy failed'); }
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

    document.getElementById('ledMode').addEventListener('focus', () => isEditing = true);
    document.getElementById('ledMode').addEventListener('blur', () => isEditing = false);
    intervalId = setInterval(updateData, 5000);
    updateData();

    // ==========================================
    // HIGH FIDELITY VECTOR MAP ENGINE
    // ==========================================
    const canvas = document.getElementById('mapCanvas');
    const ctx = canvas.getContext('2d');
    let mapWidth, mapHeight;
    
    // Viewport State
    let camX = 0, camY = 0;
    let zoom = 1.0;
    let currentLat = 0, currentLon = 0;

    // Detailed Coordinates (Simplified World 1:110m)
    const worldGeo = [
       // North America
       [[-169,65],[-165,69],[-156,71],[-140,70],[-126,70],[-124,73],[-118,76],[-107,76],[-95,75],[-82,74],[-84,68],[-78,63],[-65,60],[-62,54],[-55,52],[-59,48],[-66,45],[-64,44],[-70,42],[-76,35],[-81,25],[-82,8],[-79,9],[-77,8],[-79,7],[-82,8],[-85,13],[-88,16],[-94,18],[-97,22],[-97,26],[-105,22],[-109,24],[-115,31],[-122,34],[-123,38],[-126,43],[-124,48],[-128,49],[-130,55],[-136,57],[-142,60],[-152,59],[-161,58],[-166,60],[-168,65]],
       // South America
       [[-77,8],[-75,11],[-72,12],[-61,10],[-57,6],[-52,5],[-50,1],[-40,-3],[-37,-6],[-35,-9],[-39,-13],[-39,-18],[-41,-21],[-48,-25],[-52,-30],[-52,-34],[-57,-37],[-62,-39],[-62,-41],[-66,-44],[-66,-48],[-69,-52],[-67,-55],[-74,-53],[-75,-48],[-73,-43],[-74,-37],[-72,-30],[-70,-22],[-72,-17],[-77,-13],[-81,-5],[-80,1],[-77,8]],
       // Europe / Asia
       [[-9,39],[-9,43],[-5,48],[-2,49],[1,50],[4,52],[8,55],[8,58],[11,57],[13,55],[15,56],[20,55],[22,58],[23,66],[21,69],[29,71],[40,68],[44,68],[48,69],[58,69],[60,76],[67,77],[68,73],[73,73],[72,70],[80,72],[86,74],[96,76],[106,77],[113,73],[121,73],[126,72],[135,71],[148,70],[161,69],[170,69],[179,66],[171,60],[161,59],[156,58],[150,59],[141,54],[140,51],[136,45],[131,42],[129,41],[129,35],[125,33],[122,30],[122,26],[119,25],[117,23],[113,22],[109,20],[105,10],[103,3],[98,8],[97,16],[91,22],[88,22],[79,9],[77,8],[72,20],[68,23],[64,25],[61,25],[56,26],[53,24],[50,30],[48,30],[36,36],[35,33],[35,31],[26,35],[28,41],[23,40],[20,42],[15,40],[18,42],[15,45],[10,44],[5,43],[-2,37],[-6,36],[-9,39]],
       // Africa
       [[-17,15],[-17,21],[-13,27],[-10,30],[-6,36],[0,36],[5,37],[10,34],[11,37],[23,32],[30,31],[34,28],[32,30],[34,26],[37,23],[41,16],[43,12],[51,12],[49,10],[41,0],[41,-10],[40,-15],[35,-24],[33,-26],[29,-32],[19,-34],[18,-29],[15,-23],[12,-16],[12,-8],[9,-2],[9,4],[-5,5],[-11,6],[-14,9],[-16,13],[-17,15]],
       // Australia
       [[114,-22],[113,-25],[115,-30],[115,-34],[118,-35],[121,-34],[124,-33],[128,-32],[131,-31],[136,-34],[138,-35],[144,-38],[147,-39],[150,-37],[150,-35],[153,-32],[153,-28],[153,-25],[150,-23],[146,-19],[144,-14],[142,-11],[136,-12],[133,-14],[130,-13],[129,-15],[124,-17],[122,-18],[114,-22]],
       // Antarctica
       [[-60,-63],[-57,-73],[-23,-73],[0,-70],[50,-67],[72,-68],[92,-65],[138,-65],[162,-70],[171,-72],[180,-76],[-170,-77],[-140,-74],[-100,-72],[-70,-72],[-60,-63]]
    ];
    
    // Islands & Details
    const islandGeo = [
       // Greenland
       [[-50,60],[-40,60],[-30,65],[-20,70],[-20,80],[-50,82],[-70,78],[-60,70],[-50,60]],
       // Madagascar
       [[44,-12],[50,-12],[50,-25],[43,-25],[44,-12]],
       // UK
       [[-5,50],[2,51],[0,58],[-6,58],[-8,55],[-5,50]],
       // Japan
       [[130,33],[135,33],[140,35],[142,40],[140,45],[138,40],[130,33]],
       // Indonesia / SE Asia Islands (Simplified Blob)
       [[95,5],[105,5],[120,-5],[110,-8],[100,-5],[95,5]],
       [[105,-6],[115,-6],[115,-8],[105,-8],[105,-6]],
       // New Zealand
       [[166,-46],[170,-46],[174,-40],[178,-35],[172,-35],[166,-46]],
       // Iceland
       [[-24,63],[-13,63],[-13,66],[-24,66],[-24,63]]
    ];

    function resizeMap() {
        const p = canvas.parentElement;
        mapWidth = p.clientWidth;
        mapHeight = p.clientHeight;
        canvas.width = mapWidth;
        canvas.height = mapHeight;
        drawMap();
    }
    window.addEventListener('resize', resizeMap);
    
    function lonToX(lon) { return (lon + 180) * (mapWidth / 360); }
    function latToY(lat) { return (90 - lat) * (mapHeight / 180); }

    function drawMap() {
        if(!mapWidth) return;
        
        ctx.fillStyle = getComputedStyle(document.body).getPropertyValue('--map-water') || '#161616';
        ctx.fillRect(0, 0, mapWidth, mapHeight);
        
        ctx.save();
        ctx.translate(mapWidth/2, mapHeight/2); 
        ctx.scale(zoom, zoom);
        ctx.translate(-mapWidth/2 + camX, -mapHeight/2 + camY);

        // Grid Lines
        ctx.strokeStyle = 'rgba(0, 229, 255, 0.05)';
        ctx.lineWidth = 1 / zoom; 
        ctx.beginPath();
        for(let x=0; x<=mapWidth; x+=mapWidth/18) { ctx.moveTo(x,0); ctx.lineTo(x,mapHeight); }
        for(let y=0; y<=mapHeight; y+=mapHeight/9) { ctx.moveTo(0,y); ctx.lineTo(mapWidth,y); }
        ctx.stroke();

        // Styles for Land
        ctx.fillStyle = '#2f2f2f'; 
        ctx.strokeStyle = '#3d3d3d';
        ctx.lineWidth = 1.5 / zoom;

        // Draw Continents
        worldGeo.forEach(poly => {
            ctx.beginPath();
            if(poly.length > 0) {
                ctx.moveTo(lonToX(poly[0][0]), latToY(poly[0][1]));
                for(let i=1; i<poly.length; i++) {
                    ctx.lineTo(lonToX(poly[i][0]), latToY(poly[i][1]));
                }
            }
            ctx.closePath();
            ctx.fill();
            ctx.stroke();
        });
        
        // Draw Islands
        islandGeo.forEach(poly => {
            ctx.beginPath();
            if(poly.length > 0) {
                ctx.moveTo(lonToX(poly[0][0]), latToY(poly[0][1]));
                for(let i=1; i<poly.length; i++) {
                    ctx.lineTo(lonToX(poly[i][0]), latToY(poly[i][1]));
                }
            }
            ctx.closePath();
            ctx.fill();
            ctx.stroke();
        });

        // GPS Position
        const px = lonToX(currentLon);
        const py = latToY(currentLat);
        
        const time = Date.now() / 1000;
        const radius = 5 + Math.sin(time * 3) * 2; 

        // Dot
        ctx.fillStyle = '#ff0000';
        ctx.shadowColor = '#d50000';
        ctx.shadowBlur = 10;
        ctx.beginPath();
        ctx.arc(px, py, 4 / zoom, 0, Math.PI * 2);
        ctx.fill();
        ctx.shadowBlur = 0;

        // Pulse Ring
        ctx.strokeStyle = '#ff0000';
        ctx.lineWidth = 2 / zoom;
        ctx.beginPath();
        ctx.arc(px, py, (radius*2) / zoom, 0, Math.PI * 2);
        ctx.stroke();

        ctx.restore();
        
        requestAnimationFrame(drawMap);
    }

    const container = document.getElementById('mapContainer');
    
    window.updateMapPos = function(lat, lon) {
        currentLat = lat; 
        currentLon = lon;
        // Map is static, dot moves
    };

    setTimeout(() => {
        resizeMap();
    }, 200);

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
    doc["satsVisible"] = gpsData.satellitesVisible;
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