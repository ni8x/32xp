#include "FootballApi.h"
#include "Logger.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static const char* TAG = "FootballApi";
static const char* SCOREBOARD_BASE = "https://site.api.espn.com/apis/site/v2/sports/soccer/";
static constexpr size_t MAX_WEB_EVENTS = 24;

FootballApi::FootballApi(Settings& settings, WiFiManager& wifi)
    : _settings(settings), _wifi(wifi), _hasData(false), _isLive(false),
      _homeGoals(-1), _awayGoals(-1), _lastFetch(0), _lastAttempt(0), _attempted(false) {}

void FootballApi::begin() {
    _hasData = false; _isLive = false; _lastFetch = 0; _lastAttempt = 0;
    _attempted = false; _lastError = "";
}

uint32_t FootballApi::intervalMs() const {
    return (uint32_t)_settings.getFootballPollSeconds() * 1000UL;
}

String FootballApi::cleanSlug(const String& slug) {
    String result;
    for (size_t i = 0; i < slug.length(); ++i) {
        char c = slug[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') result += c;
    }
    return result;
}

void FootballApi::update() {
    if (!_wifi.isConnected()) return;
    uint32_t now = millis();
    if (_attempted && now - _lastAttempt < intervalMs()) return;
    _attempted = true;
    _lastAttempt = now;
    fetchScoreboard();
}

bool FootballApi::fetchNow() {
    if (!_wifi.isConnected()) {
        _lastError = "Wi-Fi is disconnected";
        LOG_ERROR(TAG, "Fetch skipped: Wi-Fi is disconnected");
        return false;
    }
    _attempted = true;
    _lastAttempt = millis();
    return fetchScoreboard();
}

bool FootballApi::fetchScoreboard() {
    const String slug = cleanSlug(_settings.getFootballLeagueSlug());
    if (slug.isEmpty()) { _lastError = "Choose an ESPN league"; return false; }

    WiFiClientSecure client;
    client.setInsecure(); // ESPN does not provide a stable certificate pin.
    HTTPClient http;
    http.setTimeout(12000);
    // ArduinoJson reads the client stream directly. Force a non-chunked HTTP/1.0
    // response so the stream begins with JSON, not HTTP chunk-size markers.
    http.useHTTP10(true);
    String url = String(SCOREBOARD_BASE) + slug + "/scoreboard";
    const String date = _settings.getFootballDate();
    if (!date.isEmpty()) url += "?dates=" + date;
    LOG_INFO(TAG, (String("Fetching ESPN: ") + url).c_str());
    if (!http.begin(client, url)) {
        _lastError = "HTTP begin failed";
        LOG_ERROR(TAG, (String("http.begin failed for: ") + url).c_str());
        return false;
    }
    http.addHeader("User-Agent", "ESP32S3Dashboard/1.0");
    const int code = http.GET();
    _lastFetch = millis();
    if (code != HTTP_CODE_OK) {
        _lastError = code < 0
            ? String("ESPN transport ") + String(code) + ": " + http.errorToString(code)
            : String("ESPN HTTP ") + String(code);
        LOG_ERROR(TAG, _lastError.c_str());
        http.end();
        return false;
    }
    bool ok = parseScoreboard(*http.getStreamPtr());
    http.end();
    return ok;
}

bool FootballApi::parseScoreboard(Stream& stream) {
    // Skip ESPN's logos, standings and broadcasts while parsing its stream.
    JsonDocument filter;
    filter["leagues"][0]["name"] = true;
    JsonObject event = filter["events"][0].to<JsonObject>();
    event["id"] = true;
    event["name"] = true;
    event["status"]["type"]["state"] = true;
    event["status"]["type"]["shortDetail"] = true;
    event["status"]["type"]["detail"] = true;
    JsonObject competition = event["competitions"][0].to<JsonObject>();
    JsonObject competitor = competition["competitors"][0].to<JsonObject>();
    competitor["homeAway"] = true;
    competitor["score"] = true;
    competitor["team"]["displayName"] = true;

    // ESPN nests far deeper than ArduinoJson's default limit of 10 (its
    // leaders/athlete/links/rel branch reaches depth ~15). The parser still
    // descends into filtered-out branches, so a too-small limit returns
    // DeserializationError::TooDeep and the whole scoreboard is lost. Raise it
    // well past ESPN's depth while the filter keeps the document tiny.
    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc, stream,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(32));
    if (err) {
        _lastError = String("ESPN JSON: ") + err.c_str();
        LOG_ERROR(TAG, _lastError.c_str());
        return false;
    }

    _leagueName = doc["leagues"][0]["name"] | _settings.getFootballLeagueSlug();
    _events.clear();
    JsonArray events = doc["events"].as<JsonArray>();
    JsonObject selected, live, upcoming, finished;
    const String chosenId = _settings.getFootballEventId();
    for (JsonObject candidate : events) {
        String id = candidate["id"] | "";
        String name = candidate["name"] | "Match";
        String state = candidate["status"]["type"]["state"] | "";
        String detail = candidate["status"]["type"]["shortDetail"] | "";
        if (detail.isEmpty()) detail = candidate["status"]["type"]["detail"] | "";
        if (_events.size() < MAX_WEB_EVENTS && !id.isEmpty()) _events.push_back({id, name + (detail.isEmpty() ? "" : " - " + detail)});
        if (!chosenId.isEmpty() && id == chosenId) selected = candidate;
        if (live.isNull() && state == "in") live = candidate;
        if (upcoming.isNull() && state == "pre") upcoming = candidate;
        if (finished.isNull() && state == "post") finished = candidate;
    }
    if (!chosenId.isEmpty() && selected.isNull()) {
        _hasData = false; _lastError = "Selected match is not on ESPN's current scoreboard"; return false;
    }
    if (selected.isNull()) selected = !live.isNull() ? live : (!upcoming.isNull() ? upcoming : finished);
    if (selected.isNull()) { _hasData = false; _lastError = "No matches on ESPN's current scoreboard"; return false; }

    _homeName = ""; _awayName = ""; _homeGoals = -1; _awayGoals = -1;
    JsonArray competitors = selected["competitions"][0]["competitors"].as<JsonArray>();
    for (JsonObject item : competitors) {
        String side = item["homeAway"] | "";
        String team = item["team"]["displayName"] | "";
        int score = item["score"].isNull() ? -1 : String(item["score"] | "").toInt();
        if (side == "home") { _homeName = team; _homeGoals = score; }
        if (side == "away") { _awayName = team; _awayGoals = score; }
    }
    String state = selected["status"]["type"]["state"] | "";
    _isLive = state == "in";
    _statusText = selected["status"]["type"]["shortDetail"] | "";
    if (_statusText.isEmpty()) _statusText = selected["status"]["type"]["detail"] | "";
    if (_statusText.isEmpty()) _statusText = state == "pre" ? "Scheduled" : (state == "post" ? "Final" : "Unavailable");
    _hasData = !_homeName.isEmpty() || !_awayName.isEmpty();
    _lastError = _hasData ? "" : "ESPN returned an incomplete match";
    return _hasData;
}
