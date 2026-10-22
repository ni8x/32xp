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
    // Performs a restart asked for by a route once its reply has had time to
    // leave the socket. Restarting inside the handler ate the response: the
    // browser saw a failed request for a change that had in fact been saved.
    void update();

private:
    Settings& _settings;
    WiFiManager& _wifiManager;
    MediaManager& _mediaManager;
    PluginManager* _pluginManager;

    AsyncWebServer _server;
    // Non-zero once a route has asked for a restart (millis() deadline).
    uint32_t _restartAt = 0;

    void scheduleRestart();
    void setupRoutes();
    String buildStatusJson();
    String buildIndexHtml();
};
