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
    // Logs the address the dashboard is reachable at and remembers it. The IP is
    // otherwise invisible in the serial log (it is only drawn on the panel), and
    // a DHCP lease can change it on any boot.
    void logAddress(const char* why);

    Settings& _settings;
    uint32_t _lastConnectAttempt;
    bool _wasConnected;
    // Last reported address, so a lease change on a live connection is noticed
    // without building a String on every loop tick.
    IPAddress _lastAddress;
    
    static constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
    static constexpr uint32_t RECONNECT_INTERVAL_MS = 20000;
};
