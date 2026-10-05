#pragma once

#include <Arduino.h>
#include <vector>
#include "Settings.h"
#include "WiFiManager.h"

// Direct client for ESPN's unofficial public soccer scoreboard. It needs no
// account or key. One request returns the selected league's scoreboard and
// supplies the match list displayed by the device web page.
class FootballApi {
public:
    struct EventChoice { String id; String label; };

    FootballApi(Settings& settings, WiFiManager& wifi);
    void begin();
    void update();
    bool fetchNow();

    bool hasData() const { return _hasData; }
    bool isLive() const { return _isLive; }
    const String& leagueName() const { return _leagueName; }
    const String& homeName() const { return _homeName; }
    const String& awayName() const { return _awayName; }
    const String& statusText() const { return _statusText; }
    int homeGoals() const { return _homeGoals; }
    int awayGoals() const { return _awayGoals; }
    const std::vector<EventChoice>& events() const { return _events; }
    const String& lastError() const { return _lastError; }
    uint32_t lastFetchMs() const { return _lastFetch; }

private:
    bool fetchScoreboard();
    bool parseScoreboard(Stream& stream);
    uint32_t intervalMs() const;
    static String cleanSlug(const String& slug);

    Settings& _settings;
    WiFiManager& _wifi;
    bool _hasData;
    bool _isLive;
    String _leagueName, _homeName, _awayName, _statusText;
    int _homeGoals, _awayGoals;
    std::vector<EventChoice> _events;
    String _lastError;
    uint32_t _lastFetch, _lastAttempt;
    bool _attempted;
};
