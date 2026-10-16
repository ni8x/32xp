#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "Plugin.h"

// Owns the ordered plugin list. App registers plugins at boot; the manager
// fans out lifecycle, render ticks, API dispatch, and status aggregation.
class PluginManager {
public:
    static constexpr uint8_t MAX_PLUGINS = 8;

    // Registration order is fixed at boot. The table closes when beginAll()
    // runs: a plugin registered after that would never receive begin() and
    // could still be dispatched half-initialised, so such a call is rejected.
    // Duplicates of id or mode are rejected too (they make dispatch ambiguous).
    void registerPlugin(Plugin* plugin);

    Plugin* findByMode(uint8_t mode) const;
    Plugin* findById(const char* id) const;
    uint8_t count() const { return _count; }
    Plugin* at(uint8_t index) const { return index < _count ? _plugins[index] : nullptr; }

    void beginAll();
    void enterMode(uint8_t mode);   // exit() old active plugin, enter() new
    void updateActive();            // update() tick for the active plugin
    void updateBackground();        // backgroundTick() for every plugin
    void notifyRotation();          // onRotationChange on the active plugin
    void notifyTheme();             // onThemeChange on the active plugin

    // POST /api/plugin/<id>/<action>. Always answers the request (404 when
    // the plugin or action is unknown); returns true if a response was sent.
    bool dispatchApi(const String& id, const String& action, AsyncWebServerRequest* request) const;

    // Fill pluginsObj: { <id>: { name, mode, ...plugin fields }, ... }.
    void buildStatusJson(JsonObject pluginsObj) const;

private:
    Plugin* _plugins[MAX_PLUGINS] = {nullptr};
    uint8_t _count = 0;
    Plugin* _active = nullptr;
    bool _begun = false;   // set by beginAll(); closes the registration table
};
