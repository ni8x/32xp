#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "Display.h"
#include "Settings.h"

// Compile-time plugin interface. Each screen mode is a Plugin; adding a
// feature = adding a class + registering it in App.cpp's plugin list.
// (Runtime-installable plugins are not possible on ESP32; this buys isolated
// modules and a zero-touch App/WebServer, not app-store plugins.)
class Plugin {
public:
    virtual ~Plugin() = default;

    // Stable identifier used in URLs (/api/plugin/<id>/<action>) and as the
    // key of this plugin's object in /api/status -> plugins. Lowercase.
    virtual const char* id() const = 0;
    // Human-readable name (dashboard mode dropdown / logs).
    virtual const char* name() const = 0;
    // Which displayMode number this plugin serves (Settings::getDisplayMode).
    virtual uint8_t mode() const = 0;

    // Lifecycle
    virtual void begin() {}   // once at boot, after all managers are up
    virtual void enter() {}   // becoming the active screen
    virtual void exit() {}    // no longer the active screen
    virtual void update() {}  // called every loop while active; draw here
    // Called every loop for EVERY registered plugin, active or not. Use it for
    // background work (network polls, timers) that must keep running while a
    // different screen is showing.
    virtual void backgroundTick() {}
    virtual void onRotationChange() {} // screen rotated; re-lay out
    virtual void onThemeChange() {}    // theme changed; recolor if needed

    // Literal routes this plugin owns (e.g. frozen endpoints the Android app
    // posts to). Registered once at boot, before the generic plugin route.
    virtual void registerRoutes(AsyncWebServer& server) {}

    // POST /api/plugin/<id>/<action>. Return true if the request was handled
    // (a response was sent); return false to let the manager answer 404.
    virtual bool handleApi(AsyncWebServerRequest* request, const String& action) { return false; }

    // Add this plugin's slice to /api/status (received every 4 s by the web
    // dashboard): obj["<field>"] = ... inside plugins.<id>. Keep it small.
    virtual void buildStatusJson(JsonObject obj) {}

    // HTML snippet (one or more .card divs + an optional <script>) injected
    // into the dashboard page at boot, so a plugin ships its own UI without
    // touching WebServer. Return "" for no dashboard section.
    virtual String uiSection() const { return ""; }
};
