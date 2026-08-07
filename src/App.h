#pragma once

#include "Settings.h"
#include "Display.h"
#include "WiFiManager.h"
#include "WebServer.h"
#include "TimeManager.h"
#include "WeatherManager.h"
#include "MediaManager.h"
#include "LyricsManager.h"

class App {
public:
    App();
    ~App();

    void init();
    void update();

private:
    Settings _settings;
    Display _display;
    WiFiManager _wifiManager;
    MediaManager _mediaManager;
    WebServer _webServer;
    TimeManager _timeManager;
    WeatherManager _weatherManager;
    LyricsManager _lyricsManager;

    bool _wasWifiConnected;
    bool _wasTimeSynced;
    uint8_t _lastDisplayMode;
    uint8_t _lastRotation;
    uint32_t _lastClockUpdate;
    String _lastLyricLine;
    String _lastLyricColor;
    bool _lastLyricGlow;
    bool _lastLyricRainbow;
    bool _lastLyricFull;
    uint32_t _lastLyricDraw;
    uint8_t _lastLyricProgress;
    // Bounding box of the drawn current line; the animation tick erases only
    // this region so the rest of the screen never flickers.
    int16_t _lastCurrX;
    int16_t _lastCurrY;
    int16_t _lastCurrW;
    int16_t _lastCurrH;
    // Widths of the last drawn timestamp readouts (for the per-second partial tick).
    int16_t _lastTsLW;
    int16_t _lastTsRW;
    bool _lastLyricActive;
    bool _lastPhoneConnected;
    String _lastLyricTitle;
    uint8_t _lastBrightness;

    void drawDefaultScreen();
    void drawClockScreen();
    void drawLyricsScreen();
};
