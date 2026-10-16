#include "WiFiManager.h"
#include "Logger.h"

static const char* TAG = "WiFi";

WiFiManager::WiFiManager(Settings& settings) 
    : _settings(settings), _lastConnectAttempt(0), _wasConnected(false),
      _lastAddress(IPAddress(0, 0, 0, 0)) {
}

WiFiManager::~WiFiManager() {
    disconnect();
}

void WiFiManager::begin() {
    LOG_INFO(TAG, "Initializing WiFiManager...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);
    
    connect();
}

void WiFiManager::update() {
    bool currentlyConnected = (WiFi.status() == WL_CONNECTED);
    
    if (currentlyConnected && !_wasConnected) {
        LOG_INFO(TAG, "Wi-Fi connected");
        _wasConnected = true;
        logAddress("IP address:");
    } else if (!currentlyConnected && _wasConnected) {
        LOG_WARN(TAG, "Wi-Fi disconnected");
        _wasConnected = false;
    } else if (currentlyConnected && WiFi.localIP() != _lastAddress) {
        // Same connection, new lease (DHCP renewal, roaming to another AP): the
        // address in the log must not go stale. IPAddress comparison, so this
        // costs no allocation on the loop path.
        logAddress("New IP address:");
    }
    
    // Auto reconnect logic
    if (!currentlyConnected) {
        uint32_t now = millis();
        if (now - _lastConnectAttempt >= RECONNECT_INTERVAL_MS) {
            connect();
        }
    }
}

void WiFiManager::connect() {
    String ssid = _settings.getWifiSSID();
    String password = _settings.getWifiPassword();

    if (ssid.isEmpty()) {
        LOG_WARN(TAG, "No Wi-Fi credentials saved. Cannot connect.");
        return;
    }

    LOG_INFO(TAG, "Attempting to connect to Wi-Fi...");
    _lastConnectAttempt = millis();
    WiFi.begin(ssid.c_str(), password.c_str());
}

void WiFiManager::disconnect() {
    WiFi.disconnect(true);
    _wasConnected = false;
    LOG_INFO(TAG, "Wi-Fi disconnected manually");
}

bool WiFiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

String WiFiManager::ipAddress() const {
    if (isConnected()) {
        return WiFi.localIP().toString();
    }
    return "0.0.0.0";
}

void WiFiManager::logAddress(const char* why) {
    _lastAddress = WiFi.localIP();
    const String ip = _lastAddress.toString();
    const String line = String(why) + " " + ip + " - open http://" + ip + "/";
    LOG_INFO(TAG, line.c_str());
}

String WiFiManager::getStatusString() const {
    switch (WiFi.status()) {
        case WL_IDLE_STATUS: return "Idle";
        case WL_NO_SSID_AVAIL: return "Network Not Found";
        case WL_SCAN_COMPLETED: return "Scan Completed";
        case WL_CONNECTED: return "Connected";
        case WL_CONNECT_FAILED: return "Wrong Password / Failed";
        case WL_CONNECTION_LOST: return "Connection Lost";
        case WL_DISCONNECTED: return "Disconnected";
        default: return "Unknown";
    }
}
