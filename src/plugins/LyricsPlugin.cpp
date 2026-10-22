#include "LyricsPlugin.h"
#include "../LyricsManager.h"
#include "../Logger.h"
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>

static const char* TAG = "LyricsPlugin";

// One uniform size for the whole active line. Keeping the size fixed is what
// makes the screen feel calm: the line wraps instead of shrinking, so advancing
// never reflows the layout. Long, rare lines may step down one size (2 -> 1)
// only when they could not otherwise fit vertically.
static constexpr uint8_t LYRIC_TEXT_MAX_SIZE = 2;
// Maximum wrapped rows for one lyric line. A line needing more than this is far
// longer than any real lyric; extra rows are simply dropped rather than
// overflowing the viewport.
static constexpr uint8_t LYRIC_MAX_ROWS = 16;
// Vertical spacing, in pixels at text size 1: tight between wrapped rows of the
// same line. BLOCK_GAP is the extra travel used when the next line slides in.
static constexpr int16_t LYRIC_ROW_GAP = 2;
static constexpr int16_t LYRIC_BLOCK_GAP = 7;
// Lyrics live between the header rule and the progress-bar timestamp band.
// Full Screen hides that chrome and reclaims the space.
static constexpr int16_t LYRIC_CONTENT_TOP = 28;
static constexpr int16_t LYRIC_CONTENT_BOTTOM = 36;

// Instagram-style viewport transition: the new current line starts where the
// old "next" line was and glides up to the exact vertical centre of the
// viewport. Wall-clock driven, so a slow SPI bus costs frames rather than
// stretching the transition.
static constexpr uint16_t LYRIC_SCROLL_MS = 300;
// Ink box of one row around its baseline y (see Display::drawText): the unifont
// ink rises 14*size px above y and descends 2*size px below it, which is also
// the emoji sprite cell (EMOJI_ASCENT / CELL - EMOJI_ASCENT).
static constexpr int16_t LYRIC_INK_ABOVE = 14;

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
      _lastLyricFull(true),
      _lastLyricScroll(true),
      _lastLyricFullSize(255),
      _lastLyricDraw(0),
      _lastFullRedraw(0),
      _lastLyricProgress(255),
      _lastCurrX(0), _lastCurrY(0), _lastCurrW(0), _lastCurrH(0),
      _lastTsLW(0), _lastTsRW(0),
      _lastLyricActive(false),
      _lastPhoneConnected(false),
      _lastLyricTitle(""),
      _scrollActive(false),
      _scrollPreview(false),
      _scrollStart(0),
      _scrollFrom(0),
      _scrollDy(0) {}

