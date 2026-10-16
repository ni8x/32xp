#include "LyricsPlugin.h"
#include "../LyricsManager.h"
#include "../Logger.h"
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>

static const char* TAG = "LyricsPlugin";

// Uniform line sizing: prev, current and next all render at the SAME fixed
// font size so the layout never reflows as lyrics change. Overlong lines are
// ellipsized to fit their fixed row.
static constexpr uint8_t LYRIC_LINE_SIZE = 2;
// Full-screen style: commas become line breaks so the whole lyric shows, and
// the text grows to the biggest size that still fits every segment on screen.
// LYRIC_FULL_MAX_SIZE is the hard ceiling; the user's preferred size is stored
// in settings (getLyricFullSize) and the renderer steps down from it to fit.
static constexpr uint8_t LYRIC_FULL_MAX_SIZE = 8;
static constexpr uint8_t LYRIC_FULL_MAX_LINES = 6;
// Fixed ink baselines per orientation (size-2 unifont draws its ink ~28px above
// the given y, so these are tuned so the three rows sit in an even block).
static constexpr int16_t LYRIC_PREV_Y_LANDSCAPE = 66;
static constexpr int16_t LYRIC_CURR_Y_LANDSCAPE = 110;
static constexpr int16_t LYRIC_NEXT_Y_LANDSCAPE = 154;
static constexpr int16_t LYRIC_PREV_Y_PORTRAIT = 116;
static constexpr int16_t LYRIC_CURR_Y_PORTRAIT = 160;
static constexpr int16_t LYRIC_NEXT_Y_PORTRAIT = 204;

