#include "ScorePlugin.h"
#include "../DashboardAssets.h"
#include "../Logger.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

static const char* TAG = "ScorePlugin";

// Animation tuning
static const uint32_t ANIM_FRAME_MS = 80;   // ~12.5 fps for marquee / blink
static const uint32_t GOAL_ANIM_MS  = 2400; // full-screen goal celebration
static const uint32_t FLASH_MS      = 1600; // how long a changed score blinks

static bool validSport(const String& s) {
    return s == "cricket" || s == "football";
}

// A score that is just digits ("2", "10") - used to tell a football goal
// (a numeric increase) from cricket's "187/6" style strings.
static bool numericScore(const String& s) {
    if (s.length() == 0) return false;
    for (size_t i = 0; i < s.length(); i++) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

static String trimmed(String s) { s.trim(); return s; }

// Cricket feeds pack extra info in brackets, e.g. "172/2 (14/20 ov, target 172)".
// The row next to the team only has room for the bare score, so split the main
// part from the bracketed detail (which the bottom marquee line shows instead).
static String scoreMain(const String& s) {
    const int p = s.indexOf('(');
    if (p < 0) return s;
    String m = s.substring(0, p);
    m.trim();
    return m;
}

static String scoreDetail(const String& s) {
    const int q = s.indexOf('(');
    if (q < 0) return String();
    const int close = s.indexOf(')', q);
    String d = (close > q) ? s.substring(q + 1, close) : s.substring(q + 1);
    d.trim();
    return d;
}

// Bracket content before the comma ("14/20 ov") stays in the team row;
// content after the comma ("target 172") goes to the bottom marquee line.
static String scoreTopDetail(const String& s) {
    const String in = scoreDetail(s);
    const int c = in.indexOf(',');
    return (c < 0) ? in : trimmed(in.substring(0, c));
}

static String scoreBottomDetail(const String& s) {
    const String in = scoreDetail(s);
    const int c = in.indexOf(',');
    return (c < 0) ? String() : trimmed(in.substring(c + 1));
}

ScorePlugin::ScorePlugin(Display* display, Settings* settings, WiFiManager* wifi)
    : _display(display),
      _settings(settings),
      _football(*settings, *wifi, ESPN_FOOTBALL),
      _cricket(*settings, *wifi, ESPN_CRICKET),
      _active(false),
      _sport(settings->getScoreSport()),
      _teamA(),
      _scoreA(),
      _teamB(),
      _scoreB(),
      _status(),
      _bottomLine(),
      _bottomOverflows(false),
      _updatedAt(0),
      _dirty(true),
      _lastAgeMinutes(0xFFFFFFFF),
      _goalActive(false),
      _goalSide(0),
      _goalStartMs(0),
      _flashUntilA(0),
      _flashUntilB(0),
      _marqueeStartMs(0),
      _lastAnimMs(0) {
    if (!validSport(_sport)) _sport = "cricket";
}

void ScorePlugin::begin() {
    _football.begin();
    _cricket.begin();
}

void ScorePlugin::backgroundTick() {
    // Poll ONLY while the scores screen is the active mode (enter()/exit() keep
    // _active in step with the mode switch). There is deliberately no
    // off-screen polling: earlier versions fetched ESPN every loop from every
    // screen, which burned bandwidth and fragmented the heap with TLS buffers.
    // Only the active sport's feed is polled: fetching both doubles the HTTPS
    // work for no visible benefit.
    if (!_active) return;
    if (_sport == "football") {
        _football.update();
    } else {
        _cricket.update();
    }
}

void ScorePlugin::setScore(const String& sport, const String& teamA, const String& scoreA,
                           const String& teamB, const String& scoreB, const String& status) {
    const bool hadData = hasData();
    const String oldScoreA = _scoreA;
    const String oldScoreB = _scoreB;
    const String oldTeamA = _teamA;
    const String oldTeamB = _teamB;

    if (validSport(sport)) setSport(sport);
    _teamA = teamA;
    _scoreA = scoreA;
    _teamB = teamB;
    _scoreB = scoreB;
    _status = status;
    const uint32_t now = millis();
    _updatedAt = now;
    _lastAgeMinutes = 0;
    _dirty = true;

    // Any change to the scrolling text restarts it from the beginning.
    if (oldTeamA != _teamA || oldTeamB != _teamB ||
        oldScoreA != _scoreA || oldScoreB != _scoreB) {
        _marqueeStartMs = now;
    }

    rebuildBottomLine();

    // Going from an empty board to a populated one is not a score change, so it
    // must not fire a blink or a celebration.
    if (!hadData) return;

    const bool changedA = (oldScoreA != _scoreA);
    const bool changedB = (oldScoreB != _scoreB);
    if (changedA) _flashUntilA = now + FLASH_MS;
    if (changedB) _flashUntilB = now + FLASH_MS;

    // A goal is a football score that went up on the SAME match. The automatic
    // match selector jumping to a different game (whose score is simply higher)
    // must not celebrate, so require both team names to be unchanged.
    const bool sameMatch = (oldTeamA == _teamA && oldTeamB == _teamB);
    if (_sport == "football" && sameMatch) {
        const bool homeGoal = changedA && numericScore(oldScoreA) && numericScore(_scoreA) &&
                              _scoreA.toInt() > oldScoreA.toInt();
        const bool awayGoal = changedB && numericScore(oldScoreB) && numericScore(_scoreB) &&
                              _scoreB.toInt() > oldScoreB.toInt();
        if (homeGoal || awayGoal) triggerGoal(homeGoal ? 0 : 1);
    }
}

void ScorePlugin::setSport(const String& sport) {
    if (!validSport(sport) || sport == _sport) return;
    _sport = sport;
    _settings->setScoreSport(_sport);
    _marqueeStartMs = millis();
    _dirty = true;
}

void ScorePlugin::clear() {
    _teamA = "";
    _scoreA = "";
    _teamB = "";
    _scoreB = "";
    _status = "";
    _updatedAt = millis();
    _flashUntilA = 0;
    _flashUntilB = 0;
    _goalActive = false;
    _marqueeStartMs = millis();
    rebuildBottomLine();
    _dirty = true;
}

bool ScorePlugin::hasData() const {
    return _teamA.length() > 0 || _teamB.length() > 0 || _status.length() > 0;
}

void ScorePlugin::enter() {
    _marqueeStartMs = millis();
    rebuildBottomLine();
    _dirty = true;
    // The scores screen is on show: resume ESPN polling and, if the saved feed
    // is already due, refresh it now so the board is current on arrival.
    _active = true;
    if (_sport == "football") {
        _football.update();
    } else {
        _cricket.update();
    }
}

void ScorePlugin::exit() {
    // Leaving the scores screen stops all ESPN traffic: no background polling,
    // no TLS buffers held, no heap churn while other screens (and the web
    // dashboard) are in use.
    _active = false;
}

void ScorePlugin::onRotationChange() {
    _marqueeStartMs = millis();
    rebuildBottomLine(); // overflow depends on the (new) panel width
    _dirty = true;
}

void ScorePlugin::onThemeChange() {
    _dirty = true;
}

void ScorePlugin::update() {
    // The board is fed by whichever ESPN client matches the active sport.
    EspnApi* feed = _sport == "football" ? &_football : &_cricket;
    if (feed->hasData() &&
        (_teamA != feed->homeName() || _scoreA != feed->homeScore() ||
         _teamB != feed->awayName() || _scoreB != feed->awayScore() ||
         _status != feed->statusText())) {
        setScore(_sport, feed->homeName(), feed->homeScore(),
                 feed->awayName(), feed->awayScore(), feed->statusText());
    }

    // Refresh the "updated N min ago" footer once a minute (only the minute
    // bucket changes, so this is cheap).
    if (hasData()) {
        uint32_t age = (millis() - _updatedAt) / 60000UL;
        if (age != _lastAgeMinutes) {
            _lastAgeMinutes = age;
            _dirty = true;
        }
    }

    // Animations repaint only their own bands (marquee scroll, score blink) and
    // run a few times a second; a goal takes over the whole panel briefly.
    const uint32_t now = millis();
    const bool animating = _goalActive || now < _flashUntilA || now < _flashUntilB || marqueeActive();

    if (animating) {
        if (now - _lastAnimMs < ANIM_FRAME_MS) {
            // Between frames: still honour a full repaint if something else
            // changed (sport, theme, rotation) so it is not starved.
            if (_dirty) {
                _dirty = false;
                drawScreen();
            }
            return;
        }
        _lastAnimMs = now;

        if (_goalActive) {
            drawGoalFrame(now);
            if (now - _goalStartMs >= GOAL_ANIM_MS) {
                _goalActive = false;
                _dirty = false;
                drawScreen(); // restore the normal board
            }
            return;
        }

        if (_dirty) {
            _dirty = false;
            drawScreen();
            return;
        }
        animFrame();
        return;
    }

    if (!_dirty) return;
    _dirty = false;
    drawScreen();
}

void ScorePlugin::drawScreen() {
    _display->fillScreen(BLACK);

    const Layout L = layout();
    const Settings::ThemePalette* palette = Settings::getThemePalette(_settings->getTheme());
    const uint16_t accent = Display::hexToRGB565(palette->accentHex);
    const uint16_t lineColor = Display::hexToRGB565(palette->lineHex);
    const uint16_t dimColor = Display::hexToRGB565(palette->dimHex);

    const char* sportLabel = (_sport == "football") ? "FOOTBALL" : "CRICKET";
    // Sport icon shown beside the heading (built-in emoji sprite: football /
    // cricket bat-and-ball, see EmojiFont.h).
    const uint32_t sportIcon = (_sport == "football") ? 0x26BD : 0x1F3CF;

    // Header + separator. Remember unifont ink renders 14*size px ABOVE the
    // given y, so these y values already account for it. The heading and its
    // icon are centred as one group.
    const int16_t iconCell = 16;  // one emoji sprite cell
    const int16_t headerGap = 8;
    const int16_t labelW = (int16_t)_display->measureText(sportLabel, 2);
    const int16_t headX = (L.dispW - (labelW + headerGap + iconCell)) / 2;
    _display->drawText(headX, L.headerY, sportLabel, accent, 2);
    _display->drawEmojiCentered(headX + labelW + headerGap + iconCell / 2, L.headerY - 12,
                                sportIcon, 1);
    _display->drawLine(10, L.lineY, L.dispW - 10, L.lineY, lineColor);

    if (!hasData()) {
        _display->drawCenteredTextAt(L.dispH / 2, "No scores yet", dimColor, 2);
        _display->drawCenteredTextAt(L.dispH / 2 + 36, "Enter them in the", dimColor, 1);
        _display->drawCenteredTextAt(L.dispH / 2 + 52, "web dashboard", dimColor, 1);
        return;
    }

    // Rows and status now draw through the shared band painters so an animated
    // frame can repaint exactly the same geometry without a full clear.
    const uint32_t now = millis();
    drawRow(L, 0, now, now < _flashUntilA);
    drawRow(L, 1, now, now < _flashUntilB);
    drawStatus(L, now);

    // Footer: how long ago the score was pushed.
    uint32_t age = (millis() - _updatedAt) / 60000UL;
    String ago;
    if (age == 0) {
        ago = "Updated just now";
    } else if (age == 1) {
        ago = "Updated 1 min ago";
    } else {
        ago = "Updated " + String(age) + " min ago";
    }
    _display->drawCenteredTextAt(L.dispH - 18, ago.c_str(), dimColor, 1);
}

ScorePlugin::Layout ScorePlugin::layout() const {
    Layout L;
    L.dispW = _display->width();
    L.dispH = _display->height();
    L.landscape = L.dispW > L.dispH;
    L.headerY = L.landscape ? 30 : 40;
    L.lineY = L.landscape ? 40 : 48;
    L.nameSize = 2;
    L.scoreSize = 2; // Reduced from 3/4 to 2 for both orientations
    L.row1Y = L.landscape ? 100 : 112;
    L.row2Y = L.landscape ? 158 : 188;
    L.statusY = L.landscape ? 208 : 262;
    return L;
}

bool ScorePlugin::overflows(const String& text, uint8_t size, int16_t maxW) const {
    if (text.length() == 0 || maxW <= 0) return false;
    return (int16_t)_display->measureText(text.c_str(), size) > maxW;
}

int16_t ScorePlugin::marqueeOffset(const String& text, uint8_t size, int16_t maxW, uint32_t now) const {
    (void)maxW;
    const int16_t w = (int16_t)_display->measureText(text.c_str(), size);
    const int16_t gap = 48; // blank run before the text loops back in
    const int32_t cycle = (int32_t)w + gap;
    if (cycle <= 0) return 0;
    const uint32_t moved = (now - _marqueeStartMs) / 22; // ~45 px/s
    return (int16_t)(moved % (uint32_t)cycle);
}

bool ScorePlugin::marqueeActive() const {
    // Cached: rebuilding the string / measuring text every loop caused heap
    // churn and stutter. Team names are stable; only the bottom line scrolls.
    return hasData() && _bottomOverflows;
}

void ScorePlugin::drawRow(const Layout& L, uint8_t index, uint32_t now, bool flashScore) const {
    const Settings::ThemePalette* palette = Settings::getThemePalette(_settings->getTheme());
    const uint16_t scoreColor = Display::hexToRGB565(palette->lyricHex);
    const uint16_t accent = Display::hexToRGB565(palette->accentHex);
    const uint16_t dimColor = Display::hexToRGB565(palette->dimHex);

    const String& name = (index == 0) ? _teamA : _teamB;
    const String& rawScore = (index == 0) ? _scoreA : _scoreB;
    const String score = scoreMain(rawScore);       // bare score, e.g. "172/2"
    const String over = scoreTopDetail(rawScore);   // e.g. "14/20 ov"
    const int16_t rowY = (index == 0) ? L.row1Y : L.row2Y;

    // Clear only this row's band, so an animated frame never flashes the rest
    // of the panel. Name and score share a baseline, so one band covers both.
    const int16_t nameTop = rowY - 14 * L.nameSize;
    const int16_t top = nameTop - 2;
    const int16_t bottom = rowY + 12;
    _display->drawFilledRectangle(0, top, L.dispW, bottom - top, BLACK);

    // Name on the left: kept STABLE (no marquee). Right after it the bare score,
    // then the over-rate ("14/20 ov") in the smaller font. Nothing draws over
    // the name: the name shrinks first, then any tail simply clips.
    const int16_t left = 4;
    const int16_t right = 2;
    const int16_t gap = 5;
    const int16_t overGap = 3;

    const uint8_t scoreSize = L.scoreSize;
    const int16_t scoreW = (score.length() > 0)
        ? (int16_t)_display->measureText(score.c_str(), scoreSize) : 0;
    const int16_t overW = (over.length() > 0)
        ? (int16_t)_display->measureText(over.c_str(), 1) : 0;

    int16_t nameMax = L.dispW - left - right
        - (scoreW > 0 ? scoreW + gap : 0)
        - (overW > 0 ? overW + overGap : 0);
    if (nameMax < 1) nameMax = 1;
    // Draw the name whole when it fits; only reserve ".." room if it must shrink.
    String fitted = name;
    if ((int16_t)_display->measureText(name.c_str(), L.nameSize) > nameMax) {
        fitted = _display->fitText(name, L.nameSize, nameMax);
    }
    _display->drawText(left, rowY, fitted.c_str(), WHITE, L.nameSize);

    const int16_t nameW = (int16_t)_display->measureText(fitted.c_str(), L.nameSize);
    int16_t x = left + nameW + gap;

    // Score immediately to the right of the name; blinks between the theme
    // score colour and the accent for a moment after it changes.
    if (score.length() > 0) {
        const bool on = flashScore && (((now / 220) & 1) == 0);
        const uint16_t color = on ? accent : scoreColor;
        _display->drawText(x, rowY, score.c_str(), color, scoreSize);
        x += scoreW + overGap;
    }
    // Over-rate detail, dimmed and smaller, tucked after the score.
    if (over.length() > 0) {
        _display->drawText(x, rowY, over.c_str(), dimColor, 1);
    }
}

void ScorePlugin::drawStatus(const Layout& L, uint32_t now) const {
    const String line = statusLine();
    if (line.length() == 0) return;
    const Settings::ThemePalette* palette = Settings::getThemePalette(_settings->getTheme());
    const uint16_t accent = Display::hexToRGB565(palette->accentHex);

    const int16_t top = L.statusY - 14 * 2 - 2;
    _display->drawFilledRectangle(0, top, L.dispW, L.statusY - top, BLACK);

    const int16_t avail = L.dispW - 20;
    if (overflows(line, 2, avail)) {
        // Too long to sit still: scroll it, left across the whole line.
        const int16_t off = marqueeOffset(line, 2, avail, now);
        _display->drawText(L.dispW - off, L.statusY, line.c_str(), accent, 2);
    } else {
        _display->drawCenteredTextAt(L.statusY, line.c_str(), accent, 2);
    }
}

void ScorePlugin::rebuildBottomLine() {
    String line;
    const String dA = scoreBottomDetail(_scoreA);
    const String dB = scoreBottomDetail(_scoreB);
    if (dA.length()) line = dA;
    if (dB.length() && dB != dA) {
        if (line.length()) line += "  ";
        line += dB;
    }
    if (_status.length()) {
        if (line.length()) line += "  ";
        line += _status;
    }
    _bottomLine = line;
    const int16_t avail = _display->width() - 20;
    _bottomOverflows = (_bottomLine.length() > 0) &&
                       ((int16_t)_display->measureText(_bottomLine.c_str(), 2) > avail);
}

void ScorePlugin::animFrame() {
    const Layout L = layout();
    const uint32_t now = millis();
    const bool flashA = now < _flashUntilA;
    const bool flashB = now < _flashUntilB;
    // Names are static, so only a flashing score needs a row repaint. The
    // bottom line repaints only while its marquee is actually running.
    if (flashA) drawRow(L, 0, now, flashA);
    if (flashB) drawRow(L, 1, now, flashB);
    if (_bottomOverflows) drawStatus(L, now);
}

void ScorePlugin::triggerGoal(uint8_t side) {
    _goalActive = true;
    _goalSide = side;
    const uint32_t now = millis();
    _goalStartMs = now;
    _lastAnimMs = now - ANIM_FRAME_MS; // draw a celebration frame immediately
}

void ScorePlugin::drawGoalFrame(uint32_t now) const {
    const Layout L = layout();
    const Settings::ThemePalette* palette = Settings::getThemePalette(_settings->getTheme());
    const uint16_t accent = Display::hexToRGB565(palette->accentHex);
    const uint32_t elapsed = now - _goalStartMs;
    const uint8_t hueBase = (uint8_t)(elapsed / 8);
    const int16_t cx = L.dispW / 2;
    const int16_t cy = L.dispH / 2;

    // Transient full-screen effect, so a clear per frame is fine here.
    _display->fillScreen(BLACK);

    // Expanding concentric rings in a cycling rainbow.
    for (uint8_t i = 0; i < 4; i++) {
        const int16_t r = (int16_t)(((elapsed / 6) + i * 34) % 150) + 6;
        _display->drawCircle(cx, cy, r, Display::hueToRGB565((uint8_t)(hueBase + i * 24)));
    }

    // Football above the banner (emoji sprite), then the big rainbow + glow
    // "GOAL!" banner (reuses the lyrics screen's renderer).
    _display->drawEmojiCentered(cx, L.landscape ? 22 : 42, 0x26BD, 2);
    _display->drawCenteredTextEffect(L.landscape ? 100 : 140, "GOAL!", WHITE, 4,
                                     true, true, hueBase);

    const String& team = (_goalSide == 0) ? _teamA : _teamB;
    const String& goalScore = (_goalSide == 0) ? _scoreA : _scoreB;
    String fitted = _display->fitText(team, 2, L.dispW - 24);
    _display->drawCenteredTextAt(L.landscape ? 150 : 200, fitted.c_str(), WHITE, 2);
    _display->drawCenteredTextAt(L.landscape ? 200 : 292, goalScore.c_str(), accent, 3);
}

bool ScorePlugin::handleApi(AsyncWebServerRequest* request, const String& action) {
    if (action == "set") {
        auto get = [&](const char* key) -> String {
            return request->hasParam(key, true) ? request->getParam(key, true)->value() : String();
        };
        setScore(get("sport"), get("teamA"), get("scoreA"), get("teamB"), get("scoreB"), get("status"));
        LOG_INFO(TAG, "Score updated");
        request->send(200, "text/plain", "OK");
        return true;
    }
    if (action == "sport") {
        if (request->hasParam("sport", true)) {
            setSport(request->getParam("sport", true)->value());
            LOG_INFO(TAG, (String("Sport set to ") + _sport).c_str());
        }
        request->send(200, "text/plain", "OK");
        return true;
    }
    if (action == "clear") {
        clear();
        LOG_INFO(TAG, "Scores cleared");
        request->send(200, "text/plain", "OK");
        return true;
    }
    if (action == "football") {
        if (request->hasParam("league", true)) {
            _settings->setFootballLeagueSlug(request->getParam("league", true)->value());
        }
        if (request->hasParam("event", true)) {
            _settings->setFootballEventId(request->getParam("event", true)->value());
        }
        if (request->hasParam("date", true)) {
            _settings->setFootballDate(request->getParam("date", true)->value());
        }
        if (request->hasParam("interval", true)) {
            int seconds = request->getParam("interval", true)->value().toInt();
            if (seconds > 0) _settings->setFootballPollSeconds((uint16_t)seconds);
        }
        bool ok = _football.fetchNow(); // refresh the match selector immediately
        LOG_INFO(TAG, ok ? "ESPN settings saved" : "ESPN settings saved; fetch failed");
        request->send(ok ? 200 : 502, "text/plain", ok ? "OK" : _football.lastError());
        return true;
    }
    if (action == "cricket") {
        if (request->hasParam("league", true)) {
            int id = request->getParam("league", true)->value().toInt();
            if (id > 0) _settings->setCricketLeagueId((uint32_t)id);
        }
        if (request->hasParam("event", true)) _settings->setCricketEventId(request->getParam("event", true)->value());
        if (request->hasParam("date", true)) _settings->setCricketDate(request->getParam("date", true)->value());
        if (request->hasParam("interval", true)) {
            int seconds = request->getParam("interval", true)->value().toInt();
            if (seconds > 0) _settings->setCricketPollSeconds((uint16_t)seconds);
        }
        bool ok = _cricket.fetchNow();
        request->send(ok ? 200 : 502, "text/plain", ok ? "OK" : _cricket.lastError());
        return true;
    }
    if (action == "refresh") {
        bool ok = _football.fetchNow();
        LOG_INFO(TAG, ok ? "ESPN refresh OK" : "ESPN refresh failed");
        request->send(200, "text/plain", ok ? "OK" : "Fetch failed");
        return true;
    }
    if (action == "browse") {
        auto get = [&](const char* key) -> String {
            return request->hasParam(key, true) ? request->getParam(key, true)->value() : String();
        };
        browseScoreboard(get("sport"), request);
        return true;
    }
    return false;
}

// Look-ahead fetch for the dashboard's Browse buttons. The league/date come
// straight from the form and nothing is persisted, so the device feed keeps
// running on its saved settings until the user actually saves the choice.
void ScorePlugin::browseScoreboard(const String& kind, AsyncWebServerRequest* request) {
    auto get = [&](const char* key) -> String {
        return request->hasParam(key, true) ? request->getParam(key, true)->value() : String();
    };
    const String league = get("league");
    String date = get("date");
    String digits;
    for (size_t i = 0; i < date.length(); ++i) {
        if (date[i] >= '0' && date[i] <= '9') digits += date[i];
    }
    date = digits; // same YYYYMMDD shape the live feed expects

    String list;
    const uint32_t started = millis();
    bool ok = false;
    String error = "Unknown sport";
    // EspnApi validates the league and owns the error text for both sports.
    if (kind == "football") {
        ok = _football.fetchScoreboardFor(league, date, &list);
        if (!ok) error = _football.lastError();
    } else if (kind == "cricket") {
        ok = _cricket.fetchScoreboardFor(league, date, &list);
        if (!ok) error = _cricket.lastError();
    }
    LOG_INFO(TAG, (String("ESPN browse ") + kind + " " + league +
                   (ok ? String(" ok ") : String(" failed ")) +
                   String((millis() - started) / 1000) + "s").c_str());

    if (!ok) {
        JsonDocument err;
        err["ok"] = false;
        err["error"] = error;
        String out;
        serializeJson(err, out);
        request->send(200, "application/json", out);
        return;
    }
    if (list.isEmpty()) list = String("{\"ok\":true,\"events\":[]}");
    request->send(200, "application/json", list);
}

void ScorePlugin::buildStatusJson(JsonObject obj) {
    obj["sport"] = _sport;
    obj["teamA"] = _teamA;
    obj["scoreA"] = _scoreA;
    obj["teamB"] = _teamB;
    obj["scoreB"] = _scoreB;
    obj["status"] = _status;

    // ESPN feed state (the public endpoint does not need a key).
    obj["footballLeague"] = _settings->getFootballLeagueSlug();
    obj["footballEvent"] = _settings->getFootballEventId();
    obj["footballDate"] = _settings->getFootballDate();
    obj["footballInterval"] = _settings->getFootballPollSeconds();
    obj["footballFetched"] = _football.hasData();
    obj["footballLive"] = _football.isLive();
    obj["footballError"] = _football.lastError();
    obj["footballMatch"] = _football.hasData()
        ? (_football.homeName() + " vs " + _football.awayName() + " (" + _football.statusText() + ")")
        : String("");
    JsonArray choices = obj["footballEvents"].to<JsonArray>();
    for (const auto& choice : _football.events()) {
        JsonObject item = choices.add<JsonObject>();
        item["id"] = choice.id;
        item["label"] = choice.label;
    }
    // Human-readable series/league names for the dashboard selects.
    obj["footballLeagueName"] = _football.leagueName();
    obj["cricketLeagueName"] = _cricket.leagueName();
    obj["cricketLeague"] = _settings->getCricketLeagueId();
    obj["cricketEvent"] = _settings->getCricketEventId();
    obj["cricketDate"] = _settings->getCricketDate();
    obj["cricketInterval"] = _settings->getCricketPollSeconds();
    obj["cricketFetched"] = _cricket.hasData();
    obj["cricketLive"] = _cricket.isLive();
    obj["cricketError"] = _cricket.lastError();
    obj["cricketMatch"] = _cricket.hasData() ? (_cricket.homeName() + " vs " + _cricket.awayName() + " (" + _cricket.statusText() + ")") : String("");
    JsonArray cricketChoices = obj["cricketEvents"].to<JsonArray>();
    for (const auto& choice : _cricket.events()) {
        JsonObject item = cricketChoices.add<JsonObject>();
        item["id"] = choice.id; item["label"] = choice.label;
    }
}

String ScorePlugin::uiSection() const {
    // Reuses the dashboard's shared CSS (.card, .row, .value, .btn-inline, .hint
    // from DashboardAssets.h) and helpers (postData, registerStatusUpdater).
    // Values are pushed to the device; ESPN lookups (Browse matches) also run on
    // the device, via the scores/browse API, so the browser itself never talks to
    // the internet.
    String html;
    html.reserve(16384);
    html += R"rawliteral(
    <div class="card">
      <h2>Scores</h2>
      <label>Sport</label>
      <select id="scoreSport" onchange="postData('/api/plugin/scores/sport', 'sport=' + this.value)">
        <option value="cricket">Cricket</option>
        <option value="football">Football</option>
      </select>
      <div class="row"><span>On screen:</span> <span class="value" id="scoreCurrent">-</span></div>
      <br>
      <label>Team A</label>
      <input type="text" id="scoreTeamA" placeholder="e.g. India">
      <label>Score A</label>
      <input type="text" id="scoreScoreA" placeholder="e.g. 187/6">
      <label>Team B</label>
      <input type="text" id="scoreTeamB" placeholder="e.g. Australia">
      <label>Score B</label>
      <input type="text" id="scoreScoreB" placeholder="e.g. 45/2">
      <label>Status line</label>
      <input type="text" id="scoreStatusLine" placeholder="e.g. India need 42 off 30">
      <div class="btn-inline">
        <button onclick="saveScores()">Show on Screen</button>
        <button class="danger" onclick="clearScores()">Clear</button>
      </div>
      <p class="hint">Cricket and football use the same fields &mdash; just pick the sport. These values are pushed to the device and always win over the live feed below.</p>
    </div>
)rawliteral";

    // Cricket and football share one skeleton (see espnCardHtml above); only the
    // labels, element-id prefix, option list and buttons differ.
    html += espnCardHtml(
        "Live Cricket (ESPN)", "cric", "cricket", "Series",
        "Custom ESPN series ID (optional)", "number", "e.g. 8048",
        R"opts(
        <option value="8048">Indian Premier League</option>
        <option value="1490534">Big Bash League</option>
        <option value="1512690">T20 Blast (England)</option>
        <option value="1521176">The Hundred (Men)</option>
        <option value="1521193">The Hundred (Women)</option>
        <option value="1502138">ICC Men's T20 World Cup</option>
        <option value="1483859">ICC Women's T20 World Cup</option>
        <option value="1496488">India tour of England 2026</option>
        <option value="1488246">Australia Women tour of India 2025/26</option>
)opts",
        R"btns(      <button onclick="saveCricket()">Load series / save match</button>
)btns",
        "Uses ESPN directly, with no key. Browse lists the selected series' matches on ESPN's current scoreboard without changing the device feed; save a match to show it, or leave Match on Automatic.");

    html += espnCardHtml(
        "Live Football (ESPN)", "fb", "football", "League",
        "Custom ESPN league slug (optional)", "text", "e.g. aus.1 or eng.2",
        R"opts(
        <option value="eng.1">English Premier League</option>
        <option value="esp.1">Spanish LALIGA</option>
        <option value="ger.1">German Bundesliga</option>
        <option value="ita.1">Italian Serie A</option>
        <option value="fra.1">French Ligue 1</option>
        <option value="usa.1">Major League Soccer</option>
        <option value="uefa.champions">UEFA Champions League</option>
        <option value="uefa.europa">UEFA Europa League</option>
        <option value="uefa.europa.conf">UEFA Conference League</option>
        <option value="eng.fa">English FA Cup</option>
        <option value="eng.league_cup">English Carabao Cup</option>
        <option value="ger.dfb_pokal">German DFB-Pokal</option>
        <option value="ita.coppa_italia">Italian Coppa Italia</option>
        <option value="esp.copa_del_rey">Spanish Copa del Rey</option>
        <option value="fifa.world">FIFA World Cup</option>
        <option value="fifa.worldq.uefa">FIFA World Cup Qualifying (UEFA)</option>
        <option value="conmebol.libertadores">CONMEBOL Libertadores</option>
)opts",
        R"btns(      <div class="btn-inline">
        <button onclick="saveFootball()">Load league / save match</button>
        <button onclick="refreshFootball()">Refresh now</button>
      </div>
)btns",
        "No ESPN key is needed. Browse lists the selected league's matches on ESPN's current scoreboard without changing the device feed; save a match to show it. Leave Match on Automatic to use a live match, then the next scheduled one.");

    html += R"rawliteral(  <script>
    function updateScoresStatus(data) {
      var s = data.plugins && data.plugins.scores;
      if (!s) return;
      var sportEl = document.getElementById('scoreSport');
      if (document.activeElement !== sportEl && s.sport) sportEl.value = s.sport;
      document.getElementById('scoreCurrent').innerText =
        (s.teamA || '-') + ' ' + (s.scoreA || '') +
        (s.teamB ? '  vs  ' + s.teamB + ' ' + (s.scoreB || '') : '');
      var fields = {scoreTeamA:'teamA', scoreScoreA:'scoreA', scoreTeamB:'teamB', scoreScoreB:'scoreB', scoreStatusLine:'status'};
      for (var id in fields) {
        var el = document.getElementById(id);
        if (el && document.activeElement !== el) el.value = s[fields[id]] || '';
      }
    }
    function saveScores() {
      var body = 'sport=' + encodeURIComponent(document.getElementById('scoreSport').value) +
        '&teamA=' + encodeURIComponent(document.getElementById('scoreTeamA').value) +
        '&scoreA=' + encodeURIComponent(document.getElementById('scoreScoreA').value) +
        '&teamB=' + encodeURIComponent(document.getElementById('scoreTeamB').value) +
        '&scoreB=' + encodeURIComponent(document.getElementById('scoreScoreB').value) +
        '&status=' + encodeURIComponent(document.getElementById('scoreStatusLine').value);
      postData('/api/plugin/scores/set', body);
    }
    function clearScores() {
      if (confirm('Clear the scoreboard?')) postData('/api/plugin/scores/clear', '');
    }
    // True once a Browse is started, until the user saves a match or changes
    // the series/league — so the status poller does not overwrite the browse
    // result with the saved feed's match list.
    var cricBrowsing = false, fbBrowsing = false;
    function browseMatches(sport) {
      var leagueSel, leagueCustom, dateEl, btnId, evId;
      if (sport === 'cricket') {
        leagueSel = 'cricLeague'; leagueCustom = 'cricLeagueCustom'; dateEl = 'cricDate'; btnId = 'cricBrowseBtn'; evId = 'cricEvent';
      } else {
        leagueSel = 'fbLeague'; leagueCustom = 'fbLeagueCustom'; dateEl = 'fbDate'; btnId = 'fbBrowseBtn'; evId = 'fbEvent';
      }
      var custom = document.getElementById(leagueCustom).value.trim();
      var league = custom || document.getElementById(leagueSel).value;
      if (!league) { alert('Choose a series or league first'); return; }
      var date = document.getElementById(dateEl).value || '';
      var body = 'sport=' + encodeURIComponent(sport) +
        '&league=' + encodeURIComponent(league) +
        '&date=' + encodeURIComponent(date);
      var ev = document.getElementById(evId);
      if (sport === 'cricket') cricBrowsing = true; else fbBrowsing = true;
      ev.options.length = 0;
      ev.add(new Option('Loading\u2026', ''));
      var btn = document.getElementById(btnId);
      if (btn) { btn.disabled = true; btn.dataset.label = btn.innerText; btn.innerText = 'Loading\u2026'; }
      fetch('/api/plugin/scores/browse', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: body
      }).then(function(r) { return r.json(); }).then(function(res) {
        fillEventSelect(evId, res && res.ok ? (res.events || []) : null,
          res && res.error ? res.error : 'Browse failed');
      }).catch(function() {
        fillEventSelect(evId, null, 'Browse failed');
      }).finally(function() {
        if (btn) { btn.disabled = false; btn.innerText = btn.dataset.label; }
      });
    }
    function fillEventSelect(evId, events, errorText) {
      var ev = document.getElementById(evId);
      ev.options.length = 0;
      ev.add(new Option('Automatic: live, then next match', ''));
      if (!events) {
        ev.add(new Option(errorText || 'Browse failed', ''));
        return;
      }
      if (!events.length) {
        ev.add(new Option('No matches on ESPN\u2019s current scoreboard', ''));
        return;
      }
      events.forEach(function(item) {
        ev.add(new Option(item.label, item.id));
      });
    }
    function updateCricket(data) {
      var s = data.plugins && data.plugins.scores; if (!s) return;
      document.getElementById('cricMatch').innerText = s.cricketMatch || (s.cricketFetched ? '-' : 'No data yet');
      document.getElementById('cricFeedStatus').innerText = s.cricketError || (s.cricketLive ? 'Live' : (s.cricketFetched ? 'Ready' : 'Waiting'));
      var league = document.getElementById('cricLeague');
      if (document.activeElement !== league && s.cricketLeague) {
        var found = Array.prototype.some.call(league.options, function(o){return o.value == s.cricketLeague;});
        if (!found) {
          var seriesLabel = s.cricketLeagueName ? (s.cricketLeagueName + ' (' + s.cricketLeague + ')') : (s.cricketLeague + ' (custom)');
          league.add(new Option(seriesLabel, s.cricketLeague));
        }
        league.value = s.cricketLeague;
      }
      var date = document.getElementById('cricDate');
      if (document.activeElement !== date) { var d=s.cricketDate||''; date.value=d.length===8?d.slice(0,4)+'-'+d.slice(4,6)+'-'+d.slice(6,8):''; }
      var interval = document.getElementById('cricInterval'); if (document.activeElement !== interval && s.cricketInterval) interval.value=s.cricketInterval;
      var event = document.getElementById('cricEvent');
      if (document.activeElement !== event && !cricBrowsing && s.cricketEvents) {
        var previous=s.cricketEvent||''; event.options.length=0; event.add(new Option('Automatic: live, then next match',''));
        s.cricketEvents.forEach(function(item){event.add(new Option(item.label,item.id));}); event.value=previous;
      }
    }
    function saveCricket() {
      var custom=document.getElementById('cricLeagueCustom').value.trim();
      var body='league='+encodeURIComponent(custom||document.getElementById('cricLeague').value)+
        '&event='+encodeURIComponent(document.getElementById('cricEvent').value)+
        '&date='+encodeURIComponent(document.getElementById('cricDate').value)+
        '&interval='+encodeURIComponent(document.getElementById('cricInterval').value);
      cricBrowsing = false; // saved feed becomes authoritative again
      postData('/api/plugin/scores/cricket',body);
    }
    function updateFootball(data) {
      var s = data.plugins && data.plugins.scores;
      if (!s) return;
      document.getElementById('fbMatch').innerText =
        s.footballMatch || (s.footballFetched ? '-' : 'No data yet');
      document.getElementById('fbFeedStatus').innerText = s.footballError ||
        (s.footballLive ? 'Live' : (s.footballFetched ? 'Ready' : 'Waiting'));
      var lg = document.getElementById('fbLeague');
      if (document.activeElement !== lg && s.footballLeague) {
        var known = Array.prototype.some.call(lg.options, function(o){ return o.value === s.footballLeague; });
        if (!known) {
          var leagueLabel = s.footballLeagueName ? (s.footballLeagueName + ' (' + s.footballLeague + ')') : (s.footballLeague + ' (custom slug)');
          lg.add(new Option(leagueLabel, s.footballLeague));
        }
        lg.value = s.footballLeague;
      }
      var iv = document.getElementById('fbInterval');
      if (document.activeElement !== iv && s.footballInterval) iv.value = s.footballInterval;
      var dt = document.getElementById('fbDate');
      if (document.activeElement !== dt) {
        var compactDate = s.footballDate || '';
        dt.value = compactDate.length === 8 ? compactDate.slice(0,4) + '-' + compactDate.slice(4,6) + '-' + compactDate.slice(6,8) : '';
      }
      var ev = document.getElementById('fbEvent');
      if (document.activeElement !== ev && !fbBrowsing && s.footballEvents) {
        var previous = s.footballEvent || '';
        ev.options.length = 0;
        ev.add(new Option('Automatic: live, then next match', ''));
        s.footballEvents.forEach(function(item) { ev.add(new Option(item.label, item.id)); });
        ev.value = previous;
      }
    }
    function saveFootball() {
      var customLeague = document.getElementById('fbLeagueCustom').value.trim();
      var body = 'league=' + encodeURIComponent(customLeague || document.getElementById('fbLeague').value) +
        '&event=' + encodeURIComponent(document.getElementById('fbEvent').value) +
        '&date=' + encodeURIComponent(document.getElementById('fbDate').value) +
        '&interval=' + encodeURIComponent(document.getElementById('fbInterval').value);
      fbBrowsing = false; // saved feed becomes authoritative again
      postData('/api/plugin/scores/football', body);
    }
    function refreshFootball() {
      postData('/api/plugin/scores/refresh', '');
    }
    registerStatusUpdater(updateScoresStatus);
    registerStatusUpdater(updateCricket);
    registerStatusUpdater(updateFootball);
  </script>
    )rawliteral";
    return html;
}
