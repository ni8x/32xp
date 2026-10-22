#include "App.h"
#include "Config.h"
#include "Logger.h"
#include <math.h>

static const char* TAG = "App";

App::App() 
    : _wifiManager(_settings), 
      _mediaManager(_display, _settings),
      _webServer(_settings, _wifiManager, _mediaManager, &_pluginManager),
      _timeManager(),
      _lyricsManager(),
      _clockPlugin(this, &_timeManager),
      _mediaPlugin(&_mediaManager),
      _lyricsPlugin(&_display, &_settings, &_lyricsManager),
      _scorePlugin(&_display, &_settings, &_wifiManager),
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
      _lastClockStyle(""),
      _lastTimeX(0), _lastTimeY(0), _lastTimeW(0), _lastTimeH(0),
      _lastHourAng(-1.0f), _lastMinAng(-1.0f), _lastSecAng(-1.0f),
      _handsValid(false),
      _lastHeartColor(0),
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

    // One plugin per screen mode; registration order is also the order of the
    // dashboard's mode dropdown.
    _pluginManager.registerPlugin(&_clockPlugin);
    _pluginManager.registerPlugin(&_mediaPlugin);
    _pluginManager.registerPlugin(&_lyricsPlugin);
    _pluginManager.registerPlugin(&_scorePlugin);

    _wifiManager.begin();
    _mediaManager.begin();
    _webServer.begin();
    _timeManager.begin();

    // Bring up the plugins and activate the persisted display mode.
    _pluginManager.beginAll();
    _pluginManager.enterMode(_settings.getDisplayMode());

    drawDefaultScreen();

    LOG_INFO(TAG, "Firmware initialization complete");
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
        // small y clips the top of the text.
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
    uint16_t lineColor = Display::hexToRGB565(palette->lineHex);
    uint16_t heartColor = Display::hexToRGB565(_settings.isHeartColorCustom()
        ? _settings.getHeartColor()
        : palette->heartHex);

    String timeStr = _timeManager.getFormattedTime();
    String dateStr = _timeManager.getFormattedDate();
    String customText = _settings.getClockText();
    const String style = _settings.getClockStyle();
    const int16_t cx = dispW / 2;

    // Full repaint only on the first draw (boot / rotation / theme / mode /
    // clock-face change) or when the date or custom text changed. Every other
    // second only the moving part is repainted, so the clock never flashes.
    bool fullDraw = !_clockScreenValid ||
                    dateStr != _lastClockDate ||
                    customText != _lastClockCustom ||
                    style != _lastClockStyle ||
                    heartColor != _lastHeartColor; // web heart color change

    if (fullDraw) {
        _display.fillScreen(BLACK);
    }

    if (style == "analogue") {
        // ---------- Analogue face ----------
        const int16_t R = landscape ? 86 : 96;
        const int16_t fcy = landscape ? 110 : 138;
        if (fullDraw) {
            _display.drawCircle(cx, fcy, R, accent);
            _display.drawCircle(cx, fcy, R - 1, accent);
            _display.drawCircle(cx, fcy, R - 4, lineColor);
            for (int i = 0; i < 12; i++) {
                const float a = (float)i * 6.2831853f / 12.0f;
                const float sa = sinf(a), ca = cosf(a);
                const int16_t rIn = (i % 3 == 0) ? (R - 22) : (R - 14);
                _display.drawLine(cx + (int16_t)(sa * (R - 6)), fcy - (int16_t)(ca * (R - 6)),
                                  cx + (int16_t)(sa * rIn), fcy - (int16_t)(ca * rIn),
                                  (i % 3 == 0) ? accent : lineColor);
            }
            const uint8_t dSize = landscape ? 1 : 2;
            const int16_t dy = dispH - (landscape ? 22 : 30);
            const String d = _timeManager.getFormattedDateShort();
            const int16_t dw = (int16_t)_display.measureText(d.c_str(), dSize);
            _display.drawText(cx - dw / 2, dy, d.c_str(), WHITE, dSize);
            _handsValid = false; // fresh face: nothing to erase
        }

        // Hands: angles measured from 12 o'clock, clockwise. Erase the previous
        // pose first (hands never reach the tick ring, so this is exact).
        uint8_t hh = 0, mm = 0, ss = 0;
        const bool haveTime = _timeManager.getHMS(hh, mm, ss);
        const float hAng = haveTime ? ((hh % 12) + mm / 60.0f + ss / 3600.0f) * 6.2831853f / 12.0f : 0.0f;
        const float mAng = haveTime ? (mm + ss / 60.0f) * 6.2831853f / 60.0f : 0.0f;
        const float sAng = haveTime ? (ss * 6.2831853f / 60.0f) : 0.0f;
        auto hand = [&](float ang, float len, uint8_t thick, uint16_t col) {
            const float s = sinf(ang), c = cosf(ang);
            const int16_t x1 = cx + (int16_t)(s * len);
            const int16_t y1 = fcy - (int16_t)(c * len);
            const int16_t half = thick / 2;
            for (int16_t k = -half; k <= half; k++) {
                const int16_t ox = (int16_t)(-c * k); // offset perpendicular
                const int16_t oy = (int16_t)(-s * k);
                _display.drawLine(cx + ox, fcy + oy, x1 + ox, y1 + oy, col);
            }
        };
        if (_handsValid) {
            hand(_lastHourAng, R - 50, 3, BLACK);
            hand(_lastMinAng, R - 34, 2, BLACK);
            hand(_lastSecAng, R - 28, 1, BLACK);
        }
        hand(hAng, R - 50, 3, timeColor);
        hand(mAng, R - 34, 2, accent);
        hand(sAng, R - 28, 1, RGB565(220, 70, 70));
        for (int16_t hr = 1; hr <= 4; hr++) _display.drawCircle(cx, fcy, hr, accent);
        _lastHourAng = hAng;
        _lastMinAng = mAng;
        _lastSecAng = sAng;
        _handsValid = true;

    } else if (style == "8bit") {
        // ---------- 8-bit face ----------
        if (fullDraw) {
            const int16_t m = 8;
            _display.drawRectangle(m, m, dispW - 2 * m, dispH - 2 * m, accent);
            _display.drawRectangle(m + 4, m + 4, dispW - 2 * (m + 4), dispH - 2 * (m + 4), lineColor);
            const String d = _timeManager.getFormattedDateShort();
            const int16_t dw = (int16_t)_display.measurePixelText(d.c_str(), 2);
            _display.drawPixelText(cx - dw / 2, dispH - 58, d.c_str(), lineColor, 2);
            // (custom text is drawn once, below, for every non-digital face)
        }
        const uint8_t dsize = landscape ? 6 : 5;
        const String hhmm = _timeManager.getFormattedTimeShort();
        const int16_t tw = (int16_t)_display.measurePixelText(hhmm.c_str(), dsize);
        const int16_t tx = cx - tw / 2;
        const int16_t ty = dispH / 2 + (13 * dsize) / 2 - 8;
        // The opaque pixel cells erase the previous digits in the same pass.
        _display.drawPixelText(tx, ty, hhmm.c_str(), timeColor, dsize);
        const String secs = timeStr.substring(6); // "SS" from "HH:MM:SS"
        const int16_t sw = (int16_t)_display.measurePixelText(secs.c_str(), 2);
        _display.drawPixelText(cx - sw / 2, ty + 20, secs.c_str(), accent, 2);

    } else if (style == "anime") {
        // ---------- Anime (cute) face ----------
        auto sparkle = [&](int16_t sx, int16_t sy, uint16_t col) {
            _display.drawFilledRectangle(sx - 1, sy - 4, 3, 9, col);
            _display.drawFilledRectangle(sx - 4, sy - 1, 9, 3, col);
        };
        if (fullDraw) {
            sparkle(cx - dispW / 4, dispH / 5, accent);
            sparkle(cx + dispW / 4, dispH / 4, accent);
            sparkle(cx - dispW / 3, dispH / 2 - 10, lineColor);
            sparkle(cx + dispW / 3 - 6, dispH / 2 + 30, lineColor);
            // Chibi face: heart eyes and a smile. The eyes are the built-in
            // heart emoji sprite (U+2764) - see EmojiFont.h.
            const int16_t eyeY = landscape ? 58 : 86;
            const int16_t eyeDx = 22;
            _display.drawEmojiCentered(cx - eyeDx, eyeY, 0x2764, 1);
            _display.drawEmojiCentered(cx + eyeDx, eyeY, 0x2764, 1);
            _display.drawLine(cx - 14, eyeY + 18, cx - 7, eyeY + 24, timeColor);
            _display.drawLine(cx - 7, eyeY + 24, cx + 7, eyeY + 24, timeColor);
            _display.drawLine(cx + 7, eyeY + 24, cx + 14, eyeY + 18, timeColor);
        }
        // Big glowing gradient time, refreshed every tick.
        const uint8_t tsize = 4;
        const String hhmm = _timeManager.getFormattedTimeShort();
        const int16_t ty = landscape ? 190 : 240;
        if (!fullDraw && _lastTimeW > 0) {
            const int16_t pad = 3 * tsize + 2;
            _display.drawFilledRectangle(_lastTimeX - pad, _lastTimeY - pad,
                                         _lastTimeW + 2 * pad, _lastTimeH + 2 * pad, BLACK);
        }
        int16_t ox = 0, ow = 0;
        _display.drawCenteredTextEffect(ty, hhmm.c_str(), timeColor, tsize,
                                        true, false, 0, &ox, &ow, 1, true);
        _lastTimeX = ox;
        _lastTimeY = ty - 14 * tsize;
        _lastTimeW = ow;
        _lastTimeH = 16 * tsize;
        if (fullDraw) {
            const String d = _timeManager.getFormattedDateShort();
            const int16_t dw = (int16_t)_display.measureText(d.c_str(), 1);
            _display.drawText(cx - dw / 2, dispH - 24, d.c_str(), lineColor, 1);
            // (custom text is drawn once, below, for every non-digital face)
        }

    } else if (style == "minimal") {
        // ---------- Minimal face ----------
        const uint8_t tsize = landscape ? 5 : 4;
        const String hhmm = _timeManager.getFormattedTimeShort();
        const int16_t tw = (int16_t)_display.measureText(hhmm.c_str(), tsize);
        const int16_t tx = cx - tw / 2;
        const int16_t ty = dispH / 2 + 6 * tsize;
        if (!fullDraw && _lastTimeW > 0) {
            _display.drawFilledRectangle(_lastTimeX, _lastTimeY, _lastTimeW, _lastTimeH, BLACK);
        }
        _display.drawText(tx, ty, hhmm.c_str(), timeColor, tsize);
        _lastTimeX = tx - 2;
        _lastTimeY = ty - 14 * tsize - 2;
        _lastTimeW = tw + 4;
        _lastTimeH = 16 * tsize + 6;
        if (fullDraw) {
            _display.drawLine(tx - 12, ty + 10, tx + tw + 12, ty + 10, lineColor);
            const String d = _timeManager.getFormattedDateShort();
            const int16_t dw = (int16_t)_display.measureText(d.c_str(), 2);
            _display.drawText(cx - dw / 2, ty + 44, d.c_str(), WHITE, 2);

        }

    } else {
        // ---------- Digital (default) face ----------
        if (!fullDraw && _lastClockTimeSize > 0) {
            // Erase the previous time band. The time string is monospace with a
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
            // Heart ink spans all 8 map cells but is visually centered at
            // col 3.5 (14px at scale 4), so (dispW - 28)/2 puts it dead-center.
            int16_t heartX = (dispW - 28) / 2;
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
            // Heart ink spans all 8 map cells but is visually centered at
            // col 3.5 (14px at scale 4), so (dispW - 28)/2 puts it dead-center.
            int16_t heartX = (dispW - 28) / 2;
            _display.drawPixelHeart(heartX, 40, heartColor, 4);
        }

        // Compact "HH:MM" so the digits can be big: the full "HH:MM:SS"
        // string is 256px at size 4 - clipped at both edges of the 240px
        // portrait panel. Size 5 (200px) fills the width instead. The short
        // date ("01 Jan 2024") likewise fits where the weekday form clips.
        // The whole stack (heart -> time -> date -> custom text) is positioned
        // to balance around the screen center.
        timeStr = _timeManager.getFormattedTimeShort();
        uint8_t timeSize = 5;
        int16_t timeY = 160;
        _display.drawCenteredTextAt(timeY, timeStr.c_str(), timeColor, timeSize);

        if (fullDraw) {
            int16_t dateY = 230;
            dateStr = _timeManager.getFormattedDateShort();
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
    }  // end digital face

    // Custom text ("Custom text" in the web UI). The digital face draws its own,
    // mid-screen, inside its branch. The other four draw it HERE - after the
    // face has finished, on every tick rather than only on a full frame - so no
    // face's partial repaint (time band, hand erase, opaque pixel-font cell,
    // glow pad) can leave the line erased. One band per face, clear of that
    // face's own content.
    if (style != "digital" && customText.length() > 0) {
        const int16_t customY = (style == "8bit")   ? dispH - 30
                                : (style == "analogue") ? dispH - 12
                                                        : dispH - 8;  // anime, minimal
        const int16_t customW = (int16_t)_display.measureText(customText.c_str(), 1);
        _display.drawText(cx - customW / 2, customY, customText.c_str(), accent, 1);
    }

    _lastClockStyle = style;
    _lastClockDate = dateStr;
    _lastClockCustom = customText;
    _lastHeartColor = heartColor;
    _clockScreenValid = true;
}

