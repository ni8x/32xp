#include "MediaPlugin.h"
#include "../MediaManager.h"

void MediaPlugin::enter() {
    _media->reset(); // force immediate playback when entering media mode
}

void MediaPlugin::exit() {
    _media->stop();
}

void MediaPlugin::update() {
    _media->update();
}

void MediaPlugin::onRotationChange() {
    _media->reset();
}

void MediaPlugin::onThemeChange() {
    _media->reset();
}
