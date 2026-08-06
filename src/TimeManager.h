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

private:
    bool _isSynced;
    uint32_t _lastSyncAttempt;
    
    // Hardcoded GMT+5:30 for India Standard Time
    const long _gmtOffset_sec = 19800; 
    const int _daylightOffset_sec = 0; 
    const char* _ntpServer = "pool.ntp.org";

    void checkSyncStatus();
};
