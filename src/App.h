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
    uint8_t _lastLyricProgress;
    bool _lastLyricActive;
    bool _lastPhoneConnected;
    String _lastLyricTitle;
    uint8_t _lastBrightness;

    void drawDefaultScreen();
    void drawClockScreen();
    void drawLyricsScreen();
};
