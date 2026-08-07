#include "App.h"
#include "Config.h"
#include "Logger.h"

static const char* TAG = "App";

// "m:ss" (e.g. 245 -> "4:05") for the progress-bar timestamps.
static String fmtTime(uint32_t sec) {
    uint32_t m = sec / 60;
    uint32_t s = sec % 60;
    String r = String(m) + ":";
    if (s < 10) r += "0";
    r += String(s);
    return r;
}

App::App() 
    : _wifiManager(_settings), 
      _mediaManager(_display, _settings),
      _webServer(_settings, _wifiManager, _mediaManager, _lyricsManager),
      _weatherManager(_settings, _wifiManager),
      _wasWifiConnected(false),
      _wasTimeSynced(false),
      _lastDisplayMode(255),
      _lastRotation(255),
      _lastTheme("default"),
      _lastClockUpdate(0),
      _clockScreenValid(false),
      _lastClockDate(""),
      _lastClockCustom(""),
      _lastClockTimeSize(0),
      _lastClockTimeY(0),
      _lastHeartColor(0),
      _lastLyricLine(""),
      _lastLyricColor(""),
      _lastLyricGlow(false),
      _lastLyricRainbow(false),
      _lastLyricFull(false),
      _lastLyricDraw(0),
      _lastLyricProgress(255),
      _lastCurrX(0),
      _lastCurrY(0),
      _lastCurrW(0),
      _lastCurrH(0),
      _lastTsLW(0),
      _lastTsRW(0),
      _lastLyricActive(false),
      _lastPhoneConnected(false),
      _lastLyricTitle(""),
      _lastBrightness(255) {
}

App::~App() {
}

void App::init() {
    Logger::init();
    LOG_INFO(TAG, "Starting firmware initialization...");
    LOG_INFO(TAG, Config::APP_NAME);
    LOG_INFO(TAG, Config::FIRMWARE_VERSION);

    _settings.init();
    _lastTheme = _settings.getTheme();
    _display.init(_settings.getBrightness());
    _display.setRotation(_settings.getRotation());
    _lastRotation = _settings.getRotation();
    _lastBrightness = _settings.getBrightness();

    // Default credentials are only written on first boot (no saved SSID yet),
    // so credentials saved later via the web UI are never overwritten.
    if (_settings.getWifiSSID().isEmpty()) {
        _settings.setWifiSSID("YOUR_WIFI_SSID");
        _settings.setWifiPassword("YOUR_WIFI_PASSWORD");
    }

    _wifiManager.begin();
    _mediaManager.begin();
    _webServer.begin();
    _timeManager.begin();
    _weatherManager.begin();

    drawDefaultScreen();

    LOG_INFO(TAG, "Firmware initialization complete");
}

