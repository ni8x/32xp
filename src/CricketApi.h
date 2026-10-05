#pragma once

#include <Arduino.h>
#include <vector>
#include "Settings.h"
#include "WiFiManager.h"

// ESPN's public cricket scoreboard. The league is a numeric series ID; 8048
// is the Indian Premier League.
class CricketApi {
public:
    struct EventChoice { String id; String label; };
    CricketApi(Settings& settings, WiFiManager& wifi);
    void begin(); void update(); bool fetchNow();
    bool hasData() const { return _hasData; }
    bool isLive() const { return _isLive; }
    const String& homeName() const { return _homeName; }
    const String& awayName() const { return _awayName; }
    const String& homeScore() const { return _homeScore; }
    const String& awayScore() const { return _awayScore; }
    const String& statusText() const { return _statusText; }
    const std::vector<EventChoice>& events() const { return _events; }
    const String& lastError() const { return _lastError; }
private:
    bool fetchScoreboard(); bool parseScoreboard(Stream& stream);
    uint32_t intervalMs() const;
    Settings& _settings; WiFiManager& _wifi;
    bool _hasData, _isLive, _attempted;
    String _homeName, _awayName, _homeScore, _awayScore, _statusText, _lastError;
    std::vector<EventChoice> _events;
    uint32_t _lastAttempt;
};
