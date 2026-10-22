#include "WebServer.h"
#include "EmojiFont.h"
#include "MediaManager.h"
#include "plugins/PluginManager.h"
#include "Config.h"
#include "Logger.h"
#include "DashboardAssets.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <memory>

static const char* TAG = "WebServer";

// Dashboard shell. Plugin cards (and each plugin's <script>) are injected at
// boot: %MODE_OPTIONS% = display-mode <option> list, %LYRICS_CARDS% / %SCORES_CARDS%
// = plugin uiSection() HTML (scripts included), %EMOJI_PICKER% = the emoji strip.
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
  <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
  <title>ESP32 Dashboard</title>
  <script>
    var statusUpdaters = [];
    function registerStatusUpdater(fn) { statusUpdaters.push(fn); }
  </script>
  <style>
%DASHBOARD_CSS%
  </style>
</head>
<body>

  <div class="topbar">
    <div class="topbar-title">
      <div class="topbar-dot" id="wifiDot"></div>
      <h1>ESP32 Dashboard</h1>
    </div>
    <div class="status-chips">
      <span class="chip" id="chipUptime">–</span>
      <span class="chip" id="chipHeap">–</span>
      <span class="chip" id="chipFw">–</span>
    </div>
  </div>

  <!-- Shown when a status poll fails: the fields below are the last values the
       device sent, not a live reading. Without this a frozen page could show a
       stale "Disconnected" for a board that is up. -->
  <div class="conn-warn" id="connWarn" style="display:none">
    No answer from the device — the values below are the last ones it sent.
  </div>

  <div class="layout">
    <div class="main-content">

      <!-- ══════════════ HOME ══════════════ -->
      <div class="section active" id="s-home">

        <div class="card">
          <h2>Device</h2>
          <div class="row"><span>Firmware</span><span class="value" id="firmware">–</span></div>
          <div class="row"><span>Uptime</span><span class="value" id="uptime">–</span></div>
          <div class="row"><span>Free heap</span><span class="value" id="heap">–</span></div>
          <div class="row"><span>Wi-Fi</span><span class="value" id="wifiStatus">–</span></div>
          <div class="row"><span>IP address</span><span class="value" id="ip">–</span></div>
        </div>

        <div class="card">
          <h2>Active Screen</h2>
          <label>Mode</label>
          <select id="displayMode" onchange="postData('/api/mode','mode='+this.value)">
