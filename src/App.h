#pragma once

#include "Settings.h"
#include "Display.h"
#include "WiFiManager.h"
#include "WebServer.h"
#include "TimeManager.h"
#include "WeatherManager.h"
#include "MediaManager.h"
#include "LyricsManager.h"
#include "plugins/PluginManager.h"
#include "plugins/ClockPlugin.h"
#include "plugins/MediaPlugin.h"
#include "plugins/LyricsPlugin.h"
#include "plugins/ScorePlugin.h"

class App {
public:
    App();
    ~App();

    void init();
    void update();

    // Called by ClockPlugin (mode 0). invalidateClockScreen forces a full
    // clock/default-screen repaint (rotation/theme change, mode re-entry);
    // runClockTick is the per-loop render tick (clock once per second when
    // time is synced, otherwise the default dashboard). Set
    // forceDefaultRedraw to repaint the default screen immediately even if
    // the Wi-Fi status string did not change.
    void invalidateClockScreen();
    void runClockTick(bool forceDefaultRedraw = false);

private:
    Settings _settings;
    Display _display;
    WiFiManager _wifiManager;
    MediaManager _mediaManager;
    WebServer _webServer;
    TimeManager _timeManager;
    WeatherManager _weatherManager;
    LyricsManager _lyricsManager;

    // Plugin system: one plugin per screen mode, registered in init().
    PluginManager _pluginManager;
    ClockPlugin _clockPlugin;
    MediaPlugin _mediaPlugin;
    LyricsPlugin _lyricsPlugin;
    ScorePlugin _scorePlugin;

    bool _wasWifiConnected;
    bool _wasTimeSynced;
    uint8_t _lastDisplayMode;
    uint8_t _lastRotation;
    String _lastTheme;
    uint32_t _lastClockUpdate;
    // Clock partial-redraw cache: while the date/custom text are unchanged only
    // the time band is repainted each second, so the screen never flashes black.
    bool _clockScreenValid;
    String _lastClockDate;
    String _lastClockCustom;
    uint8_t _lastClockTimeSize;
    int16_t _lastClockTimeY;
    uint16_t _lastHeartColor; // heart redraws when the user changes it via web
    uint8_t _lastBrightness;

    void drawDefaultScreen();
    void drawClockScreen();
};
