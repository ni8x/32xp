#pragma once

#include "plugins/Plugin.h"

class MediaManager;

// Mode 1: media slideshow / GIF playback. Thin wrapper around MediaManager;
// the slideshow logic itself stays in MediaManager (already self-contained).
class MediaPlugin : public Plugin {
public:
    explicit MediaPlugin(MediaManager* media) : _media(media) {}

    const char* id() const override { return "media"; }
    const char* name() const override { return "Media / Slideshow"; }
    uint8_t mode() const override { return 1; }

    void enter() override;   // reset() to force immediate playback
    void exit() override;    // stop() playback when leaving the screen
    void update() override;
    void onRotationChange() override; // restart playback for the new dimensions
    void onThemeChange() override;    // old firmware reset the slideshow here too

private:
    MediaManager* _media;
};
