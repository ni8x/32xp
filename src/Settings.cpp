#include "Settings.h"
#include "Config.h"
#include "Logger.h"

static const char* TAG = "Settings";

Settings::Settings()
    : _brightness(Config::DEFAULT_BRIGHTNESS),
      _wifiSSID(),
      _wifiPassword(),
      _clockText(),
      _latitude(DEFAULT_LATITUDE),
      _longitude(DEFAULT_LONGITUDE),
      _heartColor(DEFAULT_HEART_COLOR),
      _displayMode(0),
      _rotation(Config::DEFAULT_ROTATION),
      _slideshowInterval(5),
      _initialized(false) {
}

Settings::~Settings() {
    if (_initialized) {
        _preferences.end();
    }
}

void Settings::init() {
    LOG_INFO(TAG, "Initializing Settings...");
    
    _initialized = _preferences.begin("dashboard", false);
    if (!_initialized) {
        LOG_ERROR(TAG, "Failed to initialize Preferences");
        return;
    }

    _brightness = _preferences.getUInt("brightness", Config::DEFAULT_BRIGHTNESS);
    _wifiSSID = _preferences.getString("ssid", "");
    _wifiPassword = _preferences.getString("password", "");
    _clockText = _preferences.getString("clockText", "");
    _latitude = _preferences.getString("lat", DEFAULT_LATITUDE);
    _longitude = _preferences.getString("lon", DEFAULT_LONGITUDE);
    _heartColor = _preferences.getString("heartColor", DEFAULT_HEART_COLOR);
    _displayMode = _preferences.getUChar("mode", 0);
    _rotation = _preferences.getUChar("rotation", Config::DEFAULT_ROTATION);
    if (_rotation > 3) _rotation = 3;
    _slideshowInterval = _preferences.getUInt("ss_interval", 5);
    
    LOG_INFO(TAG, "Settings initialized");
}

uint8_t Settings::getBrightness() {
    return _brightness;
}

void Settings::setBrightness(uint8_t brightness) {
    _brightness = brightness;
    if (_initialized) {
        _preferences.putUInt("brightness", _brightness);
    }
}

String Settings::getWifiSSID() {
    return _wifiSSID;
}

void Settings::setWifiSSID(const String& ssid) {
    _wifiSSID = ssid;
    if (_initialized) {
        _preferences.putString("ssid", _wifiSSID);
    }
}

String Settings::getWifiPassword() {
    return _wifiPassword;
}

void Settings::setWifiPassword(const String& password) {
    _wifiPassword = password;
    if (_initialized) {
        _preferences.putString("password", _wifiPassword);
    }
}

String Settings::getClockText() {
    return _clockText;
}

void Settings::setClockText(const String& text) {
    _clockText = text;
    if (_initialized) {
        _preferences.putString("clockText", _clockText);
    }
}

String Settings::getLatitude() {
    return _latitude;
}

void Settings::setLatitude(const String& lat) {
    _latitude = lat;
    if (_initialized) {
        _preferences.putString("lat", _latitude);
    }
}

String Settings::getLongitude() {
    return _longitude;
}

void Settings::setLongitude(const String& lon) {
    _longitude = lon;
    if (_initialized) {
        _preferences.putString("lon", _longitude);
    }
}

uint8_t Settings::getDisplayMode() {
    return _displayMode;
}

void Settings::setDisplayMode(uint8_t mode) {
    _displayMode = mode;
    if (_initialized) {
        _preferences.putUChar("mode", _displayMode);
    }
}

uint8_t Settings::getRotation() {
    return _rotation;
}

void Settings::setRotation(uint8_t rotation) {
    if (rotation > 3) rotation = 3;
    _rotation = rotation;
    if (_initialized) {
        _preferences.putUChar("rotation", _rotation);
    }
}

uint32_t Settings::getSlideshowInterval() {
    return _slideshowInterval;
}

void Settings::setSlideshowInterval(uint32_t seconds) {
    _slideshowInterval = seconds;
    if (_initialized) {
        _preferences.putUInt("ss_interval", _slideshowInterval);
    }
}

String Settings::getHeartColor() {
    return _heartColor;
}

void Settings::setHeartColor(const String& hex) {
    _heartColor = hex;
    if (_initialized) {
        _preferences.putString("heartColor", _heartColor);
    }
}

void Settings::clearAll() {
    if (_initialized) {
        _preferences.clear();
        _wifiSSID = "";
        _wifiPassword = "";
        _brightness = Config::DEFAULT_BRIGHTNESS;
        _clockText = "";
        _latitude = DEFAULT_LATITUDE;
        _longitude = DEFAULT_LONGITUDE;
        _heartColor = DEFAULT_HEART_COLOR;
        _displayMode = 0;
        _rotation = Config::DEFAULT_ROTATION;
        _slideshowInterval = 5;
        LOG_INFO(TAG, "All settings cleared");
    }
}
