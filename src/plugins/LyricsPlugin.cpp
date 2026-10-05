#include "LyricsPlugin.h"
#include "../LyricsManager.h"
#include "../Logger.h"
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>

static const char* TAG = "LyricsPlugin";

// Uniform line sizing: prev, current and next all render at the SAME fixed
// font size so the layout never reflows as lyrics change. Overflow is
// ellipsized by fitText (never wrapped).
static constexpr uint8_t LYRIC_LINE_SIZE = 2;
// Fixed ink baselines per orientation (size-2 unifont draws its ink ~28px above
// the given y, so these are tuned so the three rows sit in an even block).
static constexpr int16_t LYRIC_PREV_Y_LANDSCAPE = 66;
static constexpr int16_t LYRIC_CURR_Y_LANDSCAPE = 110;
static constexpr int16_t LYRIC_NEXT_Y_LANDSCAPE = 154;
static constexpr int16_t LYRIC_PREV_Y_PORTRAIT = 116;
static constexpr int16_t LYRIC_CURR_Y_PORTRAIT = 160;
static constexpr int16_t LYRIC_NEXT_Y_PORTRAIT = 204;

// "m:ss" (e.g. 245 -> "4:05") for the progress-bar timestamps.
static String fmtTime(uint32_t sec) {
    uint32_t m = sec / 60;
    uint32_t s = sec % 60;
    String r = String(m) + ":";
    if (s < 10) r += "0";
    r += String(s);
    return r;
}

LyricsPlugin::LyricsPlugin(Display* display, Settings* settings, LyricsManager* lyrics)
    : _display(display), _settings(settings), _lyrics(lyrics),
      _lastLyricLine(""),
      _lastLyricColor(""),
      _lastLyricGlow(false),
      _lastLyricRainbow(false),
      _lastLyricFull(false),
      _lastLyricGradient(false),
      _lastLyricDraw(0),
      _lastLyricProgress(255),
      _lastCurrX(0), _lastCurrY(0), _lastCurrW(0), _lastCurrH(0),
      _lastTsLW(0), _lastTsRW(0),
      _lastLyricActive(false),
      _lastPhoneConnected(false),
      _lastLyricTitle("") {}

void LyricsPlugin::clearCache() {
    _lastLyricLine = "";
    _lastLyricProgress = 255;
    _lastLyricActive = false;
    _lastPhoneConnected = false;
    _lastLyricTitle = "";
}

void LyricsPlugin::enter() {
    clearCache();
}

void LyricsPlugin::onRotationChange() {
    clearCache();
}

void LyricsPlugin::onThemeChange() {
    // Theme seeds the lyric color; clearing the cache forces a full redraw
    // with the new palette on the next update().
    clearCache();
}

void LyricsPlugin::update() {
    drawScreen();
}

