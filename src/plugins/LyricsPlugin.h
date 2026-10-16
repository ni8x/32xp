#pragma once

#include "Plugin.h"

class LyricsManager;

// Mode 2: lyrics display. Owns the lyrics screen rendering (with partial
// redraw caching), the frozen /api/lyrics/* endpoints used by the Android
// app, its /api/status slice, and its dashboard card.
class LyricsPlugin : public Plugin {
public:
    LyricsPlugin(Display* display, Settings* settings, LyricsManager* lyrics);

    const char* id() const override { return "lyrics"; }
    const char* name() const override { return "Lyrics"; }
    uint8_t mode() const override { return 2; }

    void enter() override;
    void update() override;
    void onRotationChange() override;
    void onThemeChange() override;

    void registerRoutes(AsyncWebServer& server) override;
    bool handleApi(AsyncWebServerRequest* request, const String& action) override;
    void buildStatusJson(JsonObject obj) override;
    String uiSection() const override;

private:
    void drawScreen();
    void clearCache();

    Display* _display;
    Settings* _settings;
    LyricsManager* _lyrics;

    // Partial-redraw cache: while nothing changed, nothing is repainted; when
    // only the rainbow/timestamps animate, just their regions are refreshed.
    String _lastLyricLine;
    String _lastLyricColor;
    bool _lastLyricGlow;
    bool _lastLyricRainbow;
    bool _lastLyricFull;
    bool _lastLyricGradient;
    bool _lastLyricScroll;
    uint8_t _lastLyricFullSize;
    uint32_t _lastLyricDraw;
    // Timestamp of the last FULL repaint; throttles bursty updates (song change
    // fires title+line+progress back-to-back) into a single frame.
    uint32_t _lastFullRedraw;
    uint8_t _lastLyricProgress;
    // Bounding box of the drawn current line; the animation tick erases only
    // this region so the rest of the screen never flickers.
    int16_t _lastCurrX;
    int16_t _lastCurrY;
    int16_t _lastCurrW;
    int16_t _lastCurrH;
    // Widths of the last drawn timestamp readouts (for the per-second tick).
    int16_t _lastTsLW;
    int16_t _lastTsRW;
    bool _lastLyricActive;
    bool _lastPhoneConnected;
    String _lastLyricTitle;
    String _lastLyricPrev;
    String _lastLyricNext;

    // Scroll-up transition (see drawScreen): while _scrollActive the row stack is
    // redrawn by the update() tick as it slides from _scrollFrom px below its
    // place down to zero. Progress is wall-clock based, so a slow SPI bus costs
    // frames rather than stretching the transition.
    bool _scrollActive;
    uint32_t _scrollStart;
    int16_t _scrollFrom;
    int16_t _scrollDy;
};