// Scroll-up transition: when the lyric advances, the whole row stack is drawn
// one row pitch (nextY - currY) lower and slides up into place over
// LYRIC_SCROLL_MS. The frame at the start of the transition is therefore the
// previous layout shifted down by exactly one row, so the advance reads as a
// scroll rather than a cut. Frames are wall-clock driven: a slow SPI bus costs
// frames, never a stretched-out transition.
static constexpr uint16_t LYRIC_SCROLL_MS = 220;
// Ink box of one row around its baseline y (see Display::drawText): the unifont
// ink rises 14*size px above y and descends 2*size px below it, which is also
// the emoji sprite cell (EMOJI_ASCENT / CELL - EMOJI_ASCENT).
static constexpr int16_t LYRIC_INK_ABOVE = 14;
static constexpr int16_t LYRIC_INK_BELOW = 2;

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
      _lastLyricPrev(""),
      _lastLyricNext(""),
      _scrollActive(false),
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
    _lastLyricPrev = "";
    _lastLyricNext = "";
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
    bool styleScroll = _settings->getLyricScroll();
    uint8_t styleFullSize = _settings->getLyricFullSize();
    if (styleColor != _lastLyricColor || styleGlow != _lastLyricGlow ||
        styleRainbow != _lastLyricRainbow || styleFull != _lastLyricFull ||
        styleGradient != _lastLyricGradient || styleScroll != _lastLyricScroll ||
        styleFullSize != _lastLyricFullSize) {
        _lastLyricColor = styleColor;
        _lastLyricGlow = styleGlow;
        _lastLyricRainbow = styleRainbow;
        _lastLyricFull = styleFull;
        _lastLyricGradient = styleGradient;
        _lastLyricScroll = styleScroll;
        _lastLyricFullSize = styleFullSize;
        clearCache();
    }

    // Redraw when ANY state that affects the screen changes - not just the line.
    // Without this, a phone ping (phoneConnected) or now-playing (active) change
    // would leave the screen frozen on the old idle message.
    // Content changes force a full repaint. Progress ticks every second and
    // must NOT, so it is tracked separately (see the progress-only fast path).
    const uint8_t progressNow = _lyrics->getProgress();
    bool contentChanged =
        _lyrics->getCurrentLine() != _lastLyricLine ||
        _lyrics->getPrevLine() != _lastLyricPrev ||
        _lyrics->getNextLine() != _lastLyricNext ||
        _lyrics->isActive() != _lastLyricActive ||
        _lyrics->isPhoneConnected() != _lastPhoneConnected ||
        _lyrics->getTitle() != _lastLyricTitle;
    bool progressChanged = progressNow != _lastLyricProgress;
    bool stateChanged = contentChanged || progressChanged;

    const String current = _lyrics->getCurrentLine();
    const String prevLine = _lyrics->getPrevLine();
    const String nextLine = _lyrics->getNextLine();
    const bool full = _settings->getLyricFull();
    // Scroll-up transition on a line advance (the dashboard's "Scroll Up" box).
    const bool scrollUp = _settings->getLyricScroll();

    // A line ADVANCE - not the first paint of a line, and not a style/rotation
    // change - is what the scroll-up transition animates. _lastLyricLine still
    // holds the previously drawn line here; the cache is updated further down,
    // once the frame is actually committed.
    const bool lineAdvanced = scrollUp && !full && contentChanged &&
                              current.length() > 0 && _lastLyricLine.length() > 0 &&
                              _lastLyricLine != current;

    bool glow = _settings->getLyricGlow();
    bool rainbow = _settings->getLyricRainbow();
    bool gradient = _settings->getLyricGradient();

    const int16_t dispW = _display->width();
    const int16_t dispH = _display->height();
    const bool landscape = dispW > dispH;

    // Uniform geometry: prev/current/next all share ONE fixed size and fixed
    // row positions, so the layout never reflows. Overlong lines are ellipsized
    // to fit their single row.
    const int16_t prevY = landscape ? LYRIC_PREV_Y_LANDSCAPE : LYRIC_PREV_Y_PORTRAIT;
    const int16_t nextY = landscape ? LYRIC_NEXT_Y_LANDSCAPE : LYRIC_NEXT_Y_PORTRAIT;
    const int16_t baseCurY = landscape ? LYRIC_CURR_Y_LANDSCAPE : LYRIC_CURR_Y_PORTRAIT;
    // Row pitch: the vertical distance between the fixed rows, and therefore the
    // distance the stack travels in one scroll-up transition (see the constants
    // at the top of this file).
    const int16_t rowPitch = nextY - baseCurY;

    uint8_t currSize = LYRIC_LINE_SIZE;
    int16_t currY = baseCurY;

    // Full-screen style: split the line on commas (each becomes its own row) so
    // the whole lyric is shown instead of being ellipsized, then grow the font
    // to the largest size where every segment fits the width and all rows fit
    // the height. No marquee/truncation needed.
    uint8_t nFull = 0;
    String fullSeg[LYRIC_FULL_MAX_LINES];
    if (full && current.length() > 0) {
        int start = 0;
        for (int i = 0; i <= (int)current.length(); i++) {
            if (i == (int)current.length() || current[i] == ',') {
                String seg = current.substring(start, i);
                seg.trim();
                if (seg.length() > 0 && nFull < LYRIC_FULL_MAX_LINES) {
                    fullSeg[nFull++] = seg;
                }
                start = i + 1;
            }
        }
        if (nFull == 0) fullSeg[nFull++] = current; // no usable segment

        uint8_t size = _settings->getLyricFullSize();
        if (size < 1) size = 1;
        if (size > LYRIC_FULL_MAX_SIZE) size = LYRIC_FULL_MAX_SIZE;
        for (; size > 1; size--) {
            bool fits = (int32_t)16 * size * nFull <= (int32_t)dispH - 24;
            if (fits) {
                for (uint8_t k = 0; k < nFull; k++) {
                    if ((int16_t)_display->measureText(fullSeg[k].c_str(), size) > dispW - 20) {
                        fits = false;
                        break;
                    }
                }
            }
            if (fits) break;
        }
        currSize = size;
        currY = (dispH - (int16_t)(16 * currSize * nFull)) / 2;
    }

    const uint32_t now = millis();
    // Rainbow keeps animating the current line's hue; the timestamps above the
    // progress bar tick once per second.
    bool animTick = rainbow && current.length() > 0 && (now - _lastLyricDraw >= 90);
    bool timeTick = !full && _lyrics->isActive() && current.length() > 0 &&
                    _lyrics->getDurationSec() > 0 && (now - _lastLyricDraw >= 1000);

    if (!stateChanged && !animTick && !timeTick) {
        return; // No change, skip redraw
    }
    _lastLyricDraw = now;

    uint16_t lyricColor = Display::hexToRGB565(_settings->getLyricColor());

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

    // Context rows: ellipsized to a single row so they never wrap or overflow.
    // dy shifts the row during the scroll-up transition.
    auto drawPrevLine = [&](int16_t dy) {
        if (prevLine.isEmpty()) return;
        String shown = _display->fitText(prevLine, LYRIC_LINE_SIZE, dispW - 10);
        _display->drawCenteredTextAt(prevY + dy, shown.c_str(), dimColor, LYRIC_LINE_SIZE);
    };
    auto drawNextLine = [&](int16_t dy) {
        if (nextLine.isEmpty()) return;
        String shown = _display->fitText(nextLine, LYRIC_LINE_SIZE, dispW - 10);
        _display->drawCenteredTextAt(nextY + dy, shown.c_str(), dimColor, LYRIC_LINE_SIZE);
    };

    // Draw the current line with its effects. In full-screen style it may have
    // been split across several comma rows; otherwise it is ellipsized to one
    // row. The redraw cache records the drawn bounding box for rainbow refresh.
    auto drawCurrLine = [&](int16_t y) {
        if (current.isEmpty()) return;
        if (full && nFull > 0) {
            const int16_t lineStep = 16 * currSize;
            const int16_t blockTop = (dispH - lineStep * nFull) / 2;
            for (uint8_t k = 0; k < nFull; k++) {
                const int16_t ly = blockTop + k * lineStep;
                if (glow || rainbow || gradient) {
                    _display->drawCenteredTextEffect(ly, fullSeg[k].c_str(), lyricColor, currSize,
                                                     glow, rainbow, (uint8_t)(now / 10),
                                                     nullptr, nullptr, 0, gradient);
                } else {
                    _display->drawCenteredTextAt(ly, fullSeg[k].c_str(), lyricColor, currSize);
                }
            }
            _lastCurrX = 0;
            _lastCurrY = blockTop;
            _lastCurrW = dispW;
            _lastCurrH = lineStep * nFull;
            return;
        }
        String shown = _display->fitText(current, currSize, dispW - 10);
        if (glow || rainbow || gradient) {
            _display->drawCenteredTextEffect(y, shown.c_str(), lyricColor, currSize,
                                             glow, rainbow, (uint8_t)(now / 10),
                                             &_lastCurrX, &_lastCurrW, full ? 0 : 1, gradient);
        } else {
            _display->drawCenteredTextAt(y, shown.c_str(), lyricColor, currSize);
            int16_t w = (int16_t)_display->measureText(shown.c_str(), currSize);
            _lastCurrX = (dispW - w) / 2;
            _lastCurrW = w;
        }
        _lastCurrY = y;
        _lastCurrH = 16 * currSize;
    };

    // --- Scroll-up transition ---
    // The transition repaints the three rows itself while it runs, so the band
    // it clears spans every position they occupy on the way: their final ink box
    // extended down by the travel distance. Its bottom stays clear of the
    // progress bar's timestamp band, which keeps ticking through a scroll.
    const int16_t inkAbove = LYRIC_INK_ABOVE * currSize;
    const int16_t inkBelow = LYRIC_INK_BELOW * currSize;
    const int16_t bandPad = 3 * currSize + 2;   // covers the glow halo
    const int16_t bandTop = (prevLine.length() > 0 && !full ? prevY : currY) - inkAbove - bandPad;
    const int16_t bandLowest = (nextLine.length() > 0 && !full ? nextY : currY) + inkBelow;
    const int16_t bandLimit = dispH - 18 - 15;  // the bar's timestamp band starts here

    auto paintStack = [&](int16_t dy) {
        if (!full) {
            drawPrevLine(dy);
            drawNextLine(dy);
        }
        drawCurrLine(currY + dy);
    };
    auto clearStackBand = [&]() {
        int16_t top = bandTop;
        int16_t bot = bandLowest + _scrollFrom + bandPad;
        if (top < 0) top = 0;
        if (bot > bandLimit) bot = bandLimit;
        if (bot > top) _display->drawFilledRectangle(0, top, dispW, (int16_t)(bot - top), BLACK);
    };

    if (_scrollActive) {
        // A line arriving mid-flight - or anything that changes the layout -
        // abandons the transition and falls through to the normal change path,
        // which repaints from scratch and re-arms the scroll if the line moved.
        const bool stale = full || _lyrics->getCurrentLine() != _lastLyricLine ||
                           _lyrics->getPrevLine() != _lastLyricPrev ||
                           _lyrics->getNextLine() != _lastLyricNext;
        if (stale) {
            _scrollActive = false;
        } else {
            const uint32_t elapsed = now - _scrollStart;
            const bool done = elapsed >= LYRIC_SCROLL_MS;
            // Ease-out: the remaining travel is from * (1 - p)^2, so the stack
            // decelerates into place instead of stopping dead on the last pixel.
            const int32_t inv = done ? 0 : 1000 - (int32_t)(elapsed * 1000 / LYRIC_SCROLL_MS);
            const int16_t dy = done ? 0 : (int16_t)((int32_t)_scrollFrom * inv * inv / 1000000);
            if (done) _scrollActive = false;
            _lastLyricDraw = now;
            // Integer millis repeat the same pixel row for several ticks, and the
            // first tick re-computes exactly the frame the repaint just drew:
            // skipping those avoids an identical erase-and-redraw flicker.
            if (dy == _scrollDy) return;
            _scrollDy = dy;
            clearStackBand();
            paintStack(dy);
            return;
        }
    }

    if (!stateChanged) {
        // Rainbow refresh of the current line: erase just its bounding box (plus
        // glow margin) and redraw it, so the rest of the layout stays steady.
        if (animTick && _lastCurrW > 0) {
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

    _lastLyricLine = current;
    _lastLyricPrev = prevLine;
    _lastLyricNext = nextLine;
    _lastLyricProgress = progressNow;
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

    if (current.length() > 0) {
        // A line advance arms the scroll-up transition: the stack goes up on
        // screen one row pitch low, and the tick above slides it into place.
        const int16_t dy = lineAdvanced ? rowPitch : 0;
        if (lineAdvanced) {
            _scrollActive = true;
            _scrollStart = now;
            _scrollFrom = rowPitch;
            _scrollDy = rowPitch;
        }
        // Context lines are hidden in full-screen style.
        if (!full) {
            drawPrevLine(dy);
            drawNextLine(dy);
        }
        drawCurrLine(currY + dy);
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
        <label style="display:flex; align-items:center; gap:6px; font-weight:normal;">
          <input type="checkbox" id="lyricScroll" onchange="postLyricEffects()"> Scroll Up
        </label>
      </div>
      <p style="color: var(--muted); font-size: 0.85em; margin:0 0 10px;">Effects apply to the current lyric line and can be used together. Scroll Up slides the lines up when the lyric advances (standard layout only).</p>
      <label style="margin-top:8px; display:block;">Full Screen Font Size</label>
      <div style="display:flex; gap:8px; align-items:center; margin-bottom:4px;">
        <input type="number" id="lyricFullSize" min="1" max="8" value="5" style="width:80px; margin-bottom:0;" onchange="postLyricEffects()">
        <span style="color:var(--muted); font-size:0.85em;">1 = small &middot; 8 = largest (auto shrinks to fit the screen)</span>
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
      if (data.lyricGlow === undefined) return;
      document.getElementById('lyricGlow').checked = !!data.lyricGlow;
      document.getElementById('lyricRainbow').checked = !!data.lyricRainbow;
      document.getElementById('lyricGradient').checked = !!data.lyricGradient;
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
      var glow = document.getElementById('lyricGlow').checked ? 1 : 0;
      var rainbow = document.getElementById('lyricRainbow').checked ? 1 : 0;
      var gradient = document.getElementById('lyricGradient').checked ? 1 : 0;
      var full = document.getElementById('lyricFull').checked ? 1 : 0;
      var scroll = document.getElementById('lyricScroll').checked ? 1 : 0;
      var fullsize = document.getElementById('lyricFullSize').value || 5;
      postData('/api/lyriceffect', 'glow=' + glow + '&rainbow=' + rainbow + '&gradient=' + gradient + '&full=' + full + '&scroll=' + scroll + '&fullsize=' + fullsize);
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