void LyricsPlugin::drawScreen() {
    // Style change (color/glow/rainbow/full via web UI): clear the redraw
    // cache so the screen re-renders with the new style on this tick.
    String styleColor = _settings->getLyricColor();
    bool styleGlow = _settings->getLyricGlow();
    bool styleRainbow = _settings->getLyricRainbow();
    bool styleFull = _settings->getLyricFull();
    bool styleGradient = _settings->getLyricGradient();
    if (styleColor != _lastLyricColor || styleGlow != _lastLyricGlow ||
        styleRainbow != _lastLyricRainbow || styleFull != _lastLyricFull ||
        styleGradient != _lastLyricGradient) {
        _lastLyricColor = styleColor;
        _lastLyricGlow = styleGlow;
        _lastLyricRainbow = styleRainbow;
        _lastLyricFull = styleFull;
        _lastLyricGradient = styleGradient;
        clearCache();
    }

    // Redraw when ANY state that affects the screen changes - not just the line.
    // Without this, a phone ping (phoneConnected) or now-playing (active) change
    // would leave the screen frozen on the old idle message.
    bool stateChanged =
        _lyrics->getCurrentLine() != _lastLyricLine ||
        _lyrics->getProgress() != _lastLyricProgress ||
        _lyrics->isActive() != _lastLyricActive ||
        _lyrics->isPhoneConnected() != _lastPhoneConnected ||
        _lyrics->getTitle() != _lastLyricTitle;

    String current = _lyrics->getCurrentLine();
    const bool full = _settings->getLyricFull();

    // Rainbow needs continuous redraws to animate. Glow alone is static, so it
    // never forces periodic redraws (no flicker); and when the rainbow animates,
    // only the current-line region is refreshed (see below), never the whole
    // screen.
    bool glow = _settings->getLyricGlow();
    bool rainbow = _settings->getLyricRainbow();
    bool gradient = _settings->getLyricGradient();
    bool animTick = rainbow && current.length() > 0 && (millis() - _lastLyricDraw >= 90);

    // Timestamps above the progress bar tick once per second (only when a
    // duration is known). Like the rainbow tick, this refreshes just the two
    // small readouts instead of the whole screen.
    bool timeTick = !full && _lyrics->isActive() && current.length() > 0 &&
                    _lyrics->getDurationSec() > 0 && (millis() - _lastLyricDraw >= 1000);

    if (!stateChanged && !animTick && !timeTick) {
        return; // No change, skip redraw
    }
    _lastLyricDraw = millis();

    const int16_t dispW = _display->width();
    const int16_t dispH = _display->height();
    const bool landscape = dispW > dispH;
    uint16_t lyricColor = Display::hexToRGB565(_settings->getLyricColor());

    // Chrome colors come from the active theme; the lyric color itself stays
    // user-customizable (and is seeded by the theme when it's selected).
    const Settings::ThemePalette* palette = Settings::getThemePalette(_settings->getTheme());
    uint16_t accent = Display::hexToRGB565(palette->accentHex);
    uint16_t lineColor = Display::hexToRGB565(palette->lineHex);
    uint16_t dimColor = Display::hexToRGB565(palette->dimHex);
    uint16_t warnColor = Display::hexToRGB565(palette->warnHex);
    uint16_t okColor = Display::hexToRGB565(palette->okHex);

    // Current-line geometry (shared by the full redraw and the anim tick).
    uint8_t currSize = 1;
    int16_t currY = 0;
    String curr = "";
    if (current.length() > 0) {
        if (full) {
            // Full-screen style: biggest size that fits (3, then 2, then 1).
            currSize = (_display->measureText(current.c_str(), 3) <= (uint16_t)(dispW - 40)) ? 3
                     : (_display->measureText(current.c_str(), 2) <= (uint16_t)(dispW - 40)) ? 2 : 1;
            currY = (dispH - (int16_t)(16 * currSize)) / 2;
        } else {
            // Uniform style: prev/current/next share ONE fixed size and fixed
            // row positions, so the layout never changes as lines change. Long
            // lines are ellipsized by fitText, never wrapped.
            currSize = LYRIC_LINE_SIZE;
            currY = landscape ? LYRIC_CURR_Y_LANDSCAPE : LYRIC_CURR_Y_PORTRAIT;
        }
        curr = _display->fitText(current, currSize, dispW - 20);
    }

    // Pixelated bar + timestamp readouts (7x13 font). tickOnly=true erases just
    // the previous timestamp readouts (per-second refresh); otherwise the whole
    // bar redraws.
    auto drawBarAndTimestamps = [&](bool tickOnly) {
        const int16_t barX = 20;
        const int16_t barY = dispH - 18;
        const int16_t barW = dispW - 40;
        const int16_t barH = 8;
        const int16_t block = 6; // pixel block size
        const int16_t gap = 2;   // gap between blocks
        const int16_t pitch = block + gap;
        // Timestamp ink sits ABOVE the given y (u8g2 baseline), so drawing at
        // barY-4 parks the digits just above the bar; the 15px erase band
        // (barY-15..barY-1, also above the bar) covers the whole 7x13 glyph cell.
        const int16_t tsY = barY - 4;
        const int16_t tsEraseY = barY - 15;
        const int16_t tsEraseH = 15;
        uint8_t progress = _lyrics->getProgress();
        if (tickOnly) {
            // Per-second tick: drawPixelText already paints each glyph cell with
            // a black background in the same SPI pass, so the old digits are
            // erased in place without a black flash (which the eye reads as a
            // blink). Only the leftover tail of a narrower string needs a clear.
            String tEl = fmtTime(_lyrics->getElapsedSec());
            int16_t nw = (int16_t)_display->measurePixelText(tEl.c_str());
            if (nw < _lastTsLW) {
                _display->drawFilledRectangle(barX + nw, tsEraseY, _lastTsLW - nw + 1, tsEraseH, BLACK);
            }
            _display->drawPixelText(barX, tsY, tEl.c_str(), lyricColor);
            _lastTsLW = nw;
            return; // duration never changes; leave the right side untouched
        } else {
            _display->drawRectangle(barX, barY, barW, barH, RGB565(40, 40, 40));
            int16_t n = barW / pitch;              // total block slots
            int16_t filled = (progress * n) / 100; // blocks lit
            for (int16_t i = 0; i < filled && i < n; i++) {
                _display->drawFilledRectangle(barX + i * pitch, barY + 1, block, barH - 2, lyricColor);
            }
        }
        if (_lyrics->getDurationSec() > 0) {
            String tEl = fmtTime(_lyrics->getElapsedSec());
            String tDur = fmtTime(_lyrics->getDurationSec());
            _display->drawPixelText(barX, tsY, tEl.c_str(), lyricColor);
            _lastTsLW = (int16_t)_display->measurePixelText(tEl.c_str());
            int16_t tdw = (int16_t)_display->measurePixelText(tDur.c_str());
            _display->drawPixelText(barX + barW - tdw, tsY, tDur.c_str(), RGB565(120, 120, 120));
            _lastTsRW = tdw;
        } else {
            _lastTsLW = 0;
            _lastTsRW = 0;
        }
    };

    // Draw just the current lyric line (with effects) at a given Y. When
    // outX/outW are given, the drawn line's bounding box is returned so the
    // animation tick can erase exactly that region.
    auto drawCurrLine = [&](int16_t y, int16_t* outX = nullptr, int16_t* outW = nullptr) {
        if (curr.isEmpty()) return;
        if (glow || rainbow || gradient) {
            // ~10ms per hue step -> one full rainbow sweep every ~2.5s
            uint8_t hueBase = (uint8_t)(millis() / 10);
            // On the busy non-full screen the glow is one step dimmer so it
            // reads as a halo instead of blinding the surrounding lines.
            uint8_t glowDim = full ? 0 : 1;
            _display->drawCenteredTextEffect(y, curr.c_str(), lyricColor, currSize,
                                             glow, rainbow, hueBase, outX, outW, glowDim, gradient);
        } else {
            _display->drawCenteredTextAt(y, curr.c_str(), lyricColor, currSize);
            if (outX || outW) {
                int16_t w = (int16_t)_display->measureText(curr.c_str(), currSize);
                if (outX) *outX = (dispW - w) / 2;
                if (outW) *outW = w;
            }
        }
    };

    if (!stateChanged) {
        // Rainbow animation: only the current line changes, so erase just its
        // bounding box (plus glow margin) and redraw it. The rest of the
        // screen is untouched -> no visible flicker.
        if (current.length() > 0 && _lastCurrW > 0) {
            int16_t pad = 3 * currSize + 2; // covers the glow halo margin
            _display->drawFilledRectangle(_lastCurrX - pad, _lastCurrY - pad,
                                          _lastCurrW + 2 * pad, _lastCurrH + 2 * pad, BLACK);
            drawCurrLine(_lastCurrY);
        }
        // Per-second timestamp tick: refresh just the two readouts above the bar.
        if (timeTick) {
            drawBarAndTimestamps(true);
        }
        return;
    }

    _lastLyricLine = current;
    _lastLyricProgress = _lyrics->getProgress();
    _lastLyricActive = _lyrics->isActive();
    _lastPhoneConnected = _lyrics->isPhoneConnected();
    _lastLyricTitle = _lyrics->getTitle();

    _display->fillScreen(BLACK);

    // --- Header: song info at top ---
    String header = "";
    if (_lyrics->getTitle().length() > 0) {
        header = _lyrics->getTitle();
        if (_lyrics->getArtist().length() > 0) {
            header += " - " + _lyrics->getArtist();
        }
    }
    // Header (song title) is hidden in full-screen style for a clean look.
    // y=16 not 2: Arduino_GFX draws unifont ink 14px above the given y (glyph
    // height 16 + y-offset -2), so y=2 would clip the top of the text.
    if (!full && header.length() > 0) {
        header = _display->fitText(header, 1, dispW - 20);
        _display->drawCenteredTextAt(16, header.c_str(), accent, 1);
        _display->drawLine(10, 20, dispW - 10, 20, lineColor);
    }

    if (current.length() > 0) {
        // Context lines are hidden in full-screen style.
        if (!full) {
            String prev = _lyrics->getPrevLine();
            if (prev.length() > 0) {
                prev = _display->fitText(prev, currSize, dispW - 20);
                _display->drawCenteredTextAt(landscape ? LYRIC_PREV_Y_LANDSCAPE : LYRIC_PREV_Y_PORTRAIT,
                                             prev.c_str(), dimColor, currSize);
            }
            String nxt = _lyrics->getNextLine();
            if (nxt.length() > 0) {
                nxt = _display->fitText(nxt, currSize, dispW - 20);
                _display->drawCenteredTextAt(landscape ? LYRIC_NEXT_Y_LANDSCAPE : LYRIC_NEXT_Y_PORTRAIT,
                                             nxt.c_str(), dimColor, currSize);
            }
        }

        // Remember where the current line was drawn so anim ticks can erase it.
        drawCurrLine(currY, &_lastCurrX, &_lastCurrW);
        _lastCurrY = currY;
        _lastCurrH = 16 * currSize;
    } else {
        _lastCurrW = 0; // nothing to partially redraw
        if (_lyrics->isActive()) {
            // Song known but no active line yet
            _display->drawCenteredTextAt(landscape ? 96 : 150, "Waiting for", warnColor, 1);
            _display->drawCenteredTextAt(landscape ? 114 : 168, "lyrics...", warnColor, 1);
        } else if (_lyrics->isPhoneConnected()) {
            // Phone connected but no active lyrics yet
            _display->drawCenteredTextAt(landscape ? 72 : 140, "Lyrics Mode", dimColor, 2);
            _display->drawCenteredTextAt(landscape ? 116 : 175, "Phone Connected", okColor, 1);
            _display->drawCenteredTextAt(landscape ? 134 : 193, "Waiting for lyrics...", warnColor, 1);
        } else {
            // Idle state - phone not connected
            _display->drawCenteredTextAt(landscape ? 72 : 140, "Lyrics Mode", dimColor, 2);
            _display->drawCenteredTextAt(landscape ? 116 : 175, "Connect Android app", dimColor, 1);
            _display->drawCenteredTextAt(landscape ? 134 : 193, "to send lyrics", dimColor, 1);
        }
    }

    // --- Pixelated progress bar + timestamps (hidden in full-screen style) ---
    if (!full && _lyrics->isActive() && current.length() > 0) {
        drawBarAndTimestamps(false);
    }

    // Active indicator dot (hidden in full-screen style)
    if (!full && _lyrics->isActive()) {
        _display->drawCircle(dispW - 8, dispH - 8, 3, okColor);
    }
}