%MODE_OPTIONS%
          </select>
          <label>Slideshow interval</label>
          <div class="slider-row">
            <input type="range" id="ssInterval" min="2" max="60" value="5" oninput="document.getElementById('intervalVal').innerText=this.value">
            <span class="slider-val"><span id="intervalVal">5</span>s</span>
          </div>
          <div class="btn-row">
            <button class="btn btn-primary btn-sm" onclick="postData('/api/interval','seconds='+document.getElementById('ssInterval').value)">Apply Interval</button>
          </div>
        </div>

        <div class="card">
          <h2>System</h2>
          <div class="btn-row">
            <button class="btn btn-secondary btn-sm" onclick="showSection('s-settings')">⚙ Wi-Fi Settings</button>
          </div>
          <div class="btn-row" style="margin-top:8px;">
            <button class="btn btn-danger" onclick="if(confirm('Restart device?'))postData('/api/restart','')">Restart</button>
            <button class="btn btn-secondary" onclick="if(confirm('Factory reset? All settings will be lost.'))postData('/api/reset','')">Factory Reset</button>
          </div>
        </div>

      </div>

      <!-- ══════════════ DISPLAY ══════════════ -->
      <div class="section" id="s-display">

        <div class="card">
          <h2>Appearance</h2>
          <label>Theme</label>
          <select id="theme" onchange="postData('/api/theme','theme='+this.value)">
            <option value="default">Default</option>
            <option value="pikachu">Pikachu</option>
            <option value="cyberpunk">Cyberpunk</option>
            <option value="midnight">Midnight</option>
          </select>
          <p>Preset colour scheme. Themes reset lyric &amp; heart colours to their palette — you can still customise them below.</p>
          <label>Orientation</label>
          <select id="rotation" onchange="postData('/api/orientation','rotation='+this.value)">
            <option value="0">Portrait</option>
            <option value="1">Landscape</option>
            <option value="2">Portrait (flipped)</option>
            <option value="3">Landscape (flipped)</option>
          </select>
          <p>Applies instantly; the active screen re-lays out for the new dimensions.</p>
          <label>Brightness</label>
          <div class="slider-row">
            <input type="range" id="brightness" min="0" max="255" oninput="document.getElementById('brightVal').innerText=Math.round(this.value/255*100)">
            <span class="slider-val"><span id="brightVal">–</span>%</span>
          </div>
          <div class="btn-row">
            <button class="btn btn-primary btn-sm" onclick="postData('/api/brightness','brightness='+document.getElementById('brightness').value)">Apply</button>
          </div>
        </div>

        <div class="card">
          <h2>Clock</h2>
          <label>Clock face</label>
          <select id="clockStyle" onchange="setClockStyle(this.value)">
            <option value="digital">Digital</option>
            <option value="analogue">Analogue</option>
            <option value="8bit">8-bit</option>
            <option value="anime">Anime</option>
            <option value="minimal">Minimal</option>
          </select>
          <p id="faceHint">Pick a face from the previews below — the panel repaints straight away.</p>
          <div class="face-grid" id="faceGrid">
            <button type="button" class="face-card" data-face="digital" onclick="setClockStyle('digital')">
              <span class="face-thumb t-digital"><i class="heart"></i><i class="big"></i><i class="small"></i></span>
              <span class="face-name">Digital</span>
              <span class="face-desc">Heart above big time</span>
            </button>
            <button type="button" class="face-card" data-face="analogue" onclick="setClockStyle('analogue')">
              <span class="face-thumb t-analogue"><i class="ring"></i><i class="hand h"></i><i class="hand m"></i><i class="hand s"></i><i class="pin"></i></span>
              <span class="face-name">Analogue</span>
              <span class="face-desc">Dial with live hands</span>
            </button>
            <button type="button" class="face-card" data-face="8bit" onclick="setClockStyle('8bit')">
              <span class="face-thumb t-8bit"><i class="big"></i><i class="small"></i></span>
              <span class="face-name">8-bit</span>
              <span class="face-desc">Framed block digits</span>
            </button>
            <button type="button" class="face-card" data-face="anime" onclick="setClockStyle('anime')">
              <span class="face-thumb t-anime"><i class="sp" style="left:14px;top:7px;"></i><i class="sp x" style="left:11px;top:10px;"></i><i class="sp" style="right:16px;top:15px;"></i><i class="sp x" style="right:13px;top:18px;"></i><i class="eye l"></i><i class="eye r"></i><i class="smile"></i><i class="big"></i></span>
              <span class="face-name">Anime</span>
              <span class="face-desc">Chibi face + sparkles</span>
            </button>
            <button type="button" class="face-card" data-face="minimal" onclick="setClockStyle('minimal')">
              <span class="face-thumb t-minimal"><i class="big"></i><i class="rule"></i><i class="small"></i></span>
              <span class="face-name">Minimal</span>
              <span class="face-desc">Time and a hairline rule</span>
            </button>
          </div>
          <div class="btn-row">
            <button class="btn btn-secondary btn-sm" onclick="postData('/api/mode','mode=0')">▶ Show clock on device</button>
          </div>
          <label>Custom text (shown below the time)</label>
          <input type="text" id="clockText" placeholder="e.g. Good morning!">
          <div class="emoji-row">%EMOJI_PICKER%</div>
          <div class="btn-row">
            <button class="btn btn-primary btn-sm" onclick="postData('/api/clocktext','text='+encodeURIComponent(document.getElementById('clockText').value))">Save Text</button>
          </div>
          <p>Tap an emoji to append it, then Save Text. The panel draws these as built-in colour sprites; emoji outside the strip are ignored.</p>
          <label>Pixel heart colour</label>
          <div class="color-row">
            <input type="color" id="heartColor" value="#00d2ff">
            <input type="text" id="heartColorHex" value="#00d2ff" maxlength="7" placeholder="#RRGGBB" style="flex:1;margin:0;">
            <button class="btn btn-primary btn-sm" onclick="saveHeartColor()">Save</button>
          </div>
        </div>

      </div>

      <!-- ══════════════ MEDIA ══════════════ -->
      <div class="section" id="s-media">

        <div class="card">
          <h2>Upload Media</h2>
          <form method="POST" action="/upload" enctype="multipart/form-data" id="uploadForm">
            <label class="upload-zone" for="fileInput">
              <div class="upload-icon">🖼️</div>
              <div id="uploadLabel">Tap to choose a JPEG, PNG, or GIF</div>
              <p>File is saved to internal flash (LittleFS)</p>
              <input type="file" id="fileInput" name="upload" accept=".jpg,.jpeg,.png,.gif" required onchange="document.getElementById('uploadLabel').innerText=this.files[0]?this.files[0].name:'Tap to choose a JPEG, PNG, or GIF'">
            </label>
            <div class="btn-row">
              <button type="submit" class="btn btn-primary">Upload to Flash</button>
            </div>
          </form>
        </div>

        <div class="card">
          <h2>Stored Files</h2>
          <div id="fileList"><p>Loading…</p></div>
        </div>

      </div>

      <!-- ══════════════ LYRICS ══════════════ -->
      <div class="section" id="s-lyrics">
