#include "Settings.h"
#include "Config.h"
#include "Logger.h"

static const char* TAG = "Settings";

// Built-in themes. "default" mirrors the classic hardcoded look; the others
// restyle the chrome (headers, labels, clock, status colors) and seed the
// lyric/heart color pickers when selected.
static const Settings::ThemePalette THEMES[] = {
    // name       lyric     heart     accent    time      dim       line      ok        warn      url
    {"default",   "#00ffff", "#00d2ff", "#ffd700", "#00ffff", "#3c3c3c", "#404040", "#00ff00", "#646464", "#00ffff"},
    {"pikachu",  "#ffde00", "#ffb400", "#ffde00", "#ffe74c", "#4a432b", "#7a6a1e", "#a8d84f", "#8a7f4d", "#ffe74c"},
    {"cyberpunk", "#05d9e8", "#ff2a6d", "#ff2a6d", "#05d9e8", "#3d3a5c", "#522557", "#39ff14", "#6f6a8f", "#05d9e8"},
};

const Settings::ThemePalette* Settings::getThemePalette(const String& name) {
    for (size_t i = 0; i < sizeof(THEMES) / sizeof(THEMES[0]); i++) {
        if (name == THEMES[i].name) {
            return &THEMES[i];
        }
    }
    return nullptr;
}

Settings::Settings()
    : _brightness(Config::DEFAULT_BRIGHTNESS),
      _wifiSSID(),
      _wifiPassword(),
      _clockText(),
      _latitude(DEFAULT_LATITUDE),
      _longitude(DEFAULT_LONGITUDE),
      _theme("default"),
      _heartColor(DEFAULT_HEART_COLOR),
      _lyricColor(DEFAULT_LYRIC_COLOR),
      _lyricGlow(false),
      _lyricRainbow(false),
      _lyricFull(false),
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
    // Clamp to a known theme; an unknown stored value falls back to default
    // without touching the colors (only an explicit /api/theme change does).
    String storedTheme = _preferences.getString("theme", "default");
    _theme = getThemePalette(storedTheme) ? storedTheme : "default";
    _heartColor = _preferences.getString("heartColor", DEFAULT_HEART_COLOR);
    _lyricColor = _preferences.getString("lyricColor", DEFAULT_LYRIC_COLOR);
    _lyricGlow = _preferences.getBool("lyricGlow", false);
    _lyricRainbow = _preferences.getBool("lyricRainbow", false);
    _lyricFull = _preferences.getBool("lyricFull", false);
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

String Settings::getTheme() {
    return _theme;
}

void Settings::setTheme(const String& theme) {
    const ThemePalette* palette = getThemePalette(theme);
    if (!palette) {
        LOG_WARN(TAG, ("Ignoring unknown theme: " + theme).c_str());
        return;
    }
    _theme = theme;
    if (_initialized) {
        _preferences.putString("theme", _theme);
    }
    // A theme is a preset look: seed the customizable colors to its palette.
    // Anything the user tweaks afterwards (web UI) is kept as-is.
    setLyricColor(palette->lyricHex);
    setHeartColor(palette->heartHex);
    LOG_INFO(TAG, ("Theme applied: " + theme).c_str());
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

String Settings::getLyricColor() {
    return _lyricColor;
}

void Settings::setLyricColor(const String& hex) {
    _lyricColor = hex;
    if (_initialized) {
        _preferences.putString("lyricColor", _lyricColor);
    }
}

bool Settings::getLyricGlow() {
    return _lyricGlow;
}

void Settings::setLyricGlow(bool enabled) {
    _lyricGlow = enabled;
    if (_initialized) {
        _preferences.putBool("lyricGlow", _lyricGlow);
    }
}

bool Settings::getLyricRainbow() {
    return _lyricRainbow;
}

void Settings::setLyricRainbow(bool enabled) {
    _lyricRainbow = enabled;
    if (_initialized) {
        _preferences.putBool("lyricRainbow", _lyricRainbow);
    }
}

bool Settings::getLyricFull() {
    return _lyricFull;
}

void Settings::setLyricFull(bool enabled) {
    _lyricFull = enabled;
    if (_initialized) {
        _preferences.putBool("lyricFull", _lyricFull);
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
        _theme = "default";
        _heartColor = DEFAULT_HEART_COLOR;
        _lyricColor = DEFAULT_LYRIC_COLOR;
        _lyricGlow = false;
        _lyricRainbow = false;
        _lyricFull = false;
        _displayMode = 0;
        _rotation = Config::DEFAULT_ROTATION;
        _slideshowInterval = 5;
        LOG_INFO(TAG, "All settings cleared");
    }
}
