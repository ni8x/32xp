#pragma once

#include "Plugin.h"
#include "../EspnApi.h"

// Mode 3: cricket / football scoreboard. Both sports are populated from ESPN's
// no-key scoreboard API through one shared client (see EspnApi); the web API
// can also push a score manually.
// Everything goes through the generic plugin API:
//   POST /api/plugin/scores/set      sport,teamA,scoreA,teamB,scoreB,status
//   POST /api/plugin/scores/sport    sport=cricket|football
//   POST /api/plugin/scores/clear
//   POST /api/plugin/scores/football league,event,interval
//   POST /api/plugin/scores/refresh  (force an ESPN fetch now)
//   POST /api/plugin/scores/browse   sport,league[,date]  -> match-list JSON
//     (Look-ahead only: answers the web page's Browse button without touching
//      the saved feed settings or the live board.)
// Cricket and football share one field shape; only the header label and the
// meaning of the status line differ. setScore() is the single mutation point,
// used by both the web API and the live football importer.
class ScorePlugin : public Plugin {
public:
    ScorePlugin(Display* display, Settings* settings, WiFiManager* wifi);

    const char* id() const override { return "scores"; }
    const char* name() const override { return "Scores"; }
    uint8_t mode() const override { return 3; }

    void begin() override;            // bring up the football poller
    void enter() override;            // repaint + resume ESPN polling
    void exit() override;             // stop ESPN polling when leaving the screen
    void backgroundTick() override;   // poll the active sport, scores screen only
    void update() override;           // render only when state changed
    void onRotationChange() override; // re-lay out for the new dimensions
    void onThemeChange() override;    // recolour with the new palette

    bool handleApi(AsyncWebServerRequest* request, const String& action) override;
    void buildStatusJson(JsonObject obj) override;
    String uiSection() const override;

    // Single mutation entry point (web API today, live fetcher later). An
    // empty/unknown sport keeps the current one.
    void setScore(const String& sport, const String& teamA, const String& scoreA,
                  const String& teamB, const String& scoreB, const String& status);
    void setSport(const String& sport);
    void clear();

private:
    // Resolved once per frame from the live panel size so the layout, the
    // region clears and the animations all agree on the same geometry.
    struct Layout {
        bool landscape;
        int16_t dispW;
        int16_t dispH;
        int16_t headerY;
        int16_t lineY;
        uint8_t nameSize;
        uint8_t scoreSize;
        int16_t row1Y;
        int16_t row2Y;
        int16_t statusY;
    };

    void drawScreen();
    bool hasData() const;
    // Bottom line: the status text plus any bracketed score detail. Cached so
    // the render loop never rebuilds strings / measures text per iteration.
    void rebuildBottomLine();
    String statusLine() const { return _bottomLine; }
    // Fetch the requested ESPN scoreboard on the fly (not persisted) and reply
    // with a compact {ok, events:[{id,label}]} list for the dashboard.
    void browseScoreboard(const String& kind, AsyncWebServerRequest* request);

    Layout layout() const;
    bool overflows(const String& text, uint8_t size, int16_t maxW) const;
    // Horizontal scroll offset in px for text too wide for maxW (~45 px/s).
    int16_t marqueeOffset(const String& text, uint8_t size, int16_t maxW, uint32_t now) const;
    bool marqueeActive() const;

    // Partial repaints: each clears and redraws only its own band, so animated
    // frames never flash the whole panel.
    void drawRow(const Layout& L, uint8_t index, uint32_t now, bool flashScore) const;
    void drawStatus(const Layout& L, uint32_t now) const;
    void animFrame();
    void drawGoalFrame(uint32_t now) const;
    void triggerGoal(uint8_t side); // 0 = home (A), 1 = away (B)

    Display* _display;
    Settings* _settings;
    EspnApi _football;
    EspnApi _cricket;

    bool _active;             // true only while the scores screen is showing
    String _sport;
    String _teamA;
    String _scoreA;
    String _teamB;
    String _scoreB;
    String _status;
    String _bottomLine;       // cached bottom marquee text (status + detail)
    bool _bottomOverflows;    // cached: whether _bottomLine needs to scroll
    uint32_t _updatedAt;
    bool _dirty;
    uint32_t _lastAgeMinutes; // drives the once-a-minute footer refresh

    // Animation state
    bool _goalActive;         // full-screen football goal celebration running
    uint8_t _goalSide;        // which side scored (for the celebration text)
    uint32_t _goalStartMs;
    uint32_t _flashUntilA;    // score cells blink until these times
    uint32_t _flashUntilB;
    uint32_t _marqueeStartMs; // restarts whenever the text changes
    uint32_t _lastAnimMs;     // frame pacer
};