// Clock render tick, invoked by ClockPlugin (mode 0). Behaves exactly like the
// old in-loop mode-0 branch: full repaint on first run / cache invalidation,
// otherwise a once-per-second partial update of just the time band; falls back
// to the default dashboard until time sync completes.
void App::runClockTick(bool forceDefaultRedraw) {
    bool isSynced = _timeManager.isTimeSynced();

    if (isSynced) {
        if (!_wasTimeSynced) {
            LOG_INFO(TAG, "Switching to Clock View");
            _wasTimeSynced = true;
            drawClockScreen();
            _lastClockUpdate = millis();
            return;
        }

        uint32_t now = millis();
        if (now - _lastClockUpdate >= 1000) {
            drawClockScreen();
            _lastClockUpdate = now;
        }
        return;
    }

    // Not synced yet: default dashboard, repainted when the status changes.
    static String lastStatus = "";
    String currentStatus = _wifiManager.getStatusString();
    if (currentStatus != lastStatus || forceDefaultRedraw) {
        drawDefaultScreen();
        lastStatus = currentStatus;
    }
}

void App::invalidateClockScreen() {
    _clockScreenValid = false;
    _wasTimeSynced = false; // forces a full clock repaint on the next tick
    _lastClockUpdate = 0;
}

void App::update() {
    _wifiManager.update();
    _webServer.update();
    _timeManager.update();
    _mediaManager.processRequests();

    // Plugins poll/refresh in the background even while another screen is
    // active (e.g. the Scores plugin's football feed).
    _pluginManager.updateBackground();

    uint8_t displayMode = _settings.getDisplayMode();

    // Orientation change: notify the active plugin so it re-lays out.
    uint8_t rotation = _settings.getRotation();
    if (rotation != _lastRotation) {
        _display.setRotation(rotation);
        _lastRotation = rotation;
        _pluginManager.notifyRotation();
        LOG_INFO(TAG, ("Display rotation changed to: " + String(rotation)).c_str());
    }

    // Theme change: notify the active plugin so it recolors. Polled at a low
    // rate because getTheme() returns a heap String; doing it every loop churns
    // the heap and stutters the display.
    uint32_t nowMs = millis();
    if (nowMs - _lastThemeCheckMs >= 500) {
        _lastThemeCheckMs = nowMs;
        String theme = _settings.getTheme();
        if (theme != _lastTheme) {
            _lastTheme = theme;
            _pluginManager.notifyTheme();
            LOG_INFO(TAG, ("Theme changed to: " + theme).c_str());
        }
    }

    // Unknown persisted mode (stale NVS value): fall back to the clock plugin
    // so the screen is never left blank (the old firmware rendered the clock
    // for any mode it did not know).
    if (!_pluginManager.findByMode(displayMode)) {
        displayMode = 0;
    }

    if (displayMode != _lastDisplayMode) {
        _pluginManager.enterMode(displayMode);
        _lastDisplayMode = displayMode;
    }

    _pluginManager.updateActive();

    // Apply brightness only when it actually changed (avoids redundant IO every loop)
    uint8_t brightness = _settings.getBrightness();
    if (brightness != _lastBrightness) {
        _display.setBrightness(brightness);
        _lastBrightness = brightness;
    }
}
