#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "Settings.h"
#include "WiFiManager.h"

class MediaManager; // forward declaration
class LyricsManager; // forward declaration

class WebServer {
public:
    WebServer(Settings& settings, WiFiManager& wifiManager, MediaManager& mediaManager, LyricsManager& lyricsManager);
    ~WebServer();

    void begin();

private:
    Settings& _settings;
    WiFiManager& _wifiManager;
    MediaManager& _mediaManager;
    LyricsManager& _lyricsManager;
    AsyncWebServer _server;

    void setupRoutes();
    String buildStatusJson();
};
