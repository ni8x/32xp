#pragma once

#include <Arduino.h>
#include <time.h>

class TimeManager {
public:
    TimeManager();
    ~TimeManager();

    void begin();
    void update();

    bool isTimeSynced() const;
    String getFormattedTime() const;
    String getFormattedDate() const;
    // Compact forms for the portrait clock: "HH:MM" and "01 Jan 2024". The
    // full-width forms ("HH:MM:SS", "Mon, Jan 01 2024") overflow the 240px
    // portrait width at the sizes that layout needs, so the portrait screen
    // uses these instead.
    String getFormattedTimeShort() const;
    String getFormattedDateShort() const;

private:
    bool _isSynced;
    uint32_t _lastSyncAttempt;
    
    // Hardcoded GMT+5:30 for India Standard Time
    const long _gmtOffset_sec = 19800; 
    const int _daylightOffset_sec = 0; 
    const char* _ntpServer = "pool.ntp.org";

    void checkSyncStatus();
};
