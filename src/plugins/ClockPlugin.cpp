#include "ClockPlugin.h"
#include "../App.h"

void ClockPlugin::enter() {
    // Invalidate the clock redraw cache so the screen repaints fully with the
    // new layout/colors when the clock becomes the active screen.
    _app->invalidateClockScreen();
}

void ClockPlugin::update() {
    _app->runClockTick();
}

void ClockPlugin::onRotationChange() {
    _app->invalidateClockScreen();
    if (!_time->isTimeSynced()) {
        // Not synced yet: force the default screen to re-render for the new
        // dimensions (the status string alone may not have changed).
        _app->runClockTick(true);
    }
}

void ClockPlugin::onThemeChange() {
    _app->invalidateClockScreen();
    if (!_time->isTimeSynced()) {
        _app->runClockTick(true);
    }
}
