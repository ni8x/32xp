#pragma once

#include "Plugin.h"

class App;
class TimeManager;

// Mode 0: clock + status dashboard, plus the "not synced yet" default screen.
// Thin wrapper for now — App still owns drawClockScreen/drawDefaultScreen;
// this only moves the mode dispatch into the plugin system.
class ClockPlugin : public Plugin {
public:
    ClockPlugin(App* app, TimeManager* timeManager)
        : _app(app), _time(timeManager) {}

    const char* id() const override { return "clock"; }
    const char* name() const override { return "Clock"; }
    uint8_t mode() const override { return 0; }

    void enter() override;
    void update() override;
    void onRotationChange() override;
    void onThemeChange() override;

private:
    App* _app;
    TimeManager* _time;
};
