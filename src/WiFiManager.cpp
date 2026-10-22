#include "WiFiManager.h"
#include "Logger.h"

static const char* TAG = "WiFi";

WiFiManager::WiFiManager(Settings& settings) 
    : _settings(settings), _lastConnectAttempt(0), _wasConnected(false),
      _lastFlagWarnAt(0), _lastAddress(IPAddress(0, 0, 0, 0)),
      _pinConsidered(false), _pinOffNetwork(false), _staticApplied(false) {
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
    bool currentlyConnected = isConnected();

    // The driver's event flag is not the whole truth (see isConnected()). When
    // the station is up on a real address but the flag never latched, say so
    // once a minute instead of leaving the disagreement to be guessed at from
    // the dashboard. The reconnect code below is deliberately driven by
    // "no address" rather than by the flag: re-configuring or re-begin()ing a
    // link that is already carrying HTTP churns the connection without ever
    // making the flag latch.
    if (currentlyConnected && WiFi.status() != WL_CONNECTED) {
        uint32_t now = millis();
        if (now - _lastFlagWarnAt >= 60000) {
            _lastFlagWarnAt = now;
            LOG_WARN(TAG, (String("Station is up on ") + WiFi.localIP().toString() +
                           " but the Wi-Fi driver reports '" + getRawStatusString() +
                           "'; treating it as connected").c_str());
        }
    }
    
    if (currentlyConnected && !_wasConnected) {
        _wasConnected = true;
        logAddress("IP address:");
        learnLease();
        // The link is up on a real lease, so the network is known: only now can
        // a pinned address be checked against it (see considerPin()).
        considerPin();
    } else if (!currentlyConnected && _wasConnected) {
        LOG_WARN(TAG, "Wi-Fi disconnected");
        _wasConnected = false;
        // Back to DHCP, so the next association re-learns whatever network this
        // boot ends up on instead of reusing an address from the old one.
        releasePin();
    } else if (currentlyConnected && WiFi.localIP() != _lastAddress) {
        // Same connection, new lease (DHCP renewal, roaming to another AP): the
        // address in the log must not go stale. IPAddress comparison, so this
        // costs no allocation on the loop path.
        logAddress("New IP address:");
    }
    
    // Auto reconnect logic (only when there is no address to serve on)
    if (!hasAddress()) {
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
    _staticApplied = false;
    _pinConsidered = false;
    _pinOffNetwork = false;
    // DHCP first, always: the lease is how the device finds out which network it
    // is actually on, which is what tells a pinned address whether it applies.
    WiFi.begin(ssid.c_str(), password.c_str());
}

// ── Fixed addressing ────────────────────────────────────────────────────────

bool WiFiManager::parseIp(const String& text, IPAddress& out) {
    return text.length() > 0 && out.fromString(text);
}

bool WiFiManager::usableUnicast(const IPAddress& ip) {
    const uint8_t a = ip[0];
    if (a == 0 || a == 127 || a >= 224) return false;   // "this net", loopback, multicast + reserved
    if (ip[0] == 255 && ip[1] == 255 && ip[2] == 255 && ip[3] == 255) return false;
    return true;
}

bool WiFiManager::sameSubnet(const IPAddress& a, const IPAddress& b, const IPAddress& mask) {
    for (uint8_t i = 0; i < 4; i++) {
        if ((uint8_t)(a[i] & mask[i]) != (uint8_t)(b[i] & mask[i])) return false;
    }
    return true;
}

void WiFiManager::considerPin() {
    if (_pinConsidered || _staticApplied) return;

    const String pinned = _settings.getStaticIp();
    if (pinned.isEmpty()) {
        _pinConsidered = true;     // plain DHCP, nothing to weigh
        return;
    }

    const IPAddress gateway = WiFi.gatewayIP();
    const IPAddress mask = WiFi.subnetMask();
    if (!usableUnicast(gateway) || mask == IPAddress(0, 0, 0, 0)) {
        return;    // lease not readable yet; the next tick tries again
    }

    IPAddress pin;
    if (!parseIp(pinned, pin)) {
        _pinConsidered = true;
        _pinOffNetwork = true;
        LOG_WARN(TAG, (String("Pinned address '") + pinned +
                       "' is not a usable IPv4 address; staying on DHCP").c_str());
        return;
    }
    if (WiFi.localIP() == pin) {
        _pinConsidered = true;     // already on it (e.g. it was handed out by DHCP)
        _staticApplied = true;
        return;
    }

    // The decisive test. A pin from another network associates fine and then
    // routes nowhere, which is unreachable rather than merely wrong - so the
    // pin is only applied when it belongs to the subnet the lease reported.
    if (!sameSubnet(pin, gateway, mask)) {
        _pinConsidered = true;
        _pinOffNetwork = true;
        LOG_WARN(TAG, (String("Pinned address ") + pinned + " is not on this network (" +
                       gateway.toString() + " / " + mask.toString() + "); staying on DHCP at " +
                       WiFi.localIP().toString()).c_str());
        return;
    }

    IPAddress dns = WiFi.dnsIP();
    if (!usableUnicast(dns)) dns = gateway;   // the router resolves unless the lease said otherwise
    const String leased = WiFi.localIP().toString();
    if (!WiFi.config(pin, gateway, mask, dns)) {
        _pinConsidered = true;
        _pinOffNetwork = true;
        LOG_WARN(TAG, (String("Stack rejected the pinned address ") + pinned +
                       "; staying on DHCP at " + leased).c_str());
        return;
    }
    _staticApplied = true;
    _pinConsidered = true;
    LOG_INFO(TAG, (String("Using pinned address ") + pinned + " (DHCP had " + leased +
                   ") via " + gateway.toString()).c_str());
}

void WiFiManager::releasePin() {
    _pinConsidered = false;
    _pinOffNetwork = false;
    if (!_staticApplied) return;
    _staticApplied = false;
    // config() with an empty address is the documented way back to DHCP on this
    // core: it stops the stack's fixed addressing and restarts the DHCP client.
    WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
    LOG_INFO(TAG, "Pinned address released; back on DHCP");
}

String WiFiManager::staticIpState() const {
    if (_staticApplied) return "active";
    if (_pinOffNetwork) return "off-network";
    return "dhcp";
}

void WiFiManager::learnLease() {
    // The lease is the only description of the network the device has, and a
    // pinned address is judged against it in considerPin(). The Settings setters
    // ignore unchanged values, so this does not churn NVS on every reconnect.
    _settings.setNetGateway(WiFi.gatewayIP().toString());
    _settings.setNetSubnet(WiFi.subnetMask().toString());
    _settings.setNetDns(WiFi.dnsIP().toString());
}

bool WiFiManager::setStaticIp(const String& ip, String& error) {
    String value = ip;
    value.trim();
    if (value.isEmpty()) {
        _settings.setStaticIp("");
        LOG_INFO(TAG, "Fixed IP cleared; DHCP again after the restart");
        return true;
    }

    IPAddress parsed;
    if (!parseIp(value, parsed)) {
        error = "Not an IPv4 address - use four numbers like 192.168.1.50";
        return false;
    }
    if (!usableUnicast(parsed)) {
        error = "That address cannot be used on a LAN (0.0.0.0, loopback, multicast or broadcast)";
        return false;
    }

    IPAddress gateway, mask;
    if (parseIp(_settings.getNetGateway(), gateway) && parseIp(_settings.getNetSubnet(), mask)) {
        if (!sameSubnet(parsed, gateway, mask)) {
            error = "Address is outside this network (" + _settings.getNetGateway() + " / " +
                    _settings.getNetSubnet() + ")";
            return false;
        }
        if (parsed == gateway) {
            error = "That is the gateway's own address";
            return false;
        }
        bool hostBitsZero = true, hostBitsAll = true;
        for (uint8_t i = 0; i < 4; i++) {
            const uint8_t host = (uint8_t)(parsed[i] & ~mask[i]);
            const uint8_t width = (uint8_t)(~mask[i] & 0xFF);
            if (host != 0) hostBitsZero = false;
            if (host != width) hostBitsAll = false;
        }
        if (hostBitsZero) {
            error = "That is the network address, not a host address";
            return false;
        }
        if (hostBitsAll) {
            error = "That is the broadcast address, not a host address";
            return false;
        }
    }

    _settings.setStaticIp(value);
    LOG_INFO(TAG, (String("Fixed IP stored: ") + value).c_str());
    return true;
}

String WiFiManager::configuredStaticIp() {
    return _settings.getStaticIp();
}

String WiFiManager::networkGateway() {
    return _settings.getNetGateway();
}

String WiFiManager::networkSubnet() {
    return _settings.getNetSubnet();
}

String WiFiManager::networkDns() {
    return _settings.getNetDns();
}

void WiFiManager::disconnect() {
    WiFi.disconnect(true);
    _wasConnected = false;
    LOG_INFO(TAG, "Wi-Fi disconnected manually");
}

bool WiFiManager::isConnected() const {
    // The driver's status flag alone is not trustworthy: on this core WL_CONNECTED
    // is set only by the GOT_IP event, which a station carrying a pinned address
    // does not always get (the DHCP client is stopped, so nothing asks for a
    // lease). The board then associates, owns an address, serves HTTP - and
    // answers /api/status with "Disconnected" and 0.0.0.0.
    if (WiFi.status() == WL_CONNECTED) return true;

    // The address alone is not enough either: a pinned address stays configured
    // through a link drop. RSSI() asks the driver which AP the station is on right
    // now and returns 0 when there is none, so together they cover both lies - the
    // stale flag and the stale address.
    return hasAddress() && WiFi.RSSI() != 0;
}

bool WiFiManager::hasAddress() const {
    return WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

String WiFiManager::ipAddress() const {
    // Report the address whenever the interface has one, even while the driver's
    // flag is still catching up: 0.0.0.0 here made the dashboard hide the URL of
    // a device the user was looking at.
    if (!hasAddress()) return "0.0.0.0";
    return WiFi.localIP().toString();
}

int WiFiManager::getRssi() const {
    return hasAddress() ? WiFi.RSSI() : 0;
}

void WiFiManager::logAddress(const char* why) {
    _lastAddress = WiFi.localIP();
    const String ip = _lastAddress.toString();
    const String line = String(why) + " " + ip + " - open http://" + ip + "/";
    LOG_INFO(TAG, line.c_str());
}

String WiFiManager::getStatusString() const {
    // What a human should read: the link is up if the station has an address.
    if (isConnected()) return "Connected";
    return getRawStatusString();
}

String WiFiManager::getRawStatusString() const {
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
