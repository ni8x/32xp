#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "Settings.h"
#include "WiFiManager.h"

class MediaManager;   // forward declaration
class PluginManager;

class WebServer {
public:
    WebServer(Settings& settings, WiFiManager& wifiManager, MediaManager& mediaManager,
              PluginManager* pluginManager);
    ~WebServer();

    void begin();

private:
    Settings& _settings;
    WiFiManager& _wifiManager;
    MediaManager& _mediaManager;
    PluginManager* _pluginManager;

    AsyncWebServer _server;

    void setupRoutes();
    String buildStatusJson();
    String buildIndexHtml();
};
