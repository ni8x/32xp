#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "Settings.h"
#include "WiFiManager.h"

// ESPN's public, key-less scoreboard API. Soccer ("football") and cricket
// return the same "site.api" scoreboard shape, so one client serves both: a
// sport descriptor supplies the URL base and the Settings keys to read from.
// This replaces two near-identical clients (FootballApi and CricketApi) that
// each carried a private copy of the whole HTTPS/TLS/JSON path.
struct EspnSport {
    const char* tag;   // log tag
    const char* base;  // ESPN base up to the league folder
    bool cricket;      // cricket series id + string scores; else soccer slug + goals
};

extern const EspnSport ESPN_FOOTBALL;
extern const EspnSport ESPN_CRICKET;

class EspnApi {
public:
    struct EventChoice { String id; String label; };
    static constexpr size_t MAX_WEB_EVENTS = 24;

    EspnApi(Settings& settings, WiFiManager& wifi, const EspnSport& sport);

    void begin();      // drop the board and the poll timing
    void update();     // poll on the sport's interval
    bool fetchNow();   // poll immediately (web Save / Refresh)

    // One-off scoreboard lookup for the dashboard's Browse buttons. Nothing is
    // persisted and the live board is left alone; out is filled with
    // {"ok":true,"events":[{"id":..,"label":..},...]}.
    bool fetchScoreboardFor(const String& league, const String& dateYyyymmdd, String* out);

    // Whitelist a soccer league slug so it is safe to splice into the ESPN URL.
    static String cleanSlug(const String& slug);

    bool hasData() const { return _hasData; }
    bool isLive() const { return _isLive; }
    const String& homeName() const { return _homeName; }
    const String& awayName() const { return _awayName; }
    const String& homeScore() const { return _homeScore; }
    const String& awayScore() const { return _awayScore; }
    const String& statusText() const { return _statusText; }
    const String& leagueName() const { return _leagueName; }
    const std::vector<EventChoice>& events() const { return _events; }
    const String& lastError() const { return _lastError; }

private:
    String savedLeague() const;  // league slug / cricket series id from Settings
    String savedDate() const;
    String savedEventId() const;
    uint32_t intervalMs() const;
    String scoreboardUrl(const String& league, const String& date) const;
    // The whole ESPN pipeline. browseOut != nullptr selects Browse mode, which
    // fills it with the match list and returns before touching the board.
    bool fetchBoard(const String& league, const String& date, const String& chosenId, String* browseOut);
    bool httpJson(const String& url, JsonDocument& doc);

    Settings& _settings;
    WiFiManager& _wifi;
    const EspnSport& _sport;
    bool _hasData, _isLive, _attempted;
    String _homeName, _awayName, _homeScore, _awayScore, _leagueName, _statusText, _lastError;
    std::vector<EventChoice> _events;
    uint32_t _lastAttempt;
};