%LYRICS_CARDS%
      </div>

      <!-- ══════════════ SCORES ══════════════ -->
      <div class="section" id="s-scores">
%SCORES_CARDS%
      </div>

      <!-- ══════════════ SETTINGS ══════════════ -->
      <div class="section" id="s-settings">

        <div class="card">
          <h2>Wi-Fi</h2>
          <div class="row"><span>Status</span><span class="value" id="wifiStatus2">–</span></div>
          <div class="row"><span>Driver state</span><span class="value" id="wifiState2">–</span></div>
          <div class="row"><span>IP address</span><span class="value" id="ip2">–</span></div>
          <div class="row"><span>Gateway</span><span class="value" id="gw2">–</span></div>
          <div class="row"><span>Subnet</span><span class="value" id="subnet2">–</span></div>
          <div class="row"><span>Fixed IP in use</span><span class="value" id="staticState2">–</span></div>
          <label>Fixed IP address</label>
          <input type="text" id="staticIp" placeholder="Empty = DHCP (automatic)">
          <div class="btn-row">
            <button class="btn btn-primary btn-sm" onclick="saveStaticIp()">Save &amp; restart</button>
            <button class="btn btn-secondary btn-sm" onclick="clearStaticIp()">Use DHCP</button>
          </div>
          <p class="hint">Empty means DHCP, which is what most networks want. A pinned address keeps the dashboard URL the same after every reboot, but it is only applied when it belongs to the network the device actually joins - the board always takes a lease first, so a pin from another network is skipped rather than leaving the dashboard unreachable.</p>
          <label>SSID</label>
          <input type="text" id="ssid" placeholder="Network name">
          <label>Password</label>
          <input type="password" id="password" placeholder="Password">
          <div class="btn-row">
            <button class="btn btn-primary btn-sm" onclick="postData('/api/wifi','ssid='+encodeURIComponent(document.getElementById('ssid').value)+'&password='+encodeURIComponent(document.getElementById('password').value))">Save</button>
            <button class="btn btn-secondary btn-sm" onclick="postData('/api/connect','')">Connect</button>
          </div>
        </div>

      </div>

    </div><!-- /main-content -->
  </div><!-- /layout -->

  <!-- Bottom nav (mobile) -->
  <nav class="nav" id="bottomNav">
    <button class="nav-btn active" onclick="showSection('s-home',this)" id="nb-home">
      <svg viewBox="0 0 24 24"><path d="M3 9.5L12 3l9 6.5V20a1 1 0 01-1 1H4a1 1 0 01-1-1z"/><polyline points="9 22 9 12 15 12 15 22"/></svg>
      Home
    </button>
    <button class="nav-btn" onclick="showSection('s-display',this)" id="nb-display">
      <svg viewBox="0 0 24 24"><rect x="2" y="3" width="20" height="14" rx="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>
      Display
    </button>
    <button class="nav-btn" onclick="showSection('s-media',this)" id="nb-media">
      <svg viewBox="0 0 24 24"><rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="8.5" cy="8.5" r="1.5"/><polyline points="21 15 16 10 5 21"/></svg>
      Media
    </button>
    <button class="nav-btn" onclick="showSection('s-lyrics',this)" id="nb-lyrics">
      <svg viewBox="0 0 24 24"><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/></svg>
      Lyrics
    </button>
    <button class="nav-btn" onclick="showSection('s-scores',this)" id="nb-scores">
      <svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="M12 8v4l3 3"/></svg>
      Scores
    </button>
  </nav>

  <script>
    // ── Navigation ──────────────────────────────────────────────
    function showSection(id, btn) {
      document.querySelectorAll('.section').forEach(function(s){ s.classList.remove('active'); });
      document.getElementById(id).classList.add('active');
      // mobile bottom nav
      document.querySelectorAll('.nav-btn').forEach(function(b){ b.classList.remove('active'); });
      if (btn) btn.classList.add('active');
      // highlight the matching bottom-nav button for the active section
      var sectionNavMap = {
        's-home':'nb-home','s-display':'nb-display','s-media':'nb-media',
        's-lyrics':'nb-lyrics','s-scores':'nb-scores'
      };
      var nbId = sectionNavMap[id];
      if (nbId) { var nb = document.getElementById(nbId); if(nb) nb.classList.add('active'); }
    }

    // ── Helpers ────────────────────────────────────────────────
    function postData(url, body) {
      fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: body
      }).then(function(){ updateData(); });
    }

    // Pinning the dashboard's own address. The device validates it, then
    // restarts; the page keeps the value in the field so it is not lost while
    // the board is away.
    function saveStaticIp() {
      var ip = document.getElementById('staticIp').value.trim();
      if (ip && !/^\d{1,3}(\.\d{1,3}){3}$/.test(ip)) {
        alert('Enter an IPv4 address like 192.168.1.50, or leave it empty for DHCP.');
        return;
      }
      if (!confirm(ip ? 'Pin the dashboard to ' + ip + '? The device restarts to apply it.'
                      : 'Use DHCP again? The device restarts and its address may change.')) return;
      postData('/api/staticip', 'ip=' + encodeURIComponent(ip));
    }

    function clearStaticIp() {
      document.getElementById('staticIp').value = '';
      saveStaticIp();
    }

    function fmtUptime(s) {
      if (s < 60) return s + 's';
      if (s < 3600) return Math.floor(s/60) + 'm ' + (s%60) + 's';
      return Math.floor(s/3600) + 'h ' + Math.floor((s%3600)/60) + 'm';
    }

    function saveHeartColor() {
      var hex = document.getElementById('heartColorHex').value.trim();
      if (!/^#[0-9a-fA-F]{6}$/.test(hex)) { alert('Enter a colour as #RRGGBB'); return; }
      document.getElementById('heartColor').value = hex;
      postData('/api/heartcolor', 'color=' + encodeURIComponent(hex));
    }
    document.getElementById('heartColor').addEventListener('input', function() {
      document.getElementById('heartColorHex').value = this.value;
    });

    // ── Clock face picker ──────────────────────────────────────
    var CLOCK_FACE_NOTES = {
      'digital':  'Heart, big time and the date. The lyric + heart effects still apply to this face.',
      'analogue': 'Ticked dial with live hour, minute and second hands. The date sits underneath.',
      '8bit':     'Double pixel frame with chunky block digits and a seconds counter.',
      'anime':    'Sparkles, a chibi face and a glowing gradient time.',
      'minimal':  'Clean time with a hairline rule and the date below — no other chrome.'
    };

    function markClockFace(face) {
      if (!face) return;
      var cards = document.querySelectorAll('#faceGrid .face-card');
      for (var i = 0; i < cards.length; i++) {
        cards[i].classList.toggle('active', cards[i].getAttribute('data-face') === face);
      }
      var hint = document.getElementById('faceHint');
      if (hint && CLOCK_FACE_NOTES[face]) hint.innerText = CLOCK_FACE_NOTES[face];
    }

    function setClockStyle(face) {
      postData('/api/clockstyle', 'style=' + encodeURIComponent(face));
      var sel = document.getElementById('clockStyle');
      if (sel) sel.value = face;
      markClockFace(face);
    }

    // ── Status polling ─────────────────────────────────────────
    // A failed poll must be visible: freezing the fields kept a stale reading
    // (e.g. "Disconnected" from a moment when the link was down) on screen and
    // indistinguishable from a live one.
    function setLinkWarning(show) {
      var warn = document.getElementById('connWarn');
      if (warn) warn.style.display = show ? 'block' : 'none';
      if (show) {
        var dot = document.getElementById('wifiDot');
        if (dot) dot.className = 'topbar-dot stale';
      }
    }

    function updateData() {
      fetch('/api/status').then(function(r){ return r.json(); }).then(function(data) {
        setLinkWarning(false);
        document.getElementById('firmware').innerText = data.firmware || '–';
        var ut = fmtUptime(data.uptime || 0);
        document.getElementById('uptime').innerText = ut;
        document.getElementById('chipUptime').innerText = ut;
        var hk = data.heap ? Math.round(data.heap/1024) + ' KB' : '–';
        document.getElementById('heap').innerText = data.heap ? data.heap + ' B' : '–';
        document.getElementById('chipHeap').innerText = hk;
        document.getElementById('chipFw').innerText = data.firmware || '–';

        var connected = data.wifiStatus === 'Connected';
        document.getElementById('wifiStatus').innerText = data.wifiStatus || '–';
        document.getElementById('wifiStatus2').innerText = data.wifiStatus || '–';
        document.getElementById('wifiState2').innerText = data.wifiState || '–';
        document.getElementById('ip').innerText = data.ip || '–';
        document.getElementById('ip2').innerText = data.ip || '–';
        document.getElementById('gw2').innerText = data.gateway || '–';
        document.getElementById('subnet2').innerText = data.subnet || '–';
        var pinStates = { active: 'Yes', 'off-network': 'No - not on this network', dhcp: 'No - DHCP' };
        document.getElementById('staticState2').innerText = pinStates[data.staticIpState] || '–';
        if (document.activeElement !== document.getElementById('staticIp') && data.staticIp !== undefined) {
          document.getElementById('staticIp').value = data.staticIp;
        }
        var dot = document.getElementById('wifiDot');
        dot.className = 'topbar-dot ' + (connected ? 'online' : 'offline');

        if (document.activeElement !== document.getElementById('brightness')) {
          document.getElementById('brightness').value = data.brightness;
          document.getElementById('brightVal').innerText = Math.round(data.brightness/255*100);
        }
        if (!document.getElementById('ssid').value && document.activeElement !== document.getElementById('ssid')) {
          document.getElementById('ssid').value = data.ssid || '';
        }
        if (document.activeElement !== document.getElementById('clockText') && data.clockText !== undefined) {
          document.getElementById('clockText').value = data.clockText;
        }
        if (data.clockStyle !== undefined) {
          if (document.activeElement !== document.getElementById('clockStyle')) {
            document.getElementById('clockStyle').value = data.clockStyle;
          }
          markClockFace(data.clockStyle);
        }
        if (document.activeElement !== document.getElementById('heartColor') && data.heartColor !== undefined) {
          document.getElementById('heartColor').value = data.heartColor;
          document.getElementById('heartColorHex').value = data.heartColor;
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
          try { statusUpdaters[i](data); } catch(e) { console.error(e); }
        }
      // Second argument, not .catch(): a rendering error in the block above is a
      // page bug, and must not be dressed up as "no answer from the device".
      }, function() {
        setLinkWarning(true);
      });
      // NOTE: the media list is NOT re-fetched here. It only changes when a
      // file is uploaded or deleted, and polling it re-listed LittleFS and
      // re-serialised the directory JSON every 4 seconds on the device.
    }

    function loadFiles() {
      fetch('/api/files').then(function(r){ return r.json(); }).then(function(files) {
        var el = document.getElementById('fileList');
        if (!files.length) { el.innerHTML = '<p>No media files uploaded yet.</p>'; return; }
        var html = '';
        files.forEach(function(f) {
          var ext = f.split('.').pop().toLowerCase();
          var icon = (ext==='gif') ? '🎞️' : '🖼️';
          html += '<div class="file-item"><span class="file-name">' + icon + ' ' + f + '</span>' +
            '<button class="btn btn-danger btn-sm" onclick="deleteFile(\'' + f + '\')">Delete</button></div>';
        });
        el.innerHTML = html;
      });
    }

    function deleteFile(name) {
      if (confirm('Delete ' + name + '?')) {
        postData('/api/deletefile', 'filename=' + encodeURIComponent(name));
        setTimeout(loadFiles, 400);  // refresh the list once the delete lands
      }
    }

    // ── Emoji picker ───────────────────────────────────────────
    function insertEmoji(ch) {
      var el = document.getElementById('clockText');
      if (!el) return;
      el.value += ch;
      el.focus();
    }

    markClockFace(document.getElementById('clockStyle').value);
    loadFiles();  // once at load; uploads reload the page and deletes refresh it
    setInterval(updateData, 4000);
    updateData();
  </script>
</body>
</html>
)rawliteral";


