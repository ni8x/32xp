#include "App.h"
#include "Config.h"
#include "Logger.h"

static const char* TAG = "App";

App::App() 
    : _wifiManager(_settings), 
      _mediaManager(_display, _settings),
      _webServer(_settings, _wifiManager, _mediaManager, &_pluginManager),
      _timeManager(),
      _weatherManager(_settings, _wifiManager),
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
    _weatherManager.begin();

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
    _timeManager.update();
    _weatherManager.update();
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

    // Theme change: notify the active plugin so it recolors.
    String theme = _settings.getTheme();
    if (theme != _lastTheme) {
        _lastTheme = theme;
        _pluginManager.notifyTheme();
        LOG_INFO(TAG, ("Theme changed to: " + theme).c_str());
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