void LyricsPlugin::clearCache() {
    // Any cache reset is followed by a full repaint, so a transition in flight
    // must not keep painting its frames over the new layout.
    _scrollActive = false;
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
    // Style change (color/glow/full via web UI): clear the redraw cache so the
    // screen re-renders with the new style on this tick.
    String styleColor = _settings->getLyricColor();
    bool styleFull = _settings->getLyricFull();
    bool styleScroll = _settings->getLyricScroll();
    uint8_t styleFullSize = _settings->getLyricFullSize();
    if (styleColor != _lastLyricColor ||
        styleFull != _lastLyricFull || styleScroll != _lastLyricScroll ||
        styleFullSize != _lastLyricFullSize) {
        // Enabling the animation plays it once right away; otherwise the toggle
        // would appear to do nothing until the next lyric line arrives.
        const bool previewScroll = styleScroll && !_lastLyricScroll;
        _lastLyricColor = styleColor;
        _lastLyricFull = styleFull;
        _lastLyricScroll = styleScroll;
        _lastLyricFullSize = styleFullSize;
        clearCache();
        _scrollPreview = previewScroll;
    }

    // Redraw when ANY state that affects the screen changes - not just the line.
    // Without this, a phone ping (phoneConnected) or now-playing (active) change
    // would leave the screen frozen on the old idle message.
    // Content changes force a full repaint. Progress ticks every second and
    // must NOT, so it is tracked separately (see the progress-only fast path).
    const uint8_t progressNow = _lyrics->getProgress();
    bool contentChanged =
        _lyrics->getCurrentLine() != _lastLyricLine ||
        _lyrics->isActive() != _lastLyricActive ||
        _lyrics->isPhoneConnected() != _lastPhoneConnected ||
        _lyrics->getTitle() != _lastLyricTitle;
    bool progressChanged = progressNow != _lastLyricProgress;
    bool stateChanged = contentChanged || progressChanged;

    const String current = _lyrics->getCurrentLine();
    const bool full = _settings->getLyricFull();
    // Scroll-up transition on a line advance (the dashboard's "Scroll Up" box).
    const bool scrollUp = _settings->getLyricScroll();
    const bool titleChanged = _lyrics->getTitle() != _lastLyricTitle;

    // Tell a real backward seek from a normal advance. Only a backward jump in
    // playback position suppresses the transition; repeated lyric lines (a
    // chorus is often the exact same text many times) are not seeks.
    const bool seek = current.length() > 0 && current != _lastLyricLine &&
                      _lastLyricProgress != 255 &&
                      progressNow + 2 < _lastLyricProgress;

    // A line ADVANCE - not the first paint of a line, and not a style/rotation
    // change - is what the scroll transition animates. _lastLyricLine still
    // holds the previously drawn line here; the cache is updated further down,
    // once the frame is actually committed.
    const bool lineAdvanced = scrollUp && contentChanged && !titleChanged && !seek &&
                              current.length() > 0 && _lastLyricLine.length() > 0 &&
                              _lastLyricLine != current;
    if (contentChanged && current.length() > 0 && current != _lastLyricLine) {
        LOG_INFO(TAG, ("Anim check advanced=" + String(lineAdvanced ? 1 : 0) +
                       " seek=" + String(seek ? 1 : 0) +
                       " scroll=" + String(scrollUp ? 1 : 0))
                          .c_str());
    }

    const int16_t dispW = _display->width();
    const int16_t dispH = _display->height();
    const bool landscape = dispW > dispH;

    // The lyric block owns everything between the header rule and the progress
    // bar's timestamp band. Full Screen hides that chrome and gives the line the
    // whole panel.
    const int16_t vpTop = full ? 4 : LYRIC_CONTENT_TOP;
    const int16_t vpBottom = full ? dispH - 4 : dispH - LYRIC_CONTENT_BOTTOM;
    const int16_t vpHeight = vpBottom - vpTop;

    // One text size for the whole line. Prefer the user's size; drop to 1 only
    // when the line could not otherwise fit vertically. Lines wrap rather than
    // ellipsize, so nothing is ever cut off.
    uint8_t size = _settings->getLyricFullSize();
    if (size < 1) size = 1;
    if (size > LYRIC_TEXT_MAX_SIZE) size = LYRIC_TEXT_MAX_SIZE;
    int16_t rowPitch = (16 + LYRIC_ROW_GAP) * size;
    int16_t blockGap = LYRIC_BLOCK_GAP * size;

    auto wrapLine = [&](const String& text, String* rows) -> uint8_t {
        return _display->wrapText(text, size, dispW - 16, rows, LYRIC_MAX_ROWS);
    };

    String currRows[LYRIC_MAX_ROWS];
    uint8_t nCurr = 0;
    int16_t currH = 0;
    for (;;) {
        nCurr = current.length() > 0 ? wrapLine(current, currRows) : 0;
        currH = nCurr ? (int16_t)((nCurr - 1) * rowPitch + 16 * size) : 0;
        if (currH <= vpHeight || size <= 1) break;
        size--;
        rowPitch = (16 + LYRIC_ROW_GAP) * size;
        blockGap = LYRIC_BLOCK_GAP * size;
    }
    int16_t currTop = vpTop + (vpHeight - currH) / 2;
    if (currTop < vpTop) currTop = vpTop;

    // How far the block may start below its resting place without pushing its
    // last row past the bottom of the viewport. Without this cap a wrapped line
    // starts partly off-screen, so the end of the sentence only slides into
    // view partway through the transition - which reads as the tail flickering.
    const int16_t roomBelow = vpBottom - (currTop + currH);
    auto clampScroll = [&](int16_t amount) -> int16_t {
        if (amount > roomBelow) amount = roomBelow;
        if (amount < 0) amount = 0;
        return amount;
    };

    const uint32_t now = millis();
    // Timestamps above the progress bar tick once per second.
    const bool timeTick = !full && _lyrics->isActive() && current.length() > 0 &&
                          _lyrics->getDurationSec() > 0 &&
                          (now - _lastLyricDraw >= 1000);

    if (!stateChanged && !timeTick && !_scrollActive) {
        return; // No change, skip redraw
    }
    _lastLyricDraw = now;

    const uint16_t lyricColor = Display::hexToRGB565(_settings->getLyricColor());

    // Chrome colors come from the active theme; the lyric color itself stays
    // user-customizable (and is seeded by the theme when it's selected).
    const Settings::ThemePalette* palette = Settings::getThemePalette(_settings->getTheme());
    uint16_t accent = Display::hexToRGB565(palette->accentHex);
    uint16_t lineColor = Display::hexToRGB565(palette->lineHex);
    uint16_t dimColor = Display::hexToRGB565(palette->dimHex);
    uint16_t warnColor = Display::hexToRGB565(palette->warnHex);
    uint16_t okColor = Display::hexToRGB565(palette->okHex);

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

    // Erase only the previous lyric block. Repainting the whole viewport every
    // animation frame is what caused the visible flicker; touching just the old
    // text band leaves the rest of the panel completely stable.
    auto eraseLast = [&]() {
        if (_lastCurrW <= 0 || _lastCurrH <= 0) return;
        int16_t top = _lastCurrY - 2 * size;
        int16_t bottom = _lastCurrY + _lastCurrH + 2 * size;
        if (top < vpTop) top = vpTop;
        if (bottom > vpBottom) bottom = vpBottom;
        if (bottom > top) {
            _display->drawFilledRectangle(0, top, dispW, bottom - top, BLACK);
        }
    };

    // Current lyric block: one or more wrapped rows at full brightness. Text is
    // drawn plain - no halo, shadow or duplicate context lines. The redraw cache
    // records the block bounds so the next frame erases exactly this area.
    auto drawCurrentRows = [&](int16_t top, bool record) {
        if (nCurr == 0) return;
        for (uint8_t i = 0; i < nCurr; i++) {
            const int16_t rt = top + (int16_t)i * rowPitch;
            if (rt < vpTop) continue;
            if (rt + 16 * size > vpBottom) break;
            const int16_t baseline = rt + LYRIC_INK_ABOVE * size;
            _display->drawCenteredTextAt(baseline, currRows[i].c_str(), lyricColor, size);
        }
        if (record) {
            _lastCurrX = 0;
            _lastCurrY = top;
            _lastCurrW = dispW;
            _lastCurrH = currH;
        }
    };

    // --- Instagram viewport transition ---
    // When the active line changes it starts just below the centre and glides
    // up into the exact vertical centre over LYRIC_SCROLL_MS using a smoothstep
    // curve (ease in and out). Wall-clock driven, so a slow SPI bus costs frames
    // rather than stretching the easing. Only the old and new text bands are
    // touched, so the panel does not flash on every frame.
    if (_scrollActive) {
        // A line arriving mid-flight - or anything that changes the layout -
        // abandons the transition and falls through to the normal change path.
        const bool stale = full != _lastLyricFull ||
                           current != _lastLyricLine ||
                           _lyrics->getTitle() != _lastLyricTitle;
        if (stale) {
            _scrollActive = false;
        } else {
            const uint32_t elapsed = now - _scrollStart;
            const bool done = elapsed >= LYRIC_SCROLL_MS;
            int32_t eased = 1000;
            if (!done) {
                const int32_t p = (int32_t)(elapsed * 1000 / LYRIC_SCROLL_MS);
                const int32_t p2 = p * p / 1000; // p^2, scaled to 0..1000
                eased = p2 * (3000 - 2 * p) / 1000;
            }
            const int16_t dy =
                done ? 0 : (int16_t)((int32_t)_scrollFrom * (1000 - eased) / 1000);
            if (done) _scrollActive = false;
            _lastLyricDraw = now;
            // Integer millis repeat the same pixel row for several ticks; skip
            // those identical frames to avoid an erase-and-redraw flicker.
            if (dy == _scrollDy && !done) return;
            _scrollDy = dy;
            eraseLast();
            drawCurrentRows(currTop + dy, true);
            return;
        }
    }

    if (!stateChanged) {
        // Per-second timestamp tick: refresh just the two readouts above the bar.
        if (timeTick) {
            drawBarAndTimestamps(true);
        }
        return;
    }

    // Progress-only update: refresh just the progress bar, never the whole
    // screen. In full-screen style the bar is hidden, so nothing is drawn.
    if (!contentChanged) {
        _lastLyricProgress = progressNow;
        _lastLyricDraw = now;
        if (!full && _lyrics->isActive() && current.length() > 0) {
            drawBarAndTimestamps(false);
        }
        return;
    }

    // Burst coalescing: a song change fires several state updates back-to-back
    // (now-playing, first line, progress). Wait a short beat so they collapse
    // into ONE full repaint instead of a queue of full-screen redraws, which
    // otherwise reads as lag on the song-change screen.
    if ((uint32_t)(now - _lastFullRedraw) < 70) {
        return;
    }
    _lastFullRedraw = now;

    // A normal lyric advance only repaints the lyric band. Full repaints are
    // reserved for song/status/style changes, so the screen no longer flashes
    // black between every line.
    const bool fullRepaint = titleChanged ||
                             _lastLyricLine.length() == 0 ||
                             _lyrics->isActive() != _lastLyricActive ||
                             _lyrics->isPhoneConnected() != _lastPhoneConnected;

    _lastLyricLine = current;
    _lastLyricProgress = progressNow;
    _lastLyricActive = _lyrics->isActive();
    _lastPhoneConnected = _lyrics->isPhoneConnected();
    _lastLyricTitle = _lyrics->getTitle();

    if (fullRepaint) {
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
        // y=16 not 2: Arduino_GFX draws unifont ink 14px above the given y
        // (glyph height 16 + y-offset -2), so y=2 would clip the top.
        if (!full && header.length() > 0) {
            // A music-note emoji (sprite, see EmojiFont.h) sets the song line off.
            const int16_t iconCell = 16;
            const int16_t iconGap = 5;
            header = _display->fitText(header, 1, dispW - 20 - iconCell - iconGap);
            const int16_t headerW = (int16_t)_display->measureText(header.c_str(), 1);
            const int16_t headerX = (dispW - (headerW + iconGap + iconCell)) / 2;
            _display->drawEmojiCentered(headerX + iconCell / 2, 9, 0x1F3B5, 1);
            _display->drawText(headerX + iconCell + iconGap, 16, header.c_str(), accent, 1);
            _display->drawLine(10, 20, dispW - 10, 20, lineColor);
        }
    } else {
        eraseLast();
    }

    if (current.length() > 0) {
        int16_t dy = 0;
        if (lineAdvanced) {
            // Start the new line just below the centre, then glide it up into
            // place. Only the current line is drawn - no prev/next context.
            _scrollFrom = _lastCurrH + blockGap;
            if (_scrollFrom < rowPitch) _scrollFrom = rowPitch;
            _scrollFrom = clampScroll(_scrollFrom);
            _scrollActive = true;
            _scrollStart = now;
            _scrollDy = _scrollFrom;
            dy = _scrollFrom;
        } else if (_scrollPreview) {
            // Toggle demo: drop the line by one row and glide it back up.
            _scrollPreview = false;
            _scrollFrom = rowPitch + blockGap;
            _scrollFrom = clampScroll(_scrollFrom);
            _scrollActive = true;
            _scrollStart = now;
            _scrollDy = _scrollFrom;
            dy = _scrollFrom;
        }
        drawCurrentRows(currTop + dy, dy == 0);
    } else {
        _scrollPreview = false;
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
        uint32_t posMs = request->hasParam("posms", true) ? (uint32_t)request->getParam("posms", true)->value().toInt() : 0;
        uint32_t lStart = request->hasParam("lstart", true) ? (uint32_t)request->getParam("lstart", true)->value().toInt() : 0;
        uint32_t lEnd = request->hasParam("lend", true) ? (uint32_t)request->getParam("lend", true)->value().toInt() : 0;
        _lyrics->setLine(current, prev, next, prog, posSec, durSec);
        if (lEnd > lStart) _lyrics->setLineTiming(posMs, lStart, lEnd);
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
                uint32_t posMs = data["posms"] | 0;
                uint32_t lStart = data["lstart"] | 0;
                uint32_t lEnd = data["lend"] | 0;
                _lyrics->setLine(current, prev, next, progress);
                if (lEnd > lStart) _lyrics->setLineTiming(posMs, lStart, lEnd);
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
        if (request->hasParam("full", true))
            _settings->setLyricFull(request->getParam("full", true)->value().toInt() != 0);
        if (request->hasParam("scroll", true))
            _settings->setLyricScroll(request->getParam("scroll", true)->value().toInt() != 0);
        if (request->hasParam("fullsize", true))
            _settings->setLyricFullSize((uint8_t)request->getParam("fullsize", true)->value().toInt());
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
      <div class="row"><span>Phone:</span> <span class="value" id="phoneStatus" style="color:var(--muted);">Not connected</span></div>
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
      <p style="color: var(--muted); font-size: 0.85em; margin:0 0 10px;">Pick a swatch or type any custom hex code, e.g. #ff44aa.</p>
      <label style="margin-top:8px; display:block;">Lyrics Style</label>
      <div style="display:flex; gap:24px; margin-bottom:4px;">
        <label style="display:flex; align-items:center; gap:6px; font-weight:normal;">
          <input type="checkbox" id="lyricFull" onchange="postLyricEffects()"> Full Screen
        </label>
        <label style="display:flex; align-items:center; gap:6px; font-weight:normal;">
          <input type="checkbox" id="lyricScroll" onchange="postLyricEffects()"> Scroll Animation
        </label>
      </div>
      <p style="color: var(--muted); font-size: 0.85em; margin:0 0 10px;">Full Screen hides the header and progress bar. Scroll Animation glides the new line up to the centre of the screen when it changes. Only the current line is shown.</p>
      <label style="margin-top:8px; display:block;">Lyrics Text Size</label>
      <div style="display:flex; gap:8px; align-items:center; margin-bottom:4px;">
        <input type="number" id="lyricFullSize" min="1" max="2" value="2" style="width:80px; margin-bottom:0;" onchange="postLyricEffects()">
        <span style="color:var(--muted); font-size:0.85em;">1 = compact &middot; 2 = normal. Long lines wrap and auto-shrink only if needed.</span>
      </div>
      <p style="color: var(--muted); font-size: 0.85em;">Tap TEST in the Android app to verify connection, then START to sync lyrics.</p>
    </div>
  <script>
    function updateLyricsStatus(data) {
      if (data.lyrics === undefined) return;
      document.getElementById('lyricStatus').innerText = data.lyrics.active ? 'Active' : 'Idle';
      var phoneEl = document.getElementById('phoneStatus');
      if (data.lyrics.phoneConnected) {
        phoneEl.innerText = '✅ Connected';
        phoneEl.style.color = 'var(--success)';
      } else {
        phoneEl.innerText = 'Not connected';
        phoneEl.style.color = 'var(--muted)';
      }
      document.getElementById('lyricTitle').innerText = data.lyrics.title || '-';
      document.getElementById('lyricArtist').innerText = data.lyrics.artist || '-';
      document.getElementById('lyricLine').innerText = data.lyrics.currentLine || '-';
      document.getElementById('lyricProgress').innerText = data.lyrics.progress + '%';
    }
    function syncLyricToggles(data) {
      if (data.lyricFull === undefined) return;
      document.getElementById('lyricFull').checked = !!data.lyricFull;
      document.getElementById('lyricScroll').checked = !!data.lyricScroll;
      if (data.lyricFullSize !== undefined &&
          document.activeElement !== document.getElementById('lyricFullSize')) {
        document.getElementById('lyricFullSize').value = data.lyricFullSize;
      }
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
      var full = document.getElementById('lyricFull').checked ? 1 : 0;
      var scroll = document.getElementById('lyricScroll').checked ? 1 : 0;
      var fullsize = document.getElementById('lyricFullSize').value || 2;
      postData('/api/lyriceffect', 'full=' + full + '&scroll=' + scroll + '&fullsize=' + fullsize);
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
