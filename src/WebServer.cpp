#include "WebServer.h"
#include "MediaManager.h"
#include "LyricsManager.h"
#include "Config.h"
#include "Logger.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>

static const char* TAG = "WebServer";

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Dashboard Config</title>
  <style>
    body { font-family: system-ui, -apple-system, sans-serif; background: #f4f4f9; color: #333; margin: 0; padding: 20px; }
    .container { max-width: 600px; margin: auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
    h1 { text-align: center; color: #2c3e50; }
    .card { border: 1px solid #ddd; border-radius: 5px; padding: 15px; margin-bottom: 20px; }
    .card h2 { margin-top: 0; color: #2980b9; font-size: 1.2em; border-bottom: 1px solid #eee; padding-bottom: 5px; }
    label { display: block; margin-bottom: 5px; font-weight: bold; }
    input[type="text"], input[type="password"], input[type="range"], select { width: 100%; box-sizing: border-box; padding: 8px; margin-bottom: 10px; border: 1px solid #ccc; border-radius: 4px; }
    button { background: #3498db; color: white; border: none; padding: 10px 15px; border-radius: 4px; cursor: pointer; font-size: 1em; }
    button:hover { background: #2980b9; }
    button.danger { background: #e74c3c; }
    button.danger:hover { background: #c0392b; }
    .row { display: flex; justify-content: space-between; margin-bottom: 5px; }
    .value { font-family: monospace; color: #555; }
    .file-item { display: flex; justify-content: space-between; align-items: center; padding: 5px 0; border-bottom: 1px solid #f0f0f0; }
    .lyric-line { font-style: italic; color: #888; }
  </style>
</head>
<body>
  <div class="container">
    <h1>Dashboard Config</h1>

    <div class="card">
      <h2>Device Status</h2>
      <div class="row"><span>Firmware:</span> <span class="value" id="firmware">-</span></div>
      <div class="row"><span>Uptime:</span> <span class="value" id="uptime">-</span></div>
      <div class="row"><span>Free Heap:</span> <span class="value" id="heap">-</span></div>
    </div>

    <div class="card">
      <h2>Screen Mode Switcher</h2>
      <label>Active Screen Mode</label>
      <select id="displayMode" onchange="postData('/api/mode', 'mode=' + this.value)">
        <option value="0">Clock & Weather Mode</option>
        <option value="1">Media / Slideshow Mode</option>
        <option value="2">Lyrics Mode</option>
      </select>
      <label>Slideshow Interval: <span id="intervalVal">5</span> sec</label>
      <input type="range" id="ssInterval" min="2" max="60" value="5" oninput="document.getElementById('intervalVal').innerText = this.value">
      <button onclick="postData('/api/interval', 'seconds=' + document.getElementById('ssInterval').value)">Apply Interval</button>
    </div>

    <div class="card">
      <h2>Lyrics Status</h2>
      <div class="row"><span>Status:</span> <span class="value" id="lyricStatus">-</span></div>
      <div class="row"><span>Phone:</span> <span class="value" id="phoneStatus" style="color:#888;">Not connected</span></div>
      <div class="row"><span>Song:</span> <span class="value lyric-line" id="lyricTitle">-</span></div>
      <div class="row"><span>Artist:</span> <span class="value lyric-line" id="lyricArtist">-</span></div>
      <div class="row"><span>Line:</span> <span class="value lyric-line" id="lyricLine">-</span></div>
      <div class="row"><span>Progress:</span> <span class="value" id="lyricProgress">-</span></div>
      <input type="text" id="manualLyric" placeholder="Manual display test text">
      <button onclick="sendManualLyric()">Show Test Text</button>
      <p style="color: #888; font-size: 0.85em;">Tap TEST in the Android app to verify connection, then START to sync lyrics.</p>
    </div>

    <div class="card">
      <h2>Upload Media (JPEG / PNG / GIF)</h2>
      <form method="POST" action="/upload" enctype="multipart/form-data">
        <input type="file" name="upload" accept=".jpg,.jpeg,.png,.gif" required>
        <br><br>
        <button type="submit">Upload to Screen Flash</button>
      </form>
    </div>

    <div class="card">
      <h2>Stored Media Files</h2>
      <div id="fileList">Loading files...</div>
    </div>

    <div class="card">
      <h2>Display Settings</h2>
      <label>Orientation</label>
      <select id="rotation" onchange="postData('/api/orientation', 'rotation=' + this.value)">
        <option value="0">Portrait</option>
        <option value="1">Landscape</option>
        <option value="2">Portrait (flipped)</option>
        <option value="3">Landscape (flipped)</option>
      </select>
      <p style="color: #888; font-size: 0.85em; margin-top: 0;">Applies instantly; the active screen re-lays out for the new dimensions.</p>
      <label>Brightness: <span id="brightVal"></span>%</label>
      <input type="range" id="brightness" min="0" max="255" oninput="document.getElementById('brightVal').innerText = Math.round(this.value/255*100)">
      <button onclick="postData('/api/brightness', 'brightness=' + document.getElementById('brightness').value)">Apply</button>
    </div>

    <div class="card">
      <h2>Wi-Fi</h2>
      <div class="row"><span>Status:</span> <span class="value" id="wifiStatus">-</span></div>
      <div class="row"><span>IP Address:</span> <span class="value" id="ip">-</span></div>
      <br>
      <label>SSID</label>
      <input type="text" id="ssid" placeholder="Enter SSID">
      <label>Password</label>
      <input type="password" id="password" placeholder="Enter Password">
      <div style="display:flex; gap:10px;">
        <button onclick="postData('/api/wifi', 'ssid=' + encodeURIComponent(document.getElementById('ssid').value) + '&password=' + encodeURIComponent(document.getElementById('password').value))">Save</button>
        <button onclick="postData('/api/connect', '')">Connect</button>
      </div>
    </div>

    <div class="card">
      <h2>Location Settings (Weather)</h2>
      <label>Latitude</label>
      <input type="text" id="lat" placeholder="e.g. 28.61">
      <label>Longitude</label>
      <input type="text" id="lon" placeholder="e.g. 77.20">
      <button onclick="postData('/api/location', 'lat=' + encodeURIComponent(document.getElementById('lat').value) + '&lon=' + encodeURIComponent(document.getElementById('lon').value))">Save Location</button>
    </div>

    <div class="card">
      <h2>Clock Settings</h2>
      <label>Custom Text</label>
      <input type="text" id="clockText" placeholder="Enter text below clock">
      <button onclick="postData('/api/clocktext', 'text=' + encodeURIComponent(document.getElementById('clockText').value))">Save Text</button>
      <br><br>
      <label>Pixel Heart Color</label>
      <input type="color" id="heartColor" value="#00d2ff" style="height: 40px; padding: 2px;">
      <button onclick="postData('/api/heartcolor', 'color=' + encodeURIComponent(document.getElementById('heartColor').value))">Save Heart Color</button>
    </div>

    <div class="card">
      <h2>System</h2>
      <div style="display:flex; gap:10px;">
        <button class="danger" onclick="if(confirm('Restart device?')) postData('/api/restart', '')">Restart</button>
        <button class="danger" onclick="if(confirm('Factory reset? All settings will be lost.')) postData('/api/reset', '')">Factory Reset</button>
      </div>
    </div>
  </div>

  <script>
    function updateData() {
      fetch('/api/status').then(r => r.json()).then(data => {
        document.getElementById('firmware').innerText = data.firmware;
        document.getElementById('uptime').innerText = data.uptime + ' s';
        document.getElementById('heap').innerText = data.heap + ' bytes';
        document.getElementById('wifiStatus').innerText = data.wifiStatus;
        document.getElementById('ip').innerText = data.ip;

        if (document.activeElement !== document.getElementById('brightness')) {
          document.getElementById('brightness').value = data.brightness;
          document.getElementById('brightVal').innerText = Math.round(data.brightness/255*100);
        }
        if (!document.getElementById('ssid').value && document.activeElement !== document.getElementById('ssid')) {
          document.getElementById('ssid').value = data.ssid;
        }
        if (document.activeElement !== document.getElementById('clockText') && data.clockText !== undefined) {
          document.getElementById('clockText').value = data.clockText;
        }
        if (document.activeElement !== document.getElementById('heartColor') && data.heartColor !== undefined) {
          document.getElementById('heartColor').value = data.heartColor;
        }
        if (document.activeElement !== document.getElementById('lat') && data.lat !== undefined) {
          document.getElementById('lat').value = data.lat;
        }
        if (document.activeElement !== document.getElementById('lon') && data.lon !== undefined) {
          document.getElementById('lon').value = data.lon;
        }
        if (document.activeElement !== document.getElementById('displayMode') && data.displayMode !== undefined) {
          document.getElementById('displayMode').value = data.displayMode;
        }
        if (document.activeElement !== document.getElementById('rotation') && data.rotation !== undefined) {
          document.getElementById('rotation').value = data.rotation;
        }
        if (document.activeElement !== document.getElementById('ssInterval') && data.ssInterval !== undefined) {
          document.getElementById('ssInterval').value = data.ssInterval;
          document.getElementById('intervalVal').innerText = data.ssInterval;
        }
        // Lyrics status
        if (data.lyrics !== undefined) {
          document.getElementById('lyricStatus').innerText = data.lyrics.active ? 'Active' : 'Idle';
          var phoneEl = document.getElementById('phoneStatus');
          if (data.lyrics.phoneConnected) {
            phoneEl.innerText = '✅ Connected';
            phoneEl.style.color = '#4CAF50';
          } else {
            phoneEl.innerText = 'Not connected';
            phoneEl.style.color = '#888';
          }
          document.getElementById('lyricTitle').innerText = data.lyrics.title || '-';
          document.getElementById('lyricArtist').innerText = data.lyrics.artist || '-';
          document.getElementById('lyricLine').innerText = data.lyrics.currentLine || '-';
          document.getElementById('lyricProgress').innerText = data.lyrics.progress + '%';
        }
      });
      loadFiles();
    }

    function loadFiles() {
      fetch('/api/files').then(r => r.json()).then(files => {
        let html = '';
        if (files.length === 0) {
          html = '<p>No media files uploaded yet.</p>';
        } else {
          files.forEach(f => {
            html += `<div class="file-item"><span>${f}</span><button class="danger" onclick="deleteFile('${f}')">Delete</button></div>`;
          });
        }
        document.getElementById('fileList').innerHTML = html;
      });
    }

    function deleteFile(name) {
      if (confirm('Delete ' + name + '?')) {
        postData('/api/deletefile', 'filename=' + encodeURIComponent(name));
      }
    }

    function sendManualLyric() {
      const text = document.getElementById('manualLyric').value.trim();
      if (!text) return;
      fetch('/api/lyrics/update', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ type: 'line', current: text, progress: 0 })
      })
        .then(() => updateData());
    }

    function postData(url, body) {
      fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: body
      }).then(() => updateData());
    }

    setInterval(updateData, 4000);
    updateData();
  </script>
</body>
</html>
)rawliteral";

static File uploadFile;

WebServer::WebServer(Settings& settings, WiFiManager& wifiManager, MediaManager& mediaManager, LyricsManager& lyricsManager)
    : _settings(settings), _wifiManager(wifiManager), _mediaManager(mediaManager), _lyricsManager(lyricsManager), _server(80) {
}

WebServer::~WebServer() {
}

void WebServer::begin() {
    LOG_INFO(TAG, "Starting Web Server...");
    setupRoutes();
    _server.begin();
    LOG_INFO(TAG, "Web server started");
}

String WebServer::buildStatusJson() {
    // Built with ArduinoJson so all strings (song titles, artist names, clock
    // text, etc.) are correctly escaped for JSON.
    JsonDocument doc;
    doc["firmware"] = Config::FIRMWARE_VERSION;
    doc["uptime"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
    doc["brightness"] = _settings.getBrightness();
    doc["ssid"] = _settings.getWifiSSID();
    doc["wifiStatus"] = _wifiManager.isConnected() ? "Connected" : "Disconnected";
    doc["ip"] = _wifiManager.ipAddress();
    doc["clockText"] = _settings.getClockText();
    doc["heartColor"] = _settings.getHeartColor();
    doc["lat"] = _settings.getLatitude();
    doc["lon"] = _settings.getLongitude();
    doc["displayMode"] = _settings.getDisplayMode();
    doc["rotation"] = _settings.getRotation();
    doc["ssInterval"] = _settings.getSlideshowInterval();

    JsonObject lyrics = doc["lyrics"].to<JsonObject>();
    lyrics["active"] = _lyricsManager.isActive();
    lyrics["phoneConnected"] = _lyricsManager.isPhoneConnected();
    lyrics["title"] = _lyricsManager.getTitle();
    lyrics["artist"] = _lyricsManager.getArtist();
    lyrics["currentLine"] = _lyricsManager.getCurrentLine();
    lyrics["progress"] = _lyricsManager.getProgress();

    String json;
    serializeJson(doc, json);
    return json;
}

void WebServer::setupRoutes() {
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });

    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request){
        request->send(200, "application/json", buildStatusJson());
    });

    // JSON handler for the web dashboard's "Show Test Text" (the app itself
    // uses form-encoded POSTs to /api/lyrics/line, which parse reliably).
    auto* lyricsJsonHandler = new AsyncCallbackJsonWebHandler("/api/lyrics/update",
        [this](AsyncWebServerRequest *request, JsonVariant& json) {
            JsonObject data = json.as<JsonObject>();
            String type = data["type"] | "";
            if (type == "line") {
                String current = data["current"] | "";
                if (current.isEmpty()) {
                    request->send(400, "text/plain", "Missing current lyric line");
                    return;
                }
                String prev = data["prev"] | "";
                String next = data["next"] | "";
                uint8_t progress = data["progress"] | 0;
                _lyricsManager.setLine(current, prev, next, progress);
                LOG_INFO(TAG, ("Lyric line received via JSON: " + current).c_str());
            } else if (type == "song") {
                String artist = data["artist"] | "";
                String title = data["title"] | "";
                if (artist.isEmpty() || title.isEmpty()) {
                    request->send(400, "text/plain", "Missing artist or title");
                    return;
                }
                _lyricsManager.setNowPlaying(artist, title);
                LOG_INFO(TAG, ("Now playing received via JSON: " + title).c_str());
            } else {
                request->send(400, "text/plain", "Unsupported lyrics update");
                return;
            }
            request->send(200, "text/plain", "OK");
        });
    _server.addHandler(lyricsJsonHandler);

    _server.on("/api/heartcolor", HTTP_POST, [this](AsyncWebServerRequest *request){
        if (request->hasParam("color", true)) {
            _settings.setHeartColor(request->getParam("color", true)->value());
            LOG_INFO(TAG, "Settings saved: Heart color");
        }
        request->send(200, "text/plain", "OK");
    });

    _server.on("/api/mode", HTTP_POST, [this](AsyncWebServerRequest *request){
        if (request->hasParam("mode", true)) {
            uint8_t mode = request->getParam("mode", true)->value().toInt();
            _settings.setDisplayMode(mode);
            LOG_INFO(TAG, ("Display mode updated to: " + String(mode)).c_str());
        }
        request->send(200, "text/plain", "OK");
    });

    _server.on("/api/interval", HTTP_POST, [this](AsyncWebServerRequest *request){
        if (request->hasParam("seconds", true)) {
            uint32_t secs = request->getParam("seconds", true)->value().toInt();
            if (secs < 2) secs = 2;
            if (secs > 3600) secs = 3600;
            _settings.setSlideshowInterval(secs);
            LOG_INFO(TAG, ("Slideshow interval updated to: " + String(secs) + "s").c_str());
        }
        request->send(200, "text/plain", "OK");
    });

    _server.on("/api/files", HTTP_GET, [this](AsyncWebServerRequest *request){
        std::vector<String> files = _mediaManager.getFileList();
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (const String& f : files) {
            arr.add(f);
        }
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    _server.on("/api/deletefile", HTTP_POST, [this](AsyncWebServerRequest *request){
        if (request->hasParam("filename", true)) {
            String name = request->getParam("filename", true)->value();
            if (!_mediaManager.requestDelete(name)) {
                request->send(503, "text/plain", "Unable to queue delete request");
                return;
            }
            LOG_INFO(TAG, ("Queued deletion of: " + name).c_str());
        }
        request->send(202, "text/plain", "Delete queued");
    });

    _server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", "Upload complete! <a href='/'>Go Back</a>");
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
        if (!index) {
            // Close any stale handle from a previous (possibly dropped) upload
            if (uploadFile) {
                uploadFile.close();
                uploadFile = File();
            }
            // Sanitize: strip any path components and dangerous sequences from the name
            int lastSep = max(filename.lastIndexOf('/'), filename.lastIndexOf('\\'));
            String name = filename.substring(lastSep + 1);
            name.replace("..", "_");
            name.trim();
            if (name.isEmpty()) name = "upload.bin";

            String path = "/" + name;
            LOG_INFO(TAG, ("Starting file upload: " + path).c_str());
            uploadFile = LittleFS.open(path, "w");
        }
        if (uploadFile) {
            uploadFile.write(data, len);
        }
        if (final) {
            if (uploadFile) {
                uploadFile.close();
                uploadFile = File();
                LOG_INFO(TAG, "File upload finished.");
            }
        }
    });

    _server.on("/api/location", HTTP_POST, [this](AsyncWebServerRequest *request){
        if (request->hasParam("lat", true) && request->hasParam("lon", true)) {
            _settings.setLatitude(request->getParam("lat", true)->value());
            _settings.setLongitude(request->getParam("lon", true)->value());
            LOG_INFO(TAG, "Settings saved: Location");
        }
        request->send(200, "text/plain", "OK");
    });

    _server.on("/api/clocktext", HTTP_POST, [this](AsyncWebServerRequest *request){
        if (request->hasParam("text", true)) {
            _settings.setClockText(request->getParam("text", true)->value());
            LOG_INFO(TAG, "Settings saved: Clock text");
        }
        request->send(200, "text/plain", "OK");
    });

    _server.on("/api/brightness", HTTP_POST, [this](AsyncWebServerRequest *request){
        if (request->hasParam("brightness", true)) {
            uint8_t b = request->getParam("brightness", true)->value().toInt();
            _settings.setBrightness(b);
            LOG_INFO(TAG, "Settings saved: Brightness");
        }
        request->send(200, "text/plain", "OK");
    });

    _server.on("/api/orientation", HTTP_POST, [this](AsyncWebServerRequest *request){
        if (request->hasParam("rotation", true)) {
            uint8_t rot = request->getParam("rotation", true)->value().toInt();
            _settings.setRotation(rot);
            LOG_INFO(TAG, "Settings saved: Orientation");
        }
        request->send(200, "text/plain", "OK");
    });

    _server.on("/api/wifi", HTTP_POST, [this](AsyncWebServerRequest *request){
        if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
            String ssid = request->getParam("ssid", true)->value();
            String pass = request->getParam("password", true)->value();
            _settings.setWifiSSID(ssid);
            _settings.setWifiPassword(pass);
            LOG_INFO(TAG, "Settings saved: Wi-Fi credentials");
        }
        request->send(200, "text/plain", "OK");
    });

    _server.on("/api/connect", HTTP_POST, [this](AsyncWebServerRequest *request){
        _wifiManager.connect();
        request->send(200, "text/plain", "OK");
    });

    _server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "Restarting");
        delay(500);
        ESP.restart();
    });

    _server.on("/api/reset", HTTP_POST, [this](AsyncWebServerRequest *request){
        _settings.clearAll();
        request->send(200, "text/plain", "Factory Reset");
        delay(500);
        ESP.restart();
    });

    // === Lyrics API Endpoints (for Android app) ===

    _server.on("/api/lyrics/line", HTTP_POST, [this](AsyncWebServerRequest *request){
        if (!request->hasParam("current", true)) {
            LOG_ERROR(TAG, "Lyrics line request missing current parameter");
            request->send(400, "text/plain", "Missing current lyric line");
            return;
        }
        String current = request->getParam("current", true)->value();
        String prev = request->hasParam("prev", true) ? request->getParam("prev", true)->value() : "";
        String next = request->hasParam("next", true) ? request->getParam("next", true)->value() : "";
        uint8_t prog = request->hasParam("progress", true) ? (uint8_t)request->getParam("progress", true)->value().toInt() : 0;
        _lyricsManager.setLine(current, prev, next, prog);
        LOG_INFO(TAG, ("Lyric line received: " + current).c_str());
        request->send(200, "text/plain", "OK");
    });

    _server.on("/api/lyrics/nowplaying", HTTP_POST, [this](AsyncWebServerRequest *request){
        if (request->hasParam("artist", true) && request->hasParam("title", true)) {
            String artist = request->getParam("artist", true)->value();
            String title = request->getParam("title", true)->value();
            _lyricsManager.setNowPlaying(artist, title);
            LOG_INFO(TAG, ("Now Playing from app: " + title + " - " + artist).c_str());
        }
        request->send(200, "text/plain", "OK");
    });

    _server.on("/api/lyrics/clear", HTTP_POST, [this](AsyncWebServerRequest *request){
        _lyricsManager.clear();
        LOG_INFO(TAG, "Lyrics cleared");
        request->send(200, "text/plain", "OK");
    });

    _server.on("/api/lyrics/stop", HTTP_POST, [this](AsyncWebServerRequest *request){
        _lyricsManager.stop();
        LOG_INFO(TAG, "Lyrics stopped");
        request->send(200, "text/plain", "OK");
    });

    _server.on("/api/lyrics/ping", HTTP_POST, [this](AsyncWebServerRequest *request){
        _lyricsManager.setPhoneConnected(true);
        LOG_INFO(TAG, "Phone ping received");
        request->send(200, "text/plain", "OK");
    });

    _server.on("/api/lyrics/test", HTTP_POST, [this](AsyncWebServerRequest *request){
        if (!request->hasParam("text", true)) {
            request->send(400, "text/plain", "Missing text");
            return;
        }
        String text = request->getParam("text", true)->value();
        _lyricsManager.setLine(text);
        LOG_INFO(TAG, ("Display test text received: " + text).c_str());
        request->send(200, "text/plain", "Displayed");
    });
}
