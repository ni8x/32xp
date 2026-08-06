#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "Settings.h"

class WiFiManager {
public:
    WiFiManager(Settings& settings);
    ~WiFiManager();

    void begin();
    void update();
    
    void connect();
    void disconnect();
    
    bool isConnected() const;
    String ipAddress() const;
    String getStatusString() const;

private:
    Settings& _settings;
    uint32_t _lastConnectAttempt;
    bool _wasConnected;
    
    static constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
    static constexpr uint32_t RECONNECT_INTERVAL_MS = 20000;
};