// === Web API ===

void LyricsPlugin::registerRoutes(AsyncWebServer& server) {
    // === Lyrics API Endpoints (for Android app) — FROZEN, byte-identical ===

    server.on("/api/lyrics/line", HTTP_POST, [this](AsyncWebServerRequest* request){
        if (!request->hasParam("current", true)) {
            LOG_ERROR(TAG, "Lyrics line request missing current parameter");
            request->send(400, "text/plain", "Missing current lyric line");
            return;
        }
        String current = request->getParam("current", true)->value();
        String prev = request->hasParam("prev", true) ? request->getParam("prev", true)->value() : "";
        String next = request->hasParam("next", true) ? request->getParam("next", true)->value() : "";
        uint8_t prog = request->hasParam("progress", true) ? (uint8_t)request->getParam("progress", true)->value().toInt() : 0;
        uint32_t posSec = request->hasParam("pos", true) ? (uint32_t)request->getParam("pos", true)->value().toInt() : 0;
        uint32_t durSec = request->hasParam("dur", true) ? (uint32_t)request->getParam("dur", true)->value().toInt() : 0;
        _lyrics->setLine(current, prev, next, prog, posSec, durSec);
        LOG_INFO(TAG, ("Lyric line received: " + current).c_str());
        request->send(200, "text/plain", "OK");
    });

    server.on("/api/lyrics/nowplaying", HTTP_POST, [this](AsyncWebServerRequest* request){
        if (request->hasParam("artist", true) && request->hasParam("title", true)) {
            String artist = request->getParam("artist", true)->value();
            String title = request->getParam("title", true)->value();
            _lyrics->setNowPlaying(artist, title);
            LOG_INFO(TAG, ("Now Playing from app: " + title + " - " + artist).c_str());
        }
        request->send(200, "text/plain", "OK");
    });

    server.on("/api/lyrics/clear", HTTP_POST, [this](AsyncWebServerRequest* request){
        _lyrics->clear();
        LOG_INFO(TAG, "Lyrics cleared");
        request->send(200, "text/plain", "OK");
    });

    server.on("/api/lyrics/stop", HTTP_POST, [this](AsyncWebServerRequest* request){
        _lyrics->stop();
        LOG_INFO(TAG, "Lyrics stopped");
        request->send(200, "text/plain", "OK");
    });

    server.on("/api/lyrics/ping", HTTP_POST, [this](AsyncWebServerRequest* request){
        _lyrics->setPhoneConnected(true);
        LOG_INFO(TAG, "Phone ping received");
        request->send(200, "text/plain", "OK");
    });

    server.on("/api/lyrics/test", HTTP_POST, [this](AsyncWebServerRequest* request){
        if (!request->hasParam("text", true)) {
            request->send(400, "text/plain", "Missing text");
            return;
        }
        String text = request->getParam("text", true)->value();
        _lyrics->setLine(text);
        LOG_INFO(TAG, ("Display test text received: " + text).c_str());
        request->send(200, "text/plain", "Displayed");
    });

    // JSON handler for the web dashboard's "Show Test Text" (the app itself
    // uses form-encoded POSTs to /api/lyrics/line, which parse reliably).
    auto* jsonHandler = new AsyncCallbackJsonWebHandler("/api/lyrics/update",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
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
                _lyrics->setLine(current, prev, next, progress);
                LOG_INFO(TAG, ("Lyric line received via JSON: " + current).c_str());
            } else if (type == "song") {
                String artist = data["artist"] | "";
                String title = data["title"] | "";
                if (artist.isEmpty() || title.isEmpty()) {
                    request->send(400, "text/plain", "Missing artist or title");
                    return;
                }
                _lyrics->setNowPlaying(artist, title);
                LOG_INFO(TAG, ("Now playing received via JSON: " + title).c_str());
            } else {
                request->send(400, "text/plain", "Unsupported lyrics update");
                return;
            }
            request->send(200, "text/plain", "OK");
        });
    server.addHandler(jsonHandler);

    // === Lyric styling (web dashboard) ===
    // The dashboard's color/effect controls have always posted to these paths,
    // but nothing registered them, so the toggles silently did nothing.
    // They live with the plugin now that lyrics owns its own screen.
    server.on("/api/lyriccolor", HTTP_POST, [this](AsyncWebServerRequest* request){
        if (request->hasParam("color", true)) {
            _settings->setLyricColor(request->getParam("color", true)->value());
            LOG_INFO(TAG, "Lyric color saved");
        }
        request->send(200, "text/plain", "OK");
    });

    server.on("/api/lyriceffect", HTTP_POST, [this](AsyncWebServerRequest* request){
        if (request->hasParam("glow", true))
            _settings->setLyricGlow(request->getParam("glow", true)->value().toInt() != 0);
        if (request->hasParam("rainbow", true))
            _settings->setLyricRainbow(request->getParam("rainbow", true)->value().toInt() != 0);
        if (request->hasParam("full", true))
            _settings->setLyricFull(request->getParam("full", true)->value().toInt() != 0);
        if (request->hasParam("gradient", true))
            _settings->setLyricGradient(request->getParam("gradient", true)->value().toInt() != 0);
        LOG_INFO(TAG, "Lyric effects saved");
        request->send(200, "text/plain", "OK");
    });
}