// Count non-overlapping occurrences of needle in haystack without allocating.
static size_t countOccurrences(const char* haystack, const char* needle) {
    if (!haystack || !needle || !*needle) return 0;
    const size_t nLen = strlen(needle);
    size_t found = 0;
    for (const char* p = haystack; (p = strstr(p, needle)) != nullptr; p += nLen) {
        found++;
    }
    return found;
}

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
    doc["wifiStatus"] = _wifiManager.getStatusString();
    // The driver's own word, next to the answer above: when a station is up on an
    // address but the driver's flag never latched, this is what makes the
    // difference visible instead of looking like a plain disconnect.
    doc["wifiState"] = _wifiManager.getRawStatusString();
    doc["rssi"] = _wifiManager.getRssi();
    doc["ip"] = _wifiManager.ipAddress();
    // Addressing: the pinned address (empty = DHCP) plus the gateway/subnet a
    // fixed address is built from, so the page can show what will be used.
    doc["staticIp"] = _wifiManager.configuredStaticIp();
    // Whether the stored pin is actually in force here: "active", "off-network"
    // (stored from another network, deliberately skipped) or "dhcp".
    doc["staticIpState"] = _wifiManager.staticIpState();
    doc["gateway"] = _wifiManager.networkGateway();
    doc["subnet"] = _wifiManager.networkSubnet();
    doc["clockText"] = _settings.getClockText();
    doc["clockStyle"] = _settings.getClockStyle();
    doc["theme"] = _settings.getTheme();
    doc["heartColor"] = _settings.getHeartColor();
    doc["lyricColor"] = _settings.getLyricColor();
    doc["lyricFull"] = _settings.getLyricFull();
    doc["lyricScroll"] = _settings.getLyricScroll();
    doc["lyricFullSize"] = _settings.getLyricFullSize();
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
    // The assembled page is large (~57 KB) and String::replace() reallocates the
    // whole thing per substitution, so the growth is computed up front, the
    // buffer is reserved once, and the replaces then fit without reallocating.
    // Otherwise the peak heap need is several times the page size - enough to
    // fail while a live ESPN poller is holding TLS buffers.
    // %DASHBOARD_CSS% is the shared design-system stylesheet (DashboardAssets.h),
    // spliced straight from flash so it never needs a second in-RAM copy; the
    // other four carry the plugin-built fragments.
    static const char* const PLACEHOLDER[5] = {
        "%DASHBOARD_CSS%", "%MODE_OPTIONS%", "%LYRICS_CARDS%", "%SCORES_CARDS%",
        "%EMOJI_PICKER%"
    };

    // Every substitution is assembled first; the page is then reserved and
    // filled in a single allocation (see the reserve further down).
    String modeOptions, lyricsCards, scoresCards, emojiButtons;

    // 1. Mode dropdown from the registered plugins (name + mode number).
    if (_pluginManager) {
        for (uint8_t i = 0; i < _pluginManager->count(); i++) {
            Plugin* p = _pluginManager->at(i);
            modeOptions += "<option value=\"" + String(p->mode()) + "\">" + p->name() + "</option>\n        ";
        }
    }
    // 2. Route each plugin's uiSection() into its section by plugin id.
    //    "lyrics" -> %LYRICS_CARDS%, "scores" -> %SCORES_CARDS%,
    //    anything else appended to lyrics section as fallback.
    if (_pluginManager) {
        for (uint8_t i = 0; i < _pluginManager->count(); i++) {
            Plugin* p = _pluginManager->at(i);
            String section = p->uiSection();
            if (section.length() == 0) continue;
            String pid = String(p->id());
            if (pid == "scores") {
                scoresCards += section + "\n";
            } else {
                // lyrics and any future plugins go into lyrics section
                lyricsCards += section + "\n";
            }
        }
    }
    // Reserve the exact final size (shell + the growth from each substitution)
    // and fill in one allocation: with enough capacity the replaces below never
    // reallocate, so the peak heap need is the page itself rather than several
    // copies of it.
    size_t finalLen = strlen(index_html);
    {
        const String* values[4] = { &modeOptions, &lyricsCards, &scoresCards, &emojiButtons };
        for (uint8_t i = 0; i < 4; i++) {
            const char* ph = PLACEHOLDER[i + 1];   // slot 0 is the stylesheet
            const size_t phLen = strlen(ph);
            const size_t found = countOccurrences(index_html, ph);
            if (found == 0) continue;
            const size_t newLen = values[i]->length();
            // Growth only: a shorter replacement needs no extra room.
            if (newLen > phLen) finalLen += found * (newLen - phLen);
        }
        // The stylesheet is spliced from flash, so only its length grows the page.
        finalLen += countOccurrences(index_html, PLACEHOLDER[0]) *
                    (strlen(DASHBOARD_CSS) - strlen(PLACEHOLDER[0]));
    }
    String html;
    if (!html.reserve(finalLen)) {
        // Out of contiguous heap: report it and let the route answer with its
        // fallback page rather than a blank screen.
        LOG_ERROR(TAG, ("Dashboard build failed: cannot reserve " + String((unsigned)finalLen) +
                        " bytes, free heap " + String((unsigned)ESP.getFreeHeap())).c_str());
        return String();
    }
    html = index_html;
    // Slot 0 first: the stylesheet comes from flash, so this adds no second copy
    // of it to the heap - the reserved buffer already has room for it.
    html.replace("%DASHBOARD_CSS%", DASHBOARD_CSS);
    html.replace("%MODE_OPTIONS%", modeOptions);
    html.replace("%LYRICS_CARDS%", lyricsCards);
    html.replace("%SCORES_CARDS%", scoresCards);

    // 3. Emoji picker: one button per built-in sprite (see EmojiFont.h), so the
    //    strip always matches what the panel can actually draw.
    char utf8[5];
    for (const EmojiFont::Sprite* s = EmojiFont::begin(); s; EmojiFont::next(s)) {
        utf8[EmojiFont::encodeUtf8(s->codepoint, utf8)] = '\0';
        emojiButtons += "<button type=\"button\" class=\"emoji-btn\" title=\"" + String(s->name) +
                        " U+" + String(s->codepoint, HEX) + "\" onclick=\"insertEmoji('" +
                        String(utf8) + "')\">" + String(utf8) + "</button>\n          ";
    }
    html.replace("%EMOJI_PICKER%", emojiButtons);

    LOG_INFO(TAG, ("Dashboard built: " + String(html.length()) + " bytes, free heap " +
                   String((unsigned)ESP.getFreeHeap())).c_str());
    return html;
}