void App::drawLyricsScreen() {
    // Redraw when ANY state that affects the screen changes - not just the line.
    // Without this, a phone ping (phoneConnected) or now-playing (active) change
    // would leave the screen frozen on the old idle message.
    bool stateChanged =
        _lyricsManager.getCurrentLine() != _lastLyricLine ||
        _lyricsManager.getProgress() != _lastLyricProgress ||
        _lyricsManager.isActive() != _lastLyricActive ||
        _lyricsManager.isPhoneConnected() != _lastPhoneConnected ||
        _lyricsManager.getTitle() != _lastLyricTitle;

    String current = _lyricsManager.getCurrentLine();
    const bool full = _settings.getLyricFull();

    // Rainbow needs continuous redraws to animate. Glow alone is static, so it
    // never forces periodic redraws (no flicker); and when the rainbow animates,
    // only the current-line region is refreshed (see below), never the whole
    // screen.
    bool glow = _settings.getLyricGlow();
    bool rainbow = _settings.getLyricRainbow();
    bool animTick = rainbow && current.length() > 0 && (millis() - _lastLyricDraw >= 90);

    // Timestamps above the progress bar tick once per second (only when a
    // duration is known). Like the rainbow tick, this refreshes just the two
    // small readouts instead of the whole screen.
    bool timeTick = !full && _lyricsManager.isActive() && current.length() > 0 &&
                    _lyricsManager.getDurationSec() > 0 && (millis() - _lastLyricDraw >= 1000);

    if (!stateChanged && !animTick && !timeTick) {
        return; // No change, skip redraw
    }
    _lastLyricDraw = millis();

    const int16_t dispW = _display.width();
    const int16_t dispH = _display.height();
    const bool landscape = dispW > dispH;
    uint16_t lyricColor = Display::hexToRGB565(_settings.getLyricColor());

    // Chrome colors come from the active theme; the lyric color itself stays
    // user-customizable (and is seeded by the theme when it's selected).
    const Settings::ThemePalette* palette = Settings::getThemePalette(_settings.getTheme());
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
            currSize = (_display.measureText(current.c_str(), 3) <= (uint16_t)(dispW - 40)) ? 3
                     : (_display.measureText(current.c_str(), 2) <= (uint16_t)(dispW - 40)) ? 2 : 1;
            currY = (dispH - (int16_t)(16 * currSize)) / 2;
        } else {
            currSize = (_display.measureText(current.c_str(), 2) <= (uint16_t)(dispW - 40)) ? 2 : 1;
            currY = landscape ? 110 : 160;
        }
        curr = _display.fitText(current, currSize, dispW - 20);
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
        uint8_t progress = _lyricsManager.getProgress();
        if (tickOnly) {
            // Per-second tick: drawPixelText already paints each glyph cell with
            // a black background in the same SPI pass, so the old digits are
            // erased in place without a black flash (which the eye reads as a
            // blink). Only the leftover tail of a narrower string needs a clear.
            String tEl = fmtTime(_lyricsManager.getElapsedSec());
            int16_t nw = (int16_t)_display.measurePixelText(tEl.c_str());
            if (nw < _lastTsLW) {
                _display.drawFilledRectangle(barX + nw, tsEraseY, _lastTsLW - nw + 1, tsEraseH, BLACK);
            }
            _display.drawPixelText(barX, tsY, tEl.c_str(), lyricColor);
            _lastTsLW = nw;
            return; // duration never changes; leave the right side untouched
        } else {
            _display.drawRectangle(barX, barY, barW, barH, RGB565(40, 40, 40));
            int16_t n = barW / pitch;              // total block slots
            int16_t filled = (progress * n) / 100; // blocks lit
            for (int16_t i = 0; i < filled && i < n; i++) {
                _display.drawFilledRectangle(barX + i * pitch, barY + 1, block, barH - 2, lyricColor);
            }
        }
        if (_lyricsManager.getDurationSec() > 0) {
            String tEl = fmtTime(_lyricsManager.getElapsedSec());
            String tDur = fmtTime(_lyricsManager.getDurationSec());
            _display.drawPixelText(barX, tsY, tEl.c_str(), lyricColor);
            _lastTsLW = (int16_t)_display.measurePixelText(tEl.c_str());
            int16_t tdw = (int16_t)_display.measurePixelText(tDur.c_str());
            _display.drawPixelText(barX + barW - tdw, tsY, tDur.c_str(), RGB565(120, 120, 120));
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
        if (glow || rainbow) {
            // ~10ms per hue step -> one full rainbow sweep every ~2.5s
            uint8_t hueBase = (uint8_t)(millis() / 10);
            // On the busy non-full screen the glow is one step dimmer so it
            // reads as a halo instead of blinding the surrounding lines.
            uint8_t glowDim = full ? 0 : 1;
            _display.drawCenteredTextEffect(y, curr.c_str(), lyricColor, currSize,
                                            glow, rainbow, hueBase, outX, outW, glowDim);
        } else {
            _display.drawCenteredTextAt(y, curr.c_str(), lyricColor, currSize);
            if (outX || outW) {
                int16_t w = (int16_t)_display.measureText(curr.c_str(), currSize);
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
            _display.drawFilledRectangle(_lastCurrX - pad, _lastCurrY - pad,
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
    _lastLyricProgress = _lyricsManager.getProgress();
    _lastLyricActive = _lyricsManager.isActive();
    _lastPhoneConnected = _lyricsManager.isPhoneConnected();
    _lastLyricTitle = _lyricsManager.getTitle();

    _display.fillScreen(BLACK);

    // --- Header: Song info at top ---
    String header = "";
    if (_lyricsManager.getTitle().length() > 0) {
        header = _lyricsManager.getTitle();
        if (_lyricsManager.getArtist().length() > 0) {
            header += " - " + _lyricsManager.getArtist();
        }
    }
    // Header (song title) is hidden in full-screen style for a clean look.
    // y=16 not 2: Arduino_GFX draws unifont ink 14px above the given y (glyph
    // height 16 + y-offset -2), so y=2 would clip the top of the text.
    if (!full && header.length() > 0) {
        header = _display.fitText(header, 1, dispW - 20);
        _display.drawCenteredTextAt(16, header.c_str(), accent, 1);
        _display.drawLine(10, 20, dispW - 10, 20, lineColor);
    }

    if (current.length() > 0) {
        // Context lines are hidden in full-screen style.
        if (!full) {
            String prev = _lyricsManager.getPrevLine();
            if (prev.length() > 0) {
                prev = _display.fitText(prev, 1, dispW - 20);
                _display.drawCenteredTextAt(landscape ? 64 : 110, prev.c_str(), dimColor, 1);
            }
            String nxt = _lyricsManager.getNextLine();
            if (nxt.length() > 0) {
                nxt = _display.fitText(nxt, 1, dispW - 20);
                _display.drawCenteredTextAt(landscape ? 156 : 210, nxt.c_str(), dimColor, 1);
            }
        }

        // Remember where the current line was drawn so anim ticks can erase it.
        drawCurrLine(currY, &_lastCurrX, &_lastCurrW);
        _lastCurrY = currY;
        _lastCurrH = 16 * currSize;
    } else {
        _lastCurrW = 0; // nothing to partially redraw
        if (_lyricsManager.isActive()) {
            // Song known but no active line yet
            _display.drawCenteredTextAt(landscape ? 96 : 150, "Waiting for", warnColor, 1);
            _display.drawCenteredTextAt(landscape ? 114 : 168, "lyrics...", warnColor, 1);
        } else if (_lyricsManager.isPhoneConnected()) {
            // Phone connected but no active lyrics yet
            _display.drawCenteredTextAt(landscape ? 72 : 140, "Lyrics Mode", dimColor, 2);
            _display.drawCenteredTextAt(landscape ? 116 : 175, "Phone Connected", okColor, 1);
            _display.drawCenteredTextAt(landscape ? 134 : 193, "Waiting for lyrics...", warnColor, 1);
        } else {
            // Idle state - phone not connected
            _display.drawCenteredTextAt(landscape ? 72 : 140, "Lyrics Mode", dimColor, 2);
            _display.drawCenteredTextAt(landscape ? 116 : 175, "Connect Android app", dimColor, 1);
            _display.drawCenteredTextAt(landscape ? 134 : 193, "to send lyrics", dimColor, 1);
        }
    }

    // --- Pixelated progress bar + timestamps (hidden in full-screen style) ---
    if (!full && _lyricsManager.isActive() && current.length() > 0) {
        drawBarAndTimestamps(false);
    }

    // Active indicator dot (hidden in full-screen style)
    if (!full && _lyricsManager.isActive()) {
        _display.drawCircle(dispW - 8, dispH - 8, 3, okColor);
    }
}

void App::drawDefaultScreen() {
    _display.fillScreen(BLACK);
    
    const int16_t dispW = _display.width();
    const bool landscape = dispW > _display.height();

    const Settings::ThemePalette* palette = Settings::getThemePalette(_settings.getTheme());
    uint16_t accent = Display::hexToRGB565(palette->accentHex);
    uint16_t okColor = Display::hexToRGB565(palette->okHex);
    uint16_t warnColor = Display::hexToRGB565(palette->warnHex);
    uint16_t urlColor = Display::hexToRGB565(palette->urlHex);

    // Header
    if (landscape) {
        // Top-anchor the title in landscape: drawCenteredText would vertically
        // center it in the short 240px frame and collide with the content.
        // y=30 not 6: unifont ink renders 14*size px ABOVE the given y, so a
        // small y clips the top of the text off-screen.
        _display.drawCenteredTextAt(30, "DEVICE DASHBOARD", accent, 2);
        _display.drawLine(10, 40, dispW - 10, 40, okColor);
    } else {
        _display.drawCenteredText("DEVICE DASHBOARD", accent, 2);
        _display.drawLine(10, 30, dispW - 10, 30, okColor);
    }

    if (landscape) {
        // Landscape (320x240): stack the sections more tightly since there is
        // less vertical room, but the URL still fits at size 2 on the wide axis.
        // Y values already account for unifont's ink rendering 14*size px above
        // the given y (e.g. size-2 text at y=70 occupies rows 42..73).
        _display.drawText(10, 70, "Wi-Fi Status:", accent, 2);
        String statusStr = _wifiManager.getStatusString();
        uint16_t statusColor = _wifiManager.isConnected() ? okColor : RED;
        _display.drawText(10, 100, statusStr.c_str(), statusColor, 2);

        _display.drawText(10, 140, "Web Dashboard:", accent, 2);
        if (_wifiManager.isConnected()) {
            String url = "http://" + _wifiManager.ipAddress();
            uint8_t urlSize = (url.length() <= 28) ? 2 : 1;
            _display.drawText(10, 170, url.c_str(), urlColor, urlSize);

            _display.drawText(10, 196, "Open in phone browser", WHITE, 1);
            _display.drawText(10, 214, "to control display", WHITE, 1);
        } else {
            _display.drawText(10, 170, "Waiting for Wi-Fi...", warnColor, 1);
            _display.drawText(10, 196, "Web address will appear", WHITE, 1);
            _display.drawText(10, 214, "once connected", WHITE, 1);
        }
    } else {
        // Wi-Fi Section
        _display.drawText(10, 50, "Wi-Fi Status:", accent, 2);
        
        String statusStr = _wifiManager.getStatusString();
        uint16_t statusColor = _wifiManager.isConnected() ? okColor : RED;
        _display.drawText(10, 80, statusStr.c_str(), statusColor, 2);

        // Web Dashboard section - the URL to open in a browser
        _display.drawText(10, 130, "Web Dashboard:", accent, 2);

        if (_wifiManager.isConnected()) {
            String url = "http://" + _wifiManager.ipAddress();
            // Use big text only when it fits (max ~19 chars at size 2), else fall back
            uint8_t urlSize = (url.length() <= 19) ? 2 : 1;
            _display.drawCenteredTextAt(165, url.c_str(), urlColor, urlSize);

            _display.drawText(10, 205, "Open in phone browser", WHITE, 1);
            _display.drawText(10, 222, "to control display", WHITE, 1);
        } else {
            _display.drawCenteredTextAt(165, "Waiting for Wi-Fi...", warnColor, 1);
            _display.drawText(10, 205, "Web address will appear", WHITE, 1);
            _display.drawText(10, 222, "once connected", WHITE, 1);
        }
    }
}

void App::drawClockScreen() {
    const int16_t dispW = _display.width();
    const int16_t dispH = _display.height();
    const bool landscape = dispW > dispH;

    // Theme colors: time digits + custom-text accent follow the active theme.
    // The heart follows the theme palette too, unless the user picked a custom
    // heart color - that explicit choice always wins.
    const Settings::ThemePalette* palette = Settings::getThemePalette(_settings.getTheme());
    uint16_t timeColor = Display::hexToRGB565(palette->timeHex);
    uint16_t accent = Display::hexToRGB565(palette->accentHex);
    uint16_t heartColor = Display::hexToRGB565(_settings.isHeartColorCustom()
        ? _settings.getHeartColor()
        : palette->heartHex);

    String timeStr = _timeManager.getFormattedTime();
    String dateStr = _timeManager.getFormattedDate();
    String customText = _settings.getClockText();

    // Full repaint only on the first draw (boot / rotation / theme / mode
    // change) or when the date or custom text changed. Every other second just
    // the time band is repainted, so the clock no longer flashes black.
    bool fullDraw = !_clockScreenValid ||
                    dateStr != _lastClockDate ||
                    customText != _lastClockCustom ||
                    heartColor != _lastHeartColor; // web heart color change

    if (fullDraw) {
        _display.fillScreen(BLACK);
    } else if (_lastClockTimeSize > 0) {
        // Erase the previous time region. The time string is monospace with a
        // fixed width per size, so a full-width band covers the old digits.
        int16_t bandTop = _lastClockTimeY - 14 * _lastClockTimeSize - 2;
        int16_t bandH = 16 * _lastClockTimeSize + 8;
        _display.drawFilledRectangle(0, bandTop, dispW, bandH, BLACK);
    }

    if (landscape) {
        // Landscape (320x240): vertically balanced stack with the pixel heart
        // centered on top, the big time centered under it, then the date and
        // custom text at the bottom.
        if (fullDraw) {
            int16_t heartX = (dispW - 32) / 2; // scale 4 = 32x28px
            _display.drawPixelHeart(heartX, 20, heartColor, 4);
        }

        uint8_t timeSize = 3;
        int16_t timeY = 20 + 28 + 18 + 14 * timeSize; // clears the heart, then ink above y
        _display.drawCenteredTextAt(timeY, timeStr.c_str(), timeColor, timeSize);

        if (fullDraw) {
            int16_t dateY = timeY + 60; // below the time ink + gap
            int16_t dateW = _display.measureText(dateStr.c_str(), 2);
            int16_t dateX = (dispW - dateW) / 2;
            _display.drawText(dateX, dateY, dateStr.c_str(), WHITE, 2);

            if (customText.length() > 0) {
                int16_t cw = _display.measureText(customText.c_str(), 2);
                _display.drawText((dispW - cw) / 2, dispH - 24, customText.c_str(), accent, 2);
            }
        }
        _lastClockTimeSize = timeSize;
        _lastClockTimeY = timeY;
    } else {
        // --- Portrait (240x320): heart above time, date + custom text below ---
        if (fullDraw) {
            // Large pixelated heart centered above time (scale 4 = 32x28px)
            int16_t heartX = (dispW - (8 * 4)) / 2;
            _display.drawPixelHeart(heartX, 20, heartColor, 4);
        }

        uint8_t timeSize = 4;
        int16_t timeY = (dispH - (int16_t)(16 * timeSize)) / 2;
        _display.drawCenteredTextAt(timeY, timeStr.c_str(), timeColor, timeSize);

        if (fullDraw) {
            int16_t dateY = (dispH / 2) + 40;
            int16_t dateWidth = _display.measureText(dateStr.c_str(), 2);
            int16_t dateX = (dispW - dateWidth) / 2;
            _display.drawText(dateX, dateY, dateStr.c_str(), WHITE, 2);

            if (customText.length() > 0) {
                int16_t textWidth = _display.measureText(customText.c_str(), 2);
                int16_t textX = (dispW - textWidth) / 2;
                // +36: date glyphs are 32px tall at size 2 (8x16 unifont scaled x2)
                _display.drawText(textX, dateY + 36, customText.c_str(), accent, 2);
            }
        }
        _lastClockTimeSize = timeSize;
        _lastClockTimeY = timeY;
    }

    _lastClockDate = dateStr;
    _lastClockCustom = customText;
    _lastHeartColor = heartColor;
    _clockScreenValid = true;
}

void App::update() {
    _wifiManager.update();
    _timeManager.update();
    _weatherManager.update();
    _mediaManager.processRequests();

    uint32_t now = millis();

    uint8_t displayMode = _settings.getDisplayMode();

    // Orientation change (via web UI): apply immediately and force a redraw of
    // the active screen so it re-lays out for the new dimensions.
    uint8_t rotation = _settings.getRotation();
    if (rotation != _lastRotation) {
        _display.setRotation(rotation);
        _lastRotation = rotation;
        LOG_INFO(TAG, ("Display rotation changed to: " + String(rotation)).c_str());
        if (displayMode == 1) {
            _mediaManager.reset();
        } else if (displayMode == 2) {
            _lastLyricLine = "";
            _lastLyricProgress = 255;
            _lastLyricActive = false;
            _lastPhoneConnected = false;
            _lastLyricTitle = "";
        } else {
            _wasTimeSynced = false;
            _lastClockUpdate = 0;
            _clockScreenValid = false; // full repaint with the new layout/colors
            if (!_timeManager.isTimeSynced()) {
                drawDefaultScreen();
            }
        }
    }

    // Theme change (via web UI): re-render the active screen with the new
    // palette right away. The lyric cache also picks up the freshly seeded
    // lyric color below, but the chrome colors need a forced redraw.
    String theme = _settings.getTheme();
    if (theme != _lastTheme) {
        _lastTheme = theme;
        LOG_INFO(TAG, ("Theme changed to: " + theme).c_str());
        if (displayMode == 1) {
            _mediaManager.reset();
        } else if (displayMode == 2) {
            _lastLyricLine = "";
            _lastLyricProgress = 255;
            _lastLyricActive = false;
            _lastPhoneConnected = false;
            _lastLyricTitle = "";
        } else {
            _wasTimeSynced = false;
            _lastClockUpdate = 0;
            _clockScreenValid = false; // full repaint with the new layout/colors
            if (!_timeManager.isTimeSynced()) {
                drawDefaultScreen();
            }
        }
    }

    // Lyric style change (color/glow/rainbow/full via web UI): clear the lyric
    // redraw cache so the screen re-renders with the new style on THIS loop.
    String lyricColor = _settings.getLyricColor();
    bool lyricGlow = _settings.getLyricGlow();
    bool lyricRainbow = _settings.getLyricRainbow();
    bool lyricFull = _settings.getLyricFull();
    if (lyricColor != _lastLyricColor || lyricGlow != _lastLyricGlow ||
        lyricRainbow != _lastLyricRainbow || lyricFull != _lastLyricFull) {
        _lastLyricColor = lyricColor;
        _lastLyricGlow = lyricGlow;
        _lastLyricRainbow = lyricRainbow;
        _lastLyricFull = lyricFull;
        _lastLyricLine = "";
        _lastLyricProgress = 255;
        _lastLyricActive = false;
        _lastPhoneConnected = false;
        _lastLyricTitle = "";
    }

    if (displayMode != _lastDisplayMode) {
        if (displayMode == 1) {
            LOG_INFO(TAG, "Entering media view");
            _mediaManager.reset();
        } else if (displayMode == 2) {
            LOG_INFO(TAG, "Entering lyrics view");
            _lastLyricLine = "";
            _lastLyricProgress = 255;
            _lastLyricActive = false;
            _lastPhoneConnected = false;
            _lastLyricTitle = "";
        } else {
            LOG_INFO(TAG, "Leaving media/lyrics view");
            _mediaManager.stop();
            _wasTimeSynced = false;
            _clockScreenValid = false;
        }
        _lastDisplayMode = displayMode;
    }

    if (displayMode == 1) {
        // Mode 1: Media Slideshow & GIF Playback
        _mediaManager.update();
    } else if (displayMode == 2) {
        // Mode 2: Lyrics Display Mode
        drawLyricsScreen();
    } else {
        // Mode 0: Clock & Weather Dashboard Mode
        bool isSynced = _timeManager.isTimeSynced();

        if (isSynced) {
            if (!_wasTimeSynced) {
                LOG_INFO(TAG, "Switching to Clock View");
                _wasTimeSynced = true;
                _lastClockUpdate = now - 1000;
            }

            if (now - _lastClockUpdate >= 1000) {
                drawClockScreen();
                _lastClockUpdate = now;
            }
        } else {
            static String lastStatus = "";
            String currentStatus = _wifiManager.getStatusString();

            if (currentStatus != lastStatus) {
                drawDefaultScreen();
                lastStatus = currentStatus;
            }
        }
    }

    // Apply brightness only when it actually changed (avoids redundant IO every loop)
    uint8_t brightness = _settings.getBrightness();
    if (brightness != _lastBrightness) {
        _display.setBrightness(brightness);
        _lastBrightness = brightness;
    }
}
