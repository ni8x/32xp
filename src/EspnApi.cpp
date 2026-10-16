#include "EspnApi.h"
#include "Logger.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

const EspnSport ESPN_FOOTBALL = {"FootballApi", "https://site.api.espn.com/apis/site/v2/sports/soccer/", false};
const EspnSport ESPN_CRICKET = {"CricketApi", "https://site.api.espn.com/apis/site/v2/sports/cricket/", true};

// One allow-list covers both sports: soccer also wants the league name and the
// match detail, cricket the status summary. ArduinoJson keeps only what the
// filter names, so the unused branches cost nothing.
static void buildFilter(JsonDocument& filter) {
    filter["leagues"][0]["name"] = true;
    JsonObject event = filter["events"][0].to<JsonObject>();
    event["id"] = true;
    event["name"] = true;
    event["status"]["type"]["state"] = true;
    event["status"]["type"]["shortDetail"] = true;
    event["status"]["type"]["detail"] = true;
    event["status"]["summary"] = true;
    JsonObject competitor = event["competitions"][0]["competitors"][0].to<JsonObject>();
    competitor["homeAway"] = true;
    competitor["score"] = true;
    competitor["team"]["displayName"] = true;
}

EspnApi::EspnApi(Settings& settings, WiFiManager& wifi, const EspnSport& sport)
    : _settings(settings), _wifi(wifi), _sport(sport),
      _hasData(false), _isLive(false), _attempted(false), _lastAttempt(0) {}

void EspnApi::begin() {
    _hasData = false; _isLive = false; _attempted = false; _lastAttempt = 0; _lastError = "";
}

String EspnApi::cleanSlug(const String& slug) {
    String result;
    for (size_t i = 0; i < slug.length(); ++i) {
        const char c = slug[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') result += c;
    }
    return result;
}

String EspnApi::savedLeague() const {
    if (_sport.cricket) {
        const uint32_t id = _settings.getCricketLeagueId();
        return id ? String(id) : String();
    }
    return cleanSlug(_settings.getFootballLeagueSlug());
}

String EspnApi::savedDate() const {
    return _sport.cricket ? _settings.getCricketDate() : _settings.getFootballDate();
}

String EspnApi::savedEventId() const {
    return _sport.cricket ? _settings.getCricketEventId() : _settings.getFootballEventId();
}

uint32_t EspnApi::intervalMs() const {
    return (uint32_t)(_sport.cricket ? _settings.getCricketPollSeconds()
                                     : _settings.getFootballPollSeconds()) * 1000UL;
}

String EspnApi::scoreboardUrl(const String& league, const String& date) const {
    return String(_sport.base) + league + "/scoreboard" +
           (date.isEmpty() ? String() : String("?dates=") + date);
}

void EspnApi::update() {
    if (!_wifi.isConnected()) return;
    const uint32_t now = millis();
    if (_attempted && now - _lastAttempt < intervalMs()) return;
    _attempted = true;
    _lastAttempt = now;
    fetchBoard(savedLeague(), savedDate(), savedEventId(), nullptr);
}

bool EspnApi::fetchNow() {
    if (!_wifi.isConnected()) {
        _lastError = "Wi-Fi is disconnected";
        LOG_ERROR(_sport.tag, "Fetch skipped: Wi-Fi is disconnected");
        return false;
    }
    _attempted = true;
    _lastAttempt = millis();
    return fetchBoard(savedLeague(), savedDate(), savedEventId(), nullptr);
}

bool EspnApi::httpJson(const String& url, JsonDocument& doc) {
    WiFiClientSecure client;
    client.setInsecure(); // ESPN does not provide a stable certificate pin.
    HTTPClient http;
    http.setTimeout(12000);
    // ArduinoJson reads the client stream directly. Force a non-chunked HTTP/1.0
    // response so the stream begins with JSON, not HTTP chunk-size markers.
    http.useHTTP10(true);
    LOG_INFO(_sport.tag, (String("Fetching ESPN: ") + url).c_str());
    if (!http.begin(client, url)) {
        _lastError = "HTTP begin failed";
        LOG_ERROR(_sport.tag, (String("http.begin failed for: ") + url).c_str());
        return false;
    }
    http.addHeader("User-Agent", "ESP32S3Dashboard/1.0");
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        _lastError = code < 0
            ? String("ESPN transport ") + String(code) + ": " + http.errorToString(code)
            : String("ESPN HTTP ") + String(code);
        LOG_ERROR(_sport.tag, _lastError.c_str());
        http.end();
        return false;
    }

    JsonDocument filter;
    buildFilter(filter);
    // ESPN nests far deeper than ArduinoJson's default limit of 10 (its
    // leaders/athlete/links/rel branch reaches depth ~15). The parser still
    // descends into filtered-out branches, so a too-small limit returns
    // DeserializationError::TooDeep and the whole scoreboard is lost. Raise it
    // well past ESPN's depth while the filter keeps the document tiny.
    const DeserializationError err = deserializeJson(
        doc, *http.getStreamPtr(),
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(32));
    http.end();
    if (err) {
        _lastError = String("ESPN JSON: ") + err.c_str();
        LOG_ERROR(_sport.tag, _lastError.c_str());
        return false;
    }
    return true;
}

