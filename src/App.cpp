#include "App.h"
#include "Config.h"
#include "Logger.h"

static const char* TAG = "App";

App::App() 
    : _wifiManager(_settings), 
      _mediaManager(_display, _settings),
      _webServer(_settings, _wifiManager, _mediaManager, _lyricsManager),
      _weatherManager(_settings, _wifiManager),
      _wasWifiConnected(false),
      _wasTimeSynced(false),
      _lastDisplayMode(255),
      _lastClockUpdate(0),
      _lastLyricLine(""),
      _lastLyricProgress(255),
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
    _display.init(_settings.getBrightness());
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

    if (!stateChanged) {
        return; // No change, skip redraw
    }
    _lastLyricLine = _lyricsManager.getCurrentLine();
    _lastLyricProgress = _lyricsManager.getProgress();
    _lastLyricActive = _lyricsManager.isActive();
    _lastPhoneConnected = _lyricsManager.isPhoneConnected();
    _lastLyricTitle = _lyricsManager.getTitle();

    _display.fillScreen(BLACK);

    // --- Header: Song info at top ---
    // drawCenteredTextAt centers horizontally at the given Y position.
    // (drawCenteredText would center vertically and be overdrawn by the lyric line.)
    String header = "";
    if (_lyricsManager.getTitle().length() > 0) {
        header = _lyricsManager.getTitle();
        if (_lyricsManager.getArtist().length() > 0) {
            header += " - " + _lyricsManager.getArtist();
        }
    }
    if (header.length() > 24) {
        header = header.substring(0, 22) + "..";
    }
    if (header.length() > 0) {
        _display.drawCenteredTextAt(2, header.c_str(), YELLOW, 1);
        _display.drawLine(10, 16, Config::DISPLAY_WIDTH - 10, 16, RGB565(64, 64, 64));
    }

    String current = _lyricsManager.getCurrentLine();

    if (current.length() > 0) {
        // Previous line above current (dim, small)
        String prev = _lyricsManager.getPrevLine();
        if (prev.length() > 0) {
            if (prev.length() > 30) prev = prev.substring(0, 28) + "..";
            _display.drawCenteredTextAt(110, prev.c_str(), RGB565(60, 60, 60), 1);
        }

        // Current line (bright, large, centered)
        uint8_t currSize = (current.length() > 18) ? 1 : 2;
        String curr = (current.length() > 36) ? current.substring(0, 34) + ".." : current;
        _display.drawCenteredTextAt(160, curr.c_str(), CYAN, currSize);

        // Next line below current (dim, small)
        String nxt = _lyricsManager.getNextLine();
        if (nxt.length() > 0) {
            if (nxt.length() > 30) nxt = nxt.substring(0, 28) + "..";
            _display.drawCenteredTextAt(210, nxt.c_str(), RGB565(60, 60, 60), 1);
        }
    } else if (_lyricsManager.isActive()) {
        // Song known but no active line yet
        _display.drawCenteredTextAt(150, "Waiting for", RGB565(100, 100, 100), 1);
        _display.drawCenteredTextAt(165, "lyrics...", RGB565(100, 100, 100), 1);
    } else if (_lyricsManager.isPhoneConnected()) {
        // Phone connected but no active lyrics yet
        _display.drawCenteredTextAt(140, "Lyrics Mode", RGB565(60, 60, 60), 2);
        _display.drawCenteredTextAt(175, "Phone Connected", GREEN, 1);
        _display.drawCenteredTextAt(190, "Waiting for lyrics...", RGB565(100, 100, 100), 1);
    } else {
        // Idle state - phone not connected
        _display.drawCenteredTextAt(140, "Lyrics Mode", RGB565(60, 60, 60), 2);
        _display.drawCenteredTextAt(175, "Connect Android app", RGB565(80, 80, 80), 1);
        _display.drawCenteredTextAt(190, "to send lyrics", RGB565(80, 80, 80), 1);
    }

    // --- Progress bar at bottom (when active) ---
    if (_lyricsManager.isActive() && current.length() > 0) {
        int16_t barY = Config::DISPLAY_HEIGHT - 16;
        int16_t barW = Config::DISPLAY_WIDTH - 40;
        int16_t barX = 20;
        uint8_t progress = _lyricsManager.getProgress();
        int16_t filled = (progress * barW) / 100;
        _display.drawRectangle(barX, barY, barW, 6, RGB565(40, 40, 40));
        if (filled > 0) {
            _display.drawFilledRectangle(barX + 1, barY + 1, filled - 1, 4, CYAN);
        }
    }

    // Active indicator dot
    if (_lyricsManager.isActive()) {
        _display.drawCircle(Config::DISPLAY_WIDTH - 8, Config::DISPLAY_HEIGHT - 8, 3, GREEN);
    }
}

