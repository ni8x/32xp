#include "CricketApi.h"
#include "Logger.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static const char* TAG = "CricketApi";
static const char* BASE = "https://site.api.espn.com/apis/site/v2/sports/cricket/";

CricketApi::CricketApi(Settings& settings, WiFiManager& wifi)
    : _settings(settings), _wifi(wifi), _hasData(false), _isLive(false), _attempted(false), _lastAttempt(0) {}
void CricketApi::begin() { _hasData = false; _isLive = false; _attempted = false; _lastError = ""; }
uint32_t CricketApi::intervalMs() const { return (uint32_t)_settings.getCricketPollSeconds() * 1000UL; }
void CricketApi::update() {
    if (!_wifi.isConnected()) return;
    if (_attempted && millis() - _lastAttempt < intervalMs()) return;
    _attempted = true; _lastAttempt = millis(); fetchScoreboard();
}
bool CricketApi::fetchNow() {
    if (!_wifi.isConnected()) { _lastError = "Wi-Fi is disconnected"; return false; }
    _attempted = true; _lastAttempt = millis(); return fetchScoreboard();
}
bool CricketApi::fetchScoreboard() {
    const uint16_t league = _settings.getCricketLeagueId();
    if (!league) { _lastError = "Choose an ESPN cricket series"; return false; }
    String url = String(BASE) + league + "/scoreboard";
    String date = _settings.getCricketDate(); if (!date.isEmpty()) url += "?dates=" + date;
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http; http.setTimeout(12000); http.useHTTP10(true);
    LOG_INFO(TAG, (String("Fetching ESPN: ") + url).c_str());
    if (!http.begin(client, url)) { _lastError = "HTTP begin failed"; LOG_ERROR(TAG, _lastError.c_str()); return false; }
    http.addHeader("User-Agent", "ESP32S3Dashboard/1.0");
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        _lastError = code < 0 ? String("ESPN transport ") + code + ": " + http.errorToString(code) : String("ESPN HTTP ") + code;
        LOG_ERROR(TAG, _lastError.c_str()); http.end(); return false;
    }
    bool ok = parseScoreboard(*http.getStreamPtr()); http.end(); return ok;
}
bool CricketApi::parseScoreboard(Stream& stream) {
    JsonDocument filter;
    JsonObject event = filter["events"][0].to<JsonObject>();
    event["id"] = true; event["name"] = true;
    event["status"]["type"]["state"] = true; event["status"]["type"]["shortDetail"] = true; event["status"]["summary"] = true;
    JsonObject competitor = event["competitions"][0]["competitors"][0].to<JsonObject>();
    competitor["homeAway"] = true; competitor["score"] = true; competitor["team"]["displayName"] = true;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, stream, DeserializationOption::Filter(filter));
    if (err) { _lastError = String("ESPN JSON: ") + err.c_str(); LOG_ERROR(TAG, _lastError.c_str()); return false; }
    _events.clear(); JsonArray events = doc["events"].as<JsonArray>();
    JsonObject selected, live, upcoming, finished; String chosen = _settings.getCricketEventId();
    for (JsonObject item : events) {
        String id = item["id"] | ""; String name = item["name"] | "Match";
        String state = item["status"]["type"]["state"] | ""; String detail = item["status"]["type"]["shortDetail"] | "";
        if (_events.size() < 24 && !id.isEmpty()) _events.push_back({id, name + (detail.isEmpty() ? "" : " - " + detail)});
        if (!chosen.isEmpty() && id == chosen) selected = item;
        if (live.isNull() && state == "in") live = item;
        if (upcoming.isNull() && state == "pre") upcoming = item;
        if (finished.isNull() && state == "post") finished = item;
    }
    if (!chosen.isEmpty() && selected.isNull()) { _hasData = false; _lastError = "Selected match is not on this scoreboard"; return false; }
    if (selected.isNull()) selected = !live.isNull() ? live : (!upcoming.isNull() ? upcoming : finished);
    if (selected.isNull()) { _hasData = false; _lastError = "No matches on ESPN's scoreboard"; return false; }
    _homeName=""; _awayName=""; _homeScore=""; _awayScore="";
    for (JsonObject item : selected["competitions"][0]["competitors"].as<JsonArray>()) {
        String side=item["homeAway"]|""; String name=item["team"]["displayName"]|""; String score=item["score"]|"";
        if (side=="home") {_homeName=name; _homeScore=score;} if(side=="away") {_awayName=name; _awayScore=score;}
    }
    String state=selected["status"]["type"]["state"]|""; _isLive=state=="in";
    _statusText=selected["status"]["summary"]|""; if(_statusText.isEmpty()) _statusText=selected["status"]["type"]["shortDetail"]|"";
    _hasData=!_homeName.isEmpty() || !_awayName.isEmpty(); _lastError=_hasData ? "" : "ESPN returned an incomplete match"; return _hasData;
}
