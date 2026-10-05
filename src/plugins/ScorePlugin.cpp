#include "ScorePlugin.h"
#include "../Logger.h"
#include <ESPAsyncWebServer.h>

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

ScorePlugin::ScorePlugin(Display* display, Settings* settings, WiFiManager* wifi)
    : _display(display),
      _settings(settings),
      _football(*settings, *wifi),
      _cricket(*settings, *wifi),
      _sport(settings->getScoreSport()),
      _teamA(),
      _scoreA(),
      _teamB(),
      _scoreB(),
      _status(),
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
    // Runs every loop regardless of the active screen, so the board is already
    // populated when the user switches to it and the daily API budget is spent
    // on a steady schedule rather than only while visible.
    _football.update();
    _cricket.update();
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
    _dirty = true;
}

bool ScorePlugin::hasData() const {
    return _teamA.length() > 0 || _teamB.length() > 0 || _status.length() > 0;
}

void ScorePlugin::enter() {
    _marqueeStartMs = millis();
    _dirty = true;
}

void ScorePlugin::onRotationChange() {
    _marqueeStartMs = millis();
    _dirty = true;
}

void ScorePlugin::onThemeChange() {
    _dirty = true;
}

void ScorePlugin::update() {
    // ESPN drives only the football board, so a cricket screen remains manual.
    if (_sport == "football" && _football.hasData()) {
        String sa = _football.homeGoals() < 0 ? String("-") : String(_football.homeGoals());
        String sb = _football.awayGoals() < 0 ? String("-") : String(_football.awayGoals());
        if (_teamA != _football.homeName() || _scoreA != sa ||
            _teamB != _football.awayName() || _scoreB != sb ||
            _status != _football.statusText()) {
            setScore("football", _football.homeName(), sa,
                     _football.awayName(), sb, _football.statusText());
        }
    }
    if (_sport == "cricket" && _cricket.hasData()) {
        if (_teamA != _cricket.homeName() || _scoreA != _cricket.homeScore() ||
            _teamB != _cricket.awayName() || _scoreB != _cricket.awayScore() ||
            _status != _cricket.statusText()) {
            setScore("cricket", _cricket.homeName(), _cricket.homeScore(),
                     _cricket.awayName(), _cricket.awayScore(), _cricket.statusText());
        }
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

    // Header + separator. Remember unifont ink renders 14*size px ABOVE the
    // given y, so these y values already account for it.
    _display->drawCenteredTextAt(L.headerY, sportLabel, accent, 2);
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
    L.scoreSize = L.landscape ? 3 : 4;
    L.row1Y = L.landscape ? 100 : 112;
    L.row2Y = L.landscape ? 158 : 188;
    L.nameMaxW = (int16_t)(L.dispW * 0.52);
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
    if (!hasData()) return false;
    const Layout L = layout();
    return overflows(_teamA, L.nameSize, L.nameMaxW) ||
           overflows(_teamB, L.nameSize, L.nameMaxW) ||
           overflows(_status, 2, L.dispW - 20);
}

void ScorePlugin::drawRow(const Layout& L, uint8_t index, uint32_t now, bool flashScore) const {
    const Settings::ThemePalette* palette = Settings::getThemePalette(_settings->getTheme());
    const uint16_t scoreColor = Display::hexToRGB565(palette->lyricHex);
    const uint16_t accent = Display::hexToRGB565(palette->accentHex);

    const String& name = (index == 0) ? _teamA : _teamB;
    const String& score = (index == 0) ? _scoreA : _scoreB;
    const int16_t rowY = (index == 0) ? L.row1Y : L.row2Y;

    // Clear only this row's band, so an animated frame never flashes the rest
    // of the panel. The score sits above the name baseline, hence the min().
    const int16_t nameTop = rowY - 14 * L.nameSize;
    const int16_t scoreTop = rowY + 10 - 14 * L.scoreSize;
    const int16_t top = ((nameTop < scoreTop) ? nameTop : scoreTop) - 2;
    const int16_t bottom = rowY + 12;
    _display->drawFilledRectangle(0, top, L.dispW, bottom - top, BLACK);

    // Name on the left: scroll it when it will not fit the name column.
    const int16_t nameX = 15;
    if (overflows(name, L.nameSize, L.nameMaxW)) {
        const int16_t off = marqueeOffset(name, L.nameSize, L.nameMaxW, now);
        _display->drawText(nameX - off, rowY, name.c_str(), WHITE, L.nameSize);
    } else {
        _display->drawText(nameX, rowY, name.c_str(), WHITE, L.nameSize);
    }

    // Big right-aligned score; blinks between the theme score colour and the
    // accent for a moment after it changes.
    if (score.length() > 0) {
        const bool on = flashScore && (((now / 220) & 1) == 0);
        const uint16_t color = on ? accent : scoreColor;
        const int16_t w = (int16_t)_display->measureText(score.c_str(), L.scoreSize);
        _display->drawText(L.dispW - 15 - w, rowY + 10, score.c_str(), color, L.scoreSize);
    }
}

void ScorePlugin::drawStatus(const Layout& L, uint32_t now) const {
    if (_status.length() == 0) return;
    const Settings::ThemePalette* palette = Settings::getThemePalette(_settings->getTheme());
    const uint16_t accent = Display::hexToRGB565(palette->accentHex);

    const int16_t top = L.statusY - 14 * 2 - 2;
    _display->drawFilledRectangle(0, top, L.dispW, L.statusY - top, BLACK);

    const int16_t avail = L.dispW - 20;
    if (overflows(_status, 2, avail)) {
        // Too long to sit still: scroll it in from the right edge instead of
        // truncating it with "..".
        const int16_t off = marqueeOffset(_status, 2, avail, now);
        _display->drawText(L.dispW - off, L.statusY, _status.c_str(), accent, 2);
    } else {
        _display->drawCenteredTextAt(L.statusY, _status.c_str(), accent, 2);
    }
}

void ScorePlugin::animFrame() {
    const Layout L = layout();
    const uint32_t now = millis();
    const bool flashA = now < _flashUntilA;
    const bool flashB = now < _flashUntilB;
    if (overflows(_teamA, L.nameSize, L.nameMaxW) || flashA) drawRow(L, 0, now, flashA);
    if (overflows(_teamB, L.nameSize, L.nameMaxW) || flashB) drawRow(L, 1, now, flashB);
    if (overflows(_status, 2, L.dispW - 20)) drawStatus(L, now);
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

    // Big rainbow + glow "GOAL!" banner (reuses the lyrics screen's renderer).
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
            if (id > 0) _settings->setCricketLeagueId((uint16_t)id);
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
    return false;
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
    // Reuses the dashboard's existing CSS (.card, .row, .value) and helpers
    // (postData, registerStatusUpdater). Values are pushed to the device; the
    // card never fetches anything from the internet.
    return R"rawliteral(
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
      <div style="display:flex; gap:10px;">
        <button onclick="saveScores()">Show on Screen</button>
        <button class="danger" onclick="clearScores()">Clear</button>
      </div>
      <p style="color:#888; font-size:0.85em; margin-bottom:0;">Cricket and football use the same fields &mdash; just pick the sport. These values are pushed to the device and always win over the live feed below.</p>
    </div>

    <div class="card">
      <h2>Live Cricket (ESPN)</h2>
      <div class="row"><span>Displayed match:</span> <span class="value" id="cricMatch">-</span></div>
      <div class="row"><span>Feed status:</span> <span class="value" id="cricFeedStatus">-</span></div>
      <label>Series</label>
      <select id="cricLeague" onchange="document.getElementById('cricEvent').value=''"><option value="8048">Indian Premier League</option></select>
      <label>Custom ESPN series ID</label>
      <input type="number" id="cricLeagueCustom" placeholder="e.g. 8048">
      <label>Match</label>
      <select id="cricEvent"><option value="">Automatic: live, then next match</option></select>
      <label>Match date (optional, for finished scores)</label>
      <input type="date" id="cricDate" onchange="document.getElementById('cricEvent').value=''">
      <label>Refresh interval (seconds)</label>
      <input type="number" id="cricInterval" min="30" max="3600" placeholder="30">
      <button onclick="saveCricket()">Load series / save match</button>
      <p style="color:#888; font-size:0.85em; margin-bottom:0;">Uses ESPN directly, with no key. IPL is preselected; enter another ESPN cricket series ID if needed.</p>
    </div>

    <div class="card">
      <h2>Live Football (ESPN)</h2>
      <div class="row"><span>Displayed match:</span> <span class="value" id="fbMatch">-</span></div>
      <div class="row"><span>Feed status:</span> <span class="value" id="fbFeedStatus">-</span></div>
      <label>League</label>
      <select id="fbLeague" onchange="document.getElementById('fbEvent').value=''; document.getElementById('fbLeagueCustom').value=''">
        <option value="eng.1">English Premier League</option>
        <option value="esp.1">Spanish LALIGA</option>
        <option value="ger.1">German Bundesliga</option>
        <option value="ita.1">Italian Serie A</option>
        <option value="fra.1">French Ligue 1</option>
        <option value="usa.1">Major League Soccer</option>
        <option value="uefa.champions">UEFA Champions League</option>
        <option value="uefa.europa">UEFA Europa League</option>
        <option value="fifa.world">FIFA World Cup</option>
      </select>
      <label>Custom ESPN league slug (optional)</label>
      <input type="text" id="fbLeagueCustom" placeholder="e.g. eng.fa or fifa.worldq.uefa">
      <label>Match</label>
      <select id="fbEvent"><option value="">Automatic: live, then next match</option></select>
      <label>Match date (optional, for finished scores)</label>
      <input type="date" id="fbDate" onchange="document.getElementById('fbEvent').value=''">
      <label>Refresh interval (seconds)</label>
      <input type="number" id="fbInterval" min="30" max="3600" placeholder="30">
      <div style="display:flex; gap:10px;">
        <button onclick="saveFootball()">Load league / save match</button>
        <button onclick="refreshFootball()">Refresh now</button>
      </div>
      <p style="color:#888; font-size:0.85em; margin-bottom:0;">No ESPN key is needed. Load a league first; the Match list shows matches on ESPN's current scoreboard. Leave Match on Automatic to use a live match, then the next scheduled one.</p>
    </div>
  <script>
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
    function updateCricket(data) {
      var s = data.plugins && data.plugins.scores; if (!s) return;
      document.getElementById('cricMatch').innerText = s.cricketMatch || (s.cricketFetched ? '-' : 'No data yet');
      document.getElementById('cricFeedStatus').innerText = s.cricketError || (s.cricketLive ? 'Live' : (s.cricketFetched ? 'Ready' : 'Waiting'));
      var league = document.getElementById('cricLeague');
      if (document.activeElement !== league && s.cricketLeague) {
        var found = Array.prototype.some.call(league.options, function(o){return o.value == s.cricketLeague;});
        if (!found) league.add(new Option(s.cricketLeague + ' (custom)', s.cricketLeague));
        league.value = s.cricketLeague;
      }
      var date = document.getElementById('cricDate');
      if (document.activeElement !== date) { var d=s.cricketDate||''; date.value=d.length===8?d.slice(0,4)+'-'+d.slice(4,6)+'-'+d.slice(6,8):''; }
      var interval = document.getElementById('cricInterval'); if (document.activeElement !== interval && s.cricketInterval) interval.value=s.cricketInterval;
      var event = document.getElementById('cricEvent');
      if (document.activeElement !== event && s.cricketEvents) {
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
        if (!known) lg.add(new Option(s.footballLeague + ' (custom slug)', s.footballLeague));
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
      if (document.activeElement !== ev && s.footballEvents) {
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
}