// A route has answered and wants the board restarted. The reply is queued, not
// necessarily on the wire yet, so the restart is deferred to update() - the loop
// keeps running long enough for it to be flushed.
void WebServer::scheduleRestart() {
    _restartAt = millis() + 800;
}

void WebServer::update() {
    if (_restartAt != 0 && (int32_t)(millis() - _restartAt) >= 0) {
        _restartAt = 0;
        LOG_INFO(TAG, "Restarting");
        delay(50);      // let the log line reach the serial port
        ESP.restart();
    }
}

void WebServer::setupRoutes() {
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request){
        String page = buildIndexHtml();
        if (page.length() < 1024) {
            // Assembly failed (see the ERROR log): answer with something the user
            // can act on instead of a blank white page.
            String fallback =
                "<html><head><meta charset=\"utf-8\"><title>ESP32 Dashboard</title></head>"
                "<body style=\"font-family:sans-serif;background:#ece0c8;color:#3d2f21;padding:24px\">"
                "<h2>Dashboard unavailable</h2>"
                "<p>The device could not assemble the dashboard page - it is low on memory.</p>"
                "<p>Free heap: ";
            fallback += String((unsigned)ESP.getFreeHeap());
            fallback += " bytes.</p><p>Restart the device, then reload this page.</p></body></html>";
            request->send(503, "text/html", fallback);
            return;
        }
        // Stream the page out of the String we already hold. The one-shot
        // send() copies it into the response object, which needs a SECOND
        // contiguous allocation the size of the page (~64 KB); when the heap is
        // fragmented that copy silently becomes empty and the browser gets
        // "200 OK, content-length: 0" - a blank dashboard. Chunks copy a few
        // hundred bytes at a time and the page is freed when the last one goes.
        auto pageRef = std::make_shared<String>(std::move(page));
        request->send(request->beginChunkedResponse("text/html",
            [pageRef](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                if (index >= pageRef->length()) return 0;
                size_t n = pageRef->length() - index;
                if (n > maxLen) n = maxLen;
                memcpy(buffer, pageRef->c_str() + index, n);
                return n;
            }));
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

    _server.on("/api/clocktext", HTTP_POST, [this](AsyncWebServerRequest *request){
        if (request->hasParam("text", true)) {
            _settings.setClockText(request->getParam("text", true)->value());
            LOG_INFO(TAG, "Settings saved: Clock text");
        }
        request->send(200, "text/plain", "OK");
    });

    _server.on("/api/clockstyle", HTTP_POST, [this](AsyncWebServerRequest *request){
        if (request->hasParam("style", true)) {
            // Settings::setClockStyle ignores unknown faces.
            _settings.setClockStyle(request->getParam("style", true)->value());
            LOG_INFO(TAG, "Settings saved: Clock style");
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

    // Fixed IP for the dashboard URL. The address is validated against the
    // network the device is on (gateway/subnet come from its DHCP lease) and then
    // the board restarts, so the change is applied from a clean netif state. A
    // value that somehow cannot connect falls back to DHCP on that boot.
    _server.on("/api/staticip", HTTP_POST, [this](AsyncWebServerRequest *request){
        if (!request->hasParam("ip", true)) {
            request->send(400, "text/plain", "Missing ip parameter (send an empty value for DHCP)");
            return;
        }
        String error;
        if (!_wifiManager.setStaticIp(request->getParam("ip", true)->value(), error)) {
            LOG_WARN(TAG, ("Fixed IP rejected: " + error).c_str());
            request->send(400, "text/plain", error);
            return;
        }
        request->send(200, "text/plain", "Saved - restarting to apply");
        scheduleRestart();
    });

    _server.on("/api/restart", HTTP_POST, [this](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "Restarting");
        scheduleRestart();
    });

    _server.on("/api/reset", HTTP_POST, [this](AsyncWebServerRequest *request){
        _settings.clearAll();
        request->send(200, "text/plain", "Factory Reset");
        scheduleRestart();
    });

    // Plugins register their own literal routes (frozen endpoints etc.) last
    // so they win over any future core route on the same path.
    if (_pluginManager) {
        for (uint8_t i = 0; i < _pluginManager->count(); i++) {
            _pluginManager->at(i)->registerRoutes(_server);
        }
    }
}