bool LyricsPlugin::handleApi(AsyncWebServerRequest* request, const String& action) {
    // No generic /api/plugin/lyrics/<action> endpoints yet; reserved for
    // future plugin-specific controls (e.g. /api/plugin/lyrics/style).
    (void)request; (void)action;
    return false;
}

void LyricsPlugin::buildStatusJson(JsonObject obj) {
    obj["active"] = _lyrics->isActive();
    obj["phoneConnected"] = _lyrics->isPhoneConnected();
    obj["title"] = _lyrics->getTitle();
    obj["artist"] = _lyrics->getArtist();
    obj["currentLine"] = _lyrics->getCurrentLine();
    obj["progress"] = _lyrics->getProgress();
}

String LyricsPlugin::uiSection() const {
    // Styling/classes reuse the dashboard's existing CSS (".card", ".row",
    // ".value", ".lyric-line"); the script block wires the live status poll.
    return R"rawliteral(
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
      <br><br>
      <label>Lyric Text Color</label>
      <div style="display:flex; gap:8px; align-items:center; margin-bottom:6px;">
        <input type="color" id="lyricColor" value="#00ffff" style="height:40px; padding:2px; flex:1; min-width:50px;">
        <input type="text" id="lyricColorHex" value="#00ffff" maxlength="7" placeholder="#RRGGBB" style="flex:2; margin-bottom:0;">
        <button onclick="saveLyricColor()">Save</button>
      </div>
      <p style="color: #888; font-size: 0.85em; margin:0 0 10px;">Pick a swatch or type any custom hex code, e.g. #ff44aa.</p>
      <label style="margin-top:8px; display:block;">Lyrics Style</label>
      <div style="display:flex; gap:24px; margin-bottom:4px;">
        <label style="display:flex; align-items:center; gap:6px; font-weight:normal;">
          <input type="checkbox" id="lyricGlow" onchange="postLyricEffects()"> Glow
        </label>
        <label style="display:flex; align-items:center; gap:6px; font-weight:normal;">
          <input type="checkbox" id="lyricRainbow" onchange="postLyricEffects()"> Rainbow
        </label>
        <label style="display:flex; align-items:center; gap:6px; font-weight:normal;">
          <input type="checkbox" id="lyricGradient" onchange="postLyricEffects()"> Gradient
        </label>
        <label style="display:flex; align-items:center; gap:6px; font-weight:normal;">
          <input type="checkbox" id="lyricFull" onchange="postLyricEffects()"> Full Screen (hide prev/next)
        </label>
      </div>
      <p style="color: #888; font-size: 0.85em; margin:0 0 10px;">Effects apply to the current lyric line and can be used together.</p>
      <p style="color: #888; font-size: 0.85em;">Tap TEST in the Android app to verify connection, then START to sync lyrics.</p>
    </div>
  <script>
    function updateLyricsStatus(data) {
      if (data.lyrics === undefined) return;
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
    function syncLyricToggles(data) {
      if (data.lyricGlow === undefined) return;
      document.getElementById('lyricGlow').checked = !!data.lyricGlow;
      document.getElementById('lyricRainbow').checked = !!data.lyricRainbow;
      document.getElementById('lyricGradient').checked = !!data.lyricGradient;
      document.getElementById('lyricFull').checked = !!data.lyricFull;
    }
    function saveLyricColor() {
      var hex = document.getElementById('lyricColorHex').value.trim();
      if (!/^#[0-9a-fA-F]{6}$/.test(hex)) { alert('Enter a color as #RRGGBB'); return; }
      postData('/api/lyriccolor', 'color=' + encodeURIComponent(hex));
    }
    document.getElementById('lyricColor').addEventListener('input', function() {
      document.getElementById('lyricColorHex').value = this.value;
    });
    function postLyricEffects() {
      var glow = document.getElementById('lyricGlow').checked ? 1 : 0;
      var rainbow = document.getElementById('lyricRainbow').checked ? 1 : 0;
      var gradient = document.getElementById('lyricGradient').checked ? 1 : 0;
      var full = document.getElementById('lyricFull').checked ? 1 : 0;
      postData('/api/lyriceffect', 'glow=' + glow + '&rainbow=' + rainbow + '&gradient=' + gradient + '&full=' + full);
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
    registerStatusUpdater(updateLyricsStatus);
    registerStatusUpdater(syncLyricToggles);
  </script>
    )rawliteral";
}