void App::drawDefaultScreen() {
    _display.fillScreen(BLACK);
    
    // Header
    _display.drawCenteredText("DEVICE DASHBOARD", WHITE, 2);
    _display.drawLine(10, 30, Config::DISPLAY_WIDTH - 10, 30, GREEN);

    // Wi-Fi Section
    _display.drawText(10, 50, "Wi-Fi Status:", YELLOW, 2);
    
    String statusStr = _wifiManager.getStatusString();
    uint16_t statusColor = _wifiManager.isConnected() ? GREEN : RED;
    _display.drawText(10, 80, statusStr.c_str(), statusColor, 2);

    // Web Dashboard section - the URL to open in a browser
    _display.drawText(10, 130, "Web Dashboard:", YELLOW, 2);

    if (_wifiManager.isConnected()) {
        String url = "http://" + _wifiManager.ipAddress();
        // Use big text only when it fits (max ~19 chars at size 2), else fall back
        uint8_t urlSize = (url.length() <= 19) ? 2 : 1;
        _display.drawCenteredTextAt(165, url.c_str(), CYAN, urlSize);

        _display.drawText(10, 205, "Open in phone browser", WHITE, 1);
        _display.drawText(10, 220, "to control display", WHITE, 1);
    } else {
        _display.drawCenteredTextAt(165, "Waiting for Wi-Fi...", RGB565(100, 100, 100), 1);
        _display.drawText(10, 205, "Web address will appear", WHITE, 1);
        _display.drawText(10, 220, "once connected", WHITE, 1);
    }
}

void App::drawClockScreen() {
    _display.fillScreen(BLACK);
    
    // Draw Large Pixelated Heart centered above time (scale 4 = 32x28px)
    uint16_t heartColor = Display::hexToRGB565(_settings.getHeartColor());
    int16_t heartX = (Config::DISPLAY_WIDTH - (8 * 4)) / 2;
    _display.drawPixelHeart(heartX, 20, heartColor, 4);

    String timeStr = _timeManager.getFormattedTime();
    String dateStr = _timeManager.getFormattedDate();

    // Large time centered
    _display.drawCenteredText(timeStr.c_str(), CYAN, 4);

    // Date below time
    int16_t dateY = (Config::DISPLAY_HEIGHT / 2) + 40;
    
    // Draw directly to center bottom half
    int16_t dateWidth = dateStr.length() * 12;
    int16_t dateX = (Config::DISPLAY_WIDTH - dateWidth) / 2;
    _display.drawText(dateX, dateY, dateStr.c_str(), WHITE, 2);

    // Custom Text below date
    String customText = _settings.getClockText();
    if (customText.length() > 0) {
        int16_t textWidth = customText.length() * 12;
        int16_t textX = (Config::DISPLAY_WIDTH - textWidth) / 2;
        _display.drawText(textX, dateY + 30, customText.c_str(), YELLOW, 2);
    }
}

void App::update() {
    _wifiManager.update();
    _timeManager.update();
    _weatherManager.update();
    _mediaManager.processRequests();

    uint32_t now = millis();

    uint8_t displayMode = _settings.getDisplayMode();

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