bool EspnApi::fetchBoard(const String& league, const String& date, const String& chosenId, String* browseOut) {
    if (league.isEmpty()) {
        _lastError = _sport.cricket ? "Choose an ESPN cricket series" : "Choose an ESPN league";
        return false;
    }
    JsonDocument doc;
    if (!httpJson(scoreboardUrl(league, date), doc)) return false;

    _leagueName = doc["leagues"][0]["name"] | (_sport.cricket ? String() : league);

    // One pass builds the Browse list and picks the board match.
    _events.clear();
    JsonObject selected, live, upcoming, finished;
    for (JsonObject candidate : doc["events"].as<JsonArray>()) {
        const String id = candidate["id"] | "";
        const String state = candidate["status"]["type"]["state"] | "";
        String detail = candidate["status"]["type"]["shortDetail"] | "";
        if (detail.isEmpty()) detail = candidate["status"]["type"]["detail"] | "";
        if (_events.size() < MAX_WEB_EVENTS && !id.isEmpty())
            _events.push_back({id, (candidate["name"] | String("Match")) +
                                   (detail.isEmpty() ? String() : " - " + detail)});
        if (!chosenId.isEmpty() && id == chosenId) selected = candidate;
        if (live.isNull() && state == "in") live = candidate;
        if (upcoming.isNull() && state == "pre") upcoming = candidate;
        if (finished.isNull() && state == "post") finished = candidate;
    }

    // Browse stops here: the caller only wants the match list, and the board
    // selection below belongs to the saved feed, not to this lookup.
    if (browseOut) {
        JsonDocument out;
        out["ok"] = true;
        out["league"] = _leagueName;
        JsonArray list = out["events"].to<JsonArray>();
        for (const EventChoice& choice : _events) {
            JsonObject entry = list.add<JsonObject>();
            entry["id"] = choice.id;
            entry["label"] = choice.label;
        }
        serializeJson(out, *browseOut);
        return true;
    }

    if (!chosenId.isEmpty() && selected.isNull()) {
        _hasData = false;
        _lastError = _sport.cricket ? "Selected match is not on this scoreboard"
                                    : "Selected match is not on ESPN's current scoreboard";
        return false;
    }
    if (selected.isNull()) selected = !live.isNull() ? live : (!upcoming.isNull() ? upcoming : finished);
    if (selected.isNull()) {
        _hasData = false;
        _lastError = _sport.cricket ? "No matches on ESPN's scoreboard"
                                    : "No matches on ESPN's current scoreboard";
        return false;
    }

    // Cricket scores are strings ("187/6"); soccer scores are goal counts, and a
    // match without one shows "-" exactly as the old board did.
    _homeName = ""; _awayName = ""; _homeScore = ""; _awayScore = "";
    for (JsonObject item : selected["competitions"][0]["competitors"].as<JsonArray>()) {
        const String side = item["homeAway"] | "";
        const String team = item["team"]["displayName"] | "";
        const String raw = item["score"] | "";
        const String score = _sport.cricket ? raw : (raw.isEmpty() ? String("-") : raw);
        if (side == "home") { _homeName = team; _homeScore = score; }
        if (side == "away") { _awayName = team; _awayScore = score; }
    }

    const String state = selected["status"]["type"]["state"] | "";
    _isLive = state == "in";
    _statusText = selected["status"]["summary"] | "";
    if (_statusText.isEmpty()) _statusText = selected["status"]["type"]["shortDetail"] | "";
    if (_statusText.isEmpty()) _statusText = selected["status"]["type"]["detail"] | "";
    if (_statusText.isEmpty())
        _statusText = state == "pre" ? "Scheduled" : (state == "post" ? "Final" : "Unavailable");

    _hasData = !_homeName.isEmpty() || !_awayName.isEmpty();
    _lastError = _hasData ? "" : "ESPN returned an incomplete match";
    return _hasData;
}

bool EspnApi::fetchScoreboardFor(const String& league, const String& dateYyyymmdd, String* out) {
    String key;
    if (_sport.cricket) {
        const long id = league.toInt();
        if (id <= 0) { _lastError = "Choose an ESPN cricket series"; return false; }
        key = String(id);
    } else {
        key = cleanSlug(league);
        if (key.isEmpty()) { _lastError = "Choose an ESPN league"; return false; }
    }
    // Browse only borrows the league name and the match list, so put them back
    // afterwards: fetchBoard rewrites both while building the reply.
    const String savedLeagueName = _leagueName;
    const std::vector<EventChoice> savedEvents = _events;
    const bool ok = fetchBoard(key, dateYyyymmdd, String(), out);
    _leagueName = savedLeagueName;
    _events = savedEvents;
    return ok;
}
