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
    
    // True when the station is on a network: the driver's own status OR the
    // interface owning an address. The two can disagree (see the comment on the
    // definition) and the address is the one that cannot lie while the board is
    // serving HTTP.
    bool isConnected() const;
    bool hasAddress() const;
    String ipAddress() const;
    // True while the fallback setup hotspot is running (no saved Wi-Fi yet).
    bool isSetupAp() const;
    // "Connected" when the station is usable, else the driver's state word.
    String getStatusString() const;
    // The driver's state word, even when it disagrees with isConnected().
    String getRawStatusString() const;
    int getRssi() const;

    // ── Fixed addressing ────────────────────────────────────────────────────
    // The dashboard URL should not move between boots, so the address can be
    // pinned. setStaticIp() validates a value and stores it ("" restores DHCP);
    // it returns false with a reason when the value would strand the device.
    // The caller restarts the board, so addressing is always applied from a
    // clean netif state instead of being re-configured on a live interface.
    bool setStaticIp(const String& ip, String& error);
    String configuredStaticIp();   // stored value, "" = DHCP
    String networkGateway();
    String networkSubnet();
    String networkDns();
    // "active" (in use), "off-network" (stored but not applicable here) or
    // "dhcp" (nothing stored). The dashboard shows it next to the address, so a
    // pin that was skipped is visible instead of looking ignored.
    String staticIpState() const;

private:
    // Starts the open setup hotspot used on a board with no saved Wi-Fi.
    void startSetupAp();

    // Logs the address the dashboard is reachable at and remembers it. The IP is
    // otherwise invisible in the serial log (it is only drawn on the panel), and
    // a DHCP lease can change it on any boot.
    void logAddress(const char* why);

    // Applies the stored pin, but only after a lease has shown which network the
    // device is on and only when the pin belongs to it. A pin from another
    // network is kept but never applied: that case associates and then routes
    // nowhere, which is unreachable rather than merely misconfigured.
    void considerPin();
    // Drops back to DHCP (used when the link goes down, so the next association
    // re-learns the network instead of reusing an address from the old one).
    void releasePin();
    // Remembers the current DHCP lease (gateway/mask/DNS): the network a pin is
    // judged against, and the defaults the dashboard shows.
    void learnLease();
    static bool parseIp(const String& text, IPAddress& out);
    static bool usableUnicast(const IPAddress& ip);
    static bool sameSubnet(const IPAddress& a, const IPAddress& b, const IPAddress& mask);

    Settings& _settings;
    uint32_t _lastConnectAttempt;
    bool _wasConnected;
    // True while the fallback setup hotspot is active (no saved Wi-Fi yet).
    bool _apMode;
    // Rate limit for the "address but the driver says otherwise" log line: the
    // condition persists for a whole boot, so it must not spam the serial log.
    uint32_t _lastFlagWarnAt;
    // Last reported address, so a lease change on a live connection is noticed
    // without building a String on every loop tick.
    IPAddress _lastAddress;
    // Pinned-address state for the current connection.
    bool _pinConsidered;    // judged against this connection's lease (don't re-judge each tick)
    bool _pinOffNetwork;    // stored, but not on the network we are on
    bool _staticApplied;    // the interface is using the pinned address right now

    static constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
    static constexpr uint32_t RECONNECT_INTERVAL_MS = 20000;
};
