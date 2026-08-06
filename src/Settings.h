#pragma once

#include <Arduino.h>
#include <Preferences.h>

class Settings {
public:
    Settings();
    ~Settings();

    void init();
    
    uint8_t getBrightness();
    void setBrightness(uint8_t brightness);

    String getWifiSSID();
    void setWifiSSID(const String& ssid);

    String getWifiPassword();
    void setWifiPassword(const String& password);

    String getClockText();
    void setClockText(const String& text);

    String getLatitude();
    void setLatitude(const String& lat);

    String getLongitude();
    void setLongitude(const String& lon);

    uint8_t getDisplayMode();
    void setDisplayMode(uint8_t mode);

    uint32_t getSlideshowInterval();
    void setSlideshowInterval(uint32_t seconds);

    String getHeartColor();
    void setHeartColor(const String& hex);

    // Default values
    static constexpr const char* DEFAULT_LATITUDE = "28.61";
    static constexpr const char* DEFAULT_LONGITUDE = "77.20";
    static constexpr const char* DEFAULT_HEART_COLOR = "#00d2ff";

    void clearAll();
    
private:
    Preferences _preferences;
    uint8_t _brightness;
    String _wifiSSID;
    String _wifiPassword;
    String _clockText;
    String _latitude;
    String _longitude;
    String _heartColor;
    uint8_t _displayMode;
    uint32_t _slideshowInterval;
    bool _initialized;
};
