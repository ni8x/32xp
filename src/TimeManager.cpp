#include "TimeManager.h"
#include "Logger.h"

static const char* TAG = "TimeManager";

TimeManager::TimeManager() : _isSynced(false), _lastSyncAttempt(0) {
}

TimeManager::~TimeManager() {
}

void TimeManager::begin() {
    LOG_INFO(TAG, "Initializing SNTP...");
    configTime(_gmtOffset_sec, _daylightOffset_sec, _ntpServer);
    _isSynced = false;
}

void TimeManager::update() {
    if (!_isSynced) {
        uint32_t now = millis();
        // Check every 5 seconds if not synced yet
        if (now - _lastSyncAttempt > 5000) {
            _lastSyncAttempt = now;
            checkSyncStatus();
        }
    }
}

void TimeManager::checkSyncStatus() {
    struct tm timeinfo;
    // getLocalTime has a timeout, so we give it 100ms
    if (getLocalTime(&timeinfo, 100)) {
        if (timeinfo.tm_year > (2020 - 1900)) { // Ensure year is valid (past 2020)
            _isSynced = true;
            LOG_INFO(TAG, "Time successfully synchronized via NTP");
        }
    }
}

bool TimeManager::isTimeSynced() const {
    return _isSynced;
}

String TimeManager::getFormattedTime() const {
    if (!_isSynced) return "--:--:--";
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "--:--:--";
    }
    
    char timeStringBuff[16]; // e.g. "23:59:59"
    strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S", &timeinfo);
    return String(timeStringBuff);
}

String TimeManager::getFormattedDate() const {
    if (!_isSynced) return "----/--/--";
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "----/--/--";
    }
    
    char dateStringBuff[32]; // e.g. "Mon, Jan 01 2024"
    strftime(dateStringBuff, sizeof(dateStringBuff), "%a, %b %d %Y", &timeinfo);
    return String(dateStringBuff);
}
