#include "PluginManager.h"
#include "../Logger.h"
#include <ESPAsyncWebServer.h>

static const char* TAG = "PluginMgr";

void PluginManager::registerPlugin(Plugin* plugin) {
    if (!plugin || _count >= MAX_PLUGINS) {
        LOG_ERROR(TAG, "Plugin registration failed (null or full)");
        return;
    }
    // Mode/id collisions would make dispatch ambiguous; refuse duplicates.
    for (uint8_t i = 0; i < _count; i++) {
        if (_plugins[i]->mode() == plugin->mode()) {
            LOG_ERROR(TAG, "Plugin mode collision");
            return;
        }
        if (strcmp(_plugins[i]->id(), plugin->id()) == 0) {
            LOG_ERROR(TAG, "Plugin id collision");
            return;
        }
    }
    _plugins[_count++] = plugin;
    LOG_INFO(TAG, ("Plugin registered: " + String(plugin->id())).c_str());
}

Plugin* PluginManager::findByMode(uint8_t mode) const {
    for (uint8_t i = 0; i < _count; i++) {
        if (_plugins[i]->mode() == mode) return _plugins[i];
    }
    return nullptr;
}

Plugin* PluginManager::findById(const char* id) const {
    for (uint8_t i = 0; i < _count; i++) {
        if (strcmp(_plugins[i]->id(), id) == 0) return _plugins[i];
    }
    return nullptr;
}

void PluginManager::beginAll() {
    for (uint8_t i = 0; i < _count; i++) {
        _plugins[i]->begin();
    }
}

void PluginManager::enterMode(uint8_t mode) {
    Plugin* next = findByMode(mode);
    if (next == _active) return;

    if (_active) _active->exit();
    _active = next;
    if (_active) {
        _active->enter();
        LOG_INFO(TAG, ("Active plugin: " + String(_active->id())).c_str());
    }
}

void PluginManager::updateActive() {
    if (_active) _active->update();
}

void PluginManager::updateBackground() {
    for (uint8_t i = 0; i < _count; i++) {
        _plugins[i]->backgroundTick();
    }
}

void PluginManager::notifyRotation() {
    if (_active) _active->onRotationChange();
}

void PluginManager::notifyTheme() {
    if (_active) _active->onThemeChange();
}

bool PluginManager::dispatchApi(const String& id, const String& action, AsyncWebServerRequest* request) const {
    Plugin* plugin = findById(id.c_str());
    if (!plugin) {
        request->send(404, "text/plain", "Unknown plugin");
        return true; // request handled (with 404)
    }
    if (plugin->handleApi(request, action)) return true;
    request->send(404, "text/plain", "Unknown action");
    return true;
}

void PluginManager::buildStatusJson(JsonObject pluginsObj) const {
    for (uint8_t i = 0; i < _count; i++) {
        Plugin* p = _plugins[i];
        JsonObject obj = pluginsObj[p->id()].to<JsonObject>();
        obj["name"] = p->name();
        obj["mode"] = p->mode();
        p->buildStatusJson(obj);
    }
}
