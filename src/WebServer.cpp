#include "WebServer.h"
#include "MediaManager.h"
#include "plugins/PluginManager.h"
#include "Config.h"
#include "Logger.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>

static const char* TAG = "WebServer";

// Dashboard shell. Plugin cards (and each plugin's <script>) are injected at
// boot: %MODE_OPTIONS% = display-mode <option> list, %PLUGIN_CARDS% = plugin
// uiSection() HTML (scripts included).
//
// Any plugin card that needs live updates defines
//   function update<Plugin>(data)   // called with /api/status JSON
// and registers it via registerStatusUpdater(update<Plugin>) — see the
// lyrics card script for the pattern.
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Dashboard Config</title>
  <script>
    // Plugin cards register functions called with every /api/status poll.
    // Defined in <head> so plugin scripts (injected above the main block)
    // can call it while the page is still loading.
    var statusUpdaters = [];
    function registerStatusUpdater(fn) { statusUpdaters.push(fn); }
  </script>
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
%MODE_OPTIONS%
      </select>
      <label>Slideshow Interval: <span id="intervalVal">5</span> sec</label>
      <input type="range" id="ssInterval" min="2" max="60" value="5" oninput="document.getElementById('intervalVal').innerText = this.value">
      <button onclick="postData('/api/interval', 'seconds=' + document.getElementById('ssInterval').value)">Apply Interval</button>
    </div>

%PLUGIN_CARDS%

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
      <label>Theme</label>
      <select id="theme" onchange="postData('/api/theme', 'theme=' + this.value)">
        <option value="default">Default</option>
        <option value="pikachu">Pikachu</option>
        <option value="cyberpunk">Cyberpunk</option>
        <option value="midnight">Midnight</option>
      </select>
      <p style="color: #888; font-size: 0.85em; margin-top: 0;">Preset color scheme. Selecting a theme resets the lyric &amp; heart colors to its palette &mdash; you can still customize them below.</p>
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
        if (document.activeElement !== document.getElementById('theme') && data.theme !== undefined) {
          document.getElementById('theme').value = data.theme;
        }
        if (document.activeElement !== document.getElementById('rotation') && data.rotation !== undefined) {
          document.getElementById('rotation').value = data.rotation;
        }
        if (document.activeElement !== document.getElementById('ssInterval') && data.ssInterval !== undefined) {
          document.getElementById('ssInterval').value = data.ssInterval;
          document.getElementById('intervalVal').innerText = data.ssInterval;
        }
        for (var i = 0; i < statusUpdaters.length; i++) {
          try { statusUpdaters[i](data); } catch (e) { console.error(e); }
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

WebServer::WebServer(Settings& settings, WiFiManager& wifiManager, MediaManager& mediaManager,
                     PluginManager* pluginManager)
    : _settings(settings), _wifiManager(wifiManager), _mediaManager(mediaManager),
      _pluginManager(pluginManager), _server(80) {
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
    doc["theme"] = _settings.getTheme();
    doc["heartColor"] = _settings.getHeartColor();
    doc["lyricColor"] = _settings.getLyricColor();
    doc["lyricGlow"] = _settings.getLyricGlow();
    doc["lyricRainbow"] = _settings.getLyricRainbow();
    doc["lyricFull"] = _settings.getLyricFull();
    doc["lyricGradient"] = _settings.getLyricGradient();
    doc["lat"] = _settings.getLatitude();
    doc["lon"] = _settings.getLongitude();
    doc["displayMode"] = _settings.getDisplayMode();
    doc["rotation"] = _settings.getRotation();
    doc["ssInterval"] = _settings.getSlideshowInterval();

    // One object per registered plugin: { <id>: { name, mode, ... }, ... }.
    JsonObject plugins = doc["plugins"].to<JsonObject>();
    _pluginManager->buildStatusJson(plugins);
    // The lyrics card's script reads data.lyrics.* — keep a top-level lyrics
    // object fed by the plugin's slice for web-UI compatibility.
    JsonObject lyricsObj = doc["lyrics"].to<JsonObject>();
    if (Plugin* p = _pluginManager->findById("lyrics")) {
        p->buildStatusJson(lyricsObj);
    }

    String json;
    serializeJson(doc, json);
    return json;
}

String WebServer::buildIndexHtml() {
    String html = index_html;

    // 1. Mode dropdown from the registered plugins (name + mode number).
    String modeOptions;
    if (_pluginManager) {
        for (uint8_t i = 0; i < _pluginManager->count(); i++) {
            Plugin* p = _pluginManager->at(i);
            modeOptions += "<option value=\"" + String(p->mode()) + "\">" + p->name() + "</option>\n        ";
        }
    }
    html.replace("%MODE_OPTIONS%", modeOptions);

    // 2. Plugin cards + script blocks.
    String pluginCards;
    if (_pluginManager) {
        for (uint8_t i = 0; i < _pluginManager->count(); i++) {
            String section = _pluginManager->at(i)->uiSection();
            if (section.length() > 0) {
                pluginCards += section;
                pluginCards += "\n";
            }
        }
    }
    html.replace("%PLUGIN_CARDS%", pluginCards);

    return html;
}

void WebServer::setupRoutes() {
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request){
        request->send(200, "text/html", buildIndexHtml());
    });

    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request){
        request->send(200, "application/json", buildStatusJson());
    });

    // === Generic plugin API: POST /api/plugin/<id>/<action> ===
    // Plugin-specific endpoints live under this namespace so core routes stay
    // stable. (New plugin-specific literal routes can also be registered via
    // Plugin::registerRoutes, like the frozen /api/lyrics/* endpoints.)
    _server.on("/api/plugin", HTTP_POST, [this](AsyncWebServerRequest *request){
        String url = request->url(); // "/api/plugin" or "/api/plugin/..."
        String rest = (url.length() > 11) ? url.substring(11) : ""; // after "/api/plugin"
        // rest = "/<id>/<action>"
        int slash = rest.indexOf('/', 1);
        if (rest.length() < 3 || rest[0] != '/' || slash < 2) {
            request->send(400, "text/plain", "Expected /api/plugin/<id>/<action>");
            return;
        }
        String id = rest.substring(1, slash);
        String action = rest.substring(slash + 1);
        _pluginManager->dispatchApi(id, action, request);
    });

    // === Core device routes (unchanged) ===

    _server.on("/api/theme", HTTP_POST, [this](AsyncWebServerRequest *request){
        if (request->hasParam("theme", true)) {
            String theme = request->getParam("theme", true)->value();
            _settings.setTheme(theme);
            if (_pluginManager) _pluginManager->notifyTheme();
            LOG_INFO(TAG, ("Theme saved: " + theme).c_str());
        }
        request->send(200, "text/plain", "OK");
    });

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
            if (_pluginManager) _pluginManager->notifyRotation();
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

    // Plugins register their own literal routes (frozen endpoints etc.) last
    // so they win over any future core route on the same path.
    if (_pluginManager) {
        for (uint8_t i = 0; i < _pluginManager->count(); i++) {
            _pluginManager->at(i)->registerRoutes(_server);
        }
    }
}
