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
//
// WHICH TASK RUNS WHAT - the plugin manager does not serialize these for you:
//   Arduino task (loop):  begin, enter, exit, update, backgroundTick,
//                         onRotationChange, onThemeChange, and the
//                         registerRoutes() call itself (boot time).
//   AsyncTCP task (web):  handleApi(), the lambda bodies installed by
//                         registerRoutes(), uiSection(), buildStatusJson().
// The two tasks run concurrently, so a plugin must not have the web task
// reassign a String/member that update() or drawScreen() is reading, nor drive
// the same client (EspnApi, LyricsManager, ...) from both. Keep mutations on
// the loop task (queue them from the handler, as MediaManager does with its
// mutex + _pendingDeletes) or guard the state explicitly.
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
    // The call is on the Arduino task; the handlers it installs run later on
    // the AsyncTCP task (see the task note at the top of this file).
    virtual void registerRoutes(AsyncWebServer& server) {}

    // POST /api/plugin/<id>/<action>. Runs on the AsyncTCP task, concurrently
    // with update(). Return true if the request was handled (a response was
    // sent); return false to let the manager answer 404.
    virtual bool handleApi(AsyncWebServerRequest* request, const String& action) { return false; }

    // Add this plugin's slice to /api/status (received every 4 s by the web
    // dashboard): obj["<field>"] = ... inside plugins.<id>. Keep it small.
    // Runs on the AsyncTCP task: it reads plugin state, so whatever update()
    // writes must be safe to read from another task.
    virtual void buildStatusJson(JsonObject obj) {}

    // HTML snippet (one or more .card divs + an optional <script>) injected
    // into the dashboard page at boot, so a plugin ships its own UI without
    // touching WebServer. Return "" for no dashboard section. Built on the
    // AsyncTCP task for every GET /, so it must only read task-safe state.
    virtual String uiSection() const { return ""; }
};
