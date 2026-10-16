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
    {"midnight", "#5ad0ff", "#4dd0e1", "#8ab4ff", "#aee8ff", "#2e3d5c", "#3a5a8a", "#5eead4", "#5a6e8a", "#8ab4ff"},
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
      _clockStyle(DEFAULT_CLOCK_STYLE),
      _theme("default"),
      _heartColor(DEFAULT_HEART_COLOR),
      _heartColorSet(false),
      _lyricColor(DEFAULT_LYRIC_COLOR),
      _lyricGlow(false),
      _lyricRainbow(false),
      _lyricFull(false),
      _lyricGradient(false),
      _lyricScroll(true),
      _lyricFullSize(DEFAULT_LYRIC_FULL_SIZE),
      _scoreSport("cricket"),
      _footballLeagueSlug("eng.1"),
      _footballEventId(),
      _footballDate(),
      _footballPollSeconds(30),
      _cricketLeagueId(8048),
      _cricketEventId(),
      _cricketDate(),
      _cricketPollSeconds(30),
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
    _clockStyle = _preferences.getString("clockStyle", DEFAULT_CLOCK_STYLE);
    // Clamp to a known theme; an unknown stored value falls back to default
    // without touching the colors (only an explicit /api/theme change does).
    String storedTheme = _preferences.getString("theme", "default");
    _theme = getThemePalette(storedTheme) ? storedTheme : "default";
    _heartColor = _preferences.getString("heartColor", DEFAULT_HEART_COLOR);
    _heartColorSet = _preferences.getBool("heartColorSet", false);
    // Migration: installs that predate the flag stored the user's heart color
    // directly, so a stored color that differs from the current theme default
    // is a legacy user choice and must keep winning over the theme palette.
    if (!_heartColorSet && _heartColor != getThemePalette(_theme)->heartHex) {
        _heartColorSet = true;
    }
    _lyricColor = _preferences.getString("lyricColor", DEFAULT_LYRIC_COLOR);
    _lyricGlow = _preferences.getBool("lyricGlow", false);
    _lyricRainbow = _preferences.getBool("lyricRainbow", false);
    _lyricFull = _preferences.getBool("lyricFull", false);
    _lyricGradient = _preferences.getBool("lyricGradient", false);
    // Defaults on: the transition is what makes a line advance readable at a
    // glance, and existing devices (no stored key) should get it.
    _lyricScroll = _preferences.getBool("lyricScroll", true);
    _lyricFullSize = _preferences.getUChar("lyricFullSize", DEFAULT_LYRIC_FULL_SIZE);
    if (_lyricFullSize < 1) _lyricFullSize = 1;
    if (_lyricFullSize > LYRIC_FULL_SIZE_MAX) _lyricFullSize = LYRIC_FULL_SIZE_MAX;
    // Clamp to a known sport so a stale/corrupt NVS value can't blank the screen.
    _scoreSport = _preferences.getString("scoreSport", "cricket");
    if (_scoreSport != "cricket" && _scoreSport != "football") _scoreSport = "cricket";
    _footballLeagueSlug = _preferences.getString("espnLeague", "eng.1");
    _footballEventId = _preferences.getString("espnEvent", "");
    _footballDate = _preferences.getString("espnDate", "");
    _footballPollSeconds = _preferences.getUShort("espnPollSec", 30);
    if (_footballPollSeconds < 30) _footballPollSeconds = 30;
    if (_footballPollSeconds > 3600) _footballPollSeconds = 3600;
    _cricketLeagueId = _preferences.getULong("cricLeague", 8048);
    _cricketEventId = _preferences.getString("cricEvent", "");
    _cricketDate = _preferences.getString("cricDate", "");
    _cricketPollSeconds = _preferences.getUShort("cricPollSec", 30);
    if (_cricketPollSeconds < 30) _cricketPollSeconds = 30;
    if (_cricketPollSeconds > 3600) _cricketPollSeconds = 3600;
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

String Settings::getClockStyle() {
    return _clockStyle;
}

void Settings::setClockStyle(const String& style) {
    // Accept only known faces so a bad POST can never leave the clock blank.
    if (style != "digital" && style != "analogue" && style != "8bit" &&
        style != "anime" && style != "minimal") {
        return;
    }
    _clockStyle = style;
    if (_initialized) {
        _preferences.putString("clockStyle", _clockStyle);
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
    // A theme is a preset look: seed the lyric color to its palette and reset
    // the heart to the palette default (clearing any previous manual choice, so
    // it follows the theme again). Manual tweaks via the web UI still win.
    setLyricColor(palette->lyricHex);
    _heartColor = palette->heartHex;
    _heartColorSet = false;
    if (_initialized) {
        _preferences.putString("heartColor", _heartColor);
        _preferences.putBool("heartColorSet", false);
    }
    LOG_INFO(TAG, ("Theme applied: " + theme).c_str());
}

String Settings::getHeartColor() {
    return _heartColor;
}

void Settings::setHeartColor(const String& hex) {
    _heartColor = hex;
    _heartColorSet = true; // explicit user choice overrides the theme palette
    if (_initialized) {
        _preferences.putString("heartColor", _heartColor);
        _preferences.putBool("heartColorSet", true);
    }
}

bool Settings::isHeartColorCustom() {
    return _heartColorSet;
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

bool Settings::getLyricGradient() {
    return _lyricGradient;
}

void Settings::setLyricGradient(bool enabled) {
    _lyricGradient = enabled;
    if (_initialized) {
        _preferences.putBool("lyricGradient", _lyricGradient);
    }
}

bool Settings::getLyricScroll() {
    return _lyricScroll;
}

void Settings::setLyricScroll(bool enabled) {
    _lyricScroll = enabled;
    if (_initialized) {
        _preferences.putBool("lyricScroll", _lyricScroll);
    }
}

uint8_t Settings::getLyricFullSize() {
    return _lyricFullSize;
}

void Settings::setLyricFullSize(uint8_t size) {
    if (size < 1) size = 1;
    if (size > LYRIC_FULL_SIZE_MAX) size = LYRIC_FULL_SIZE_MAX;
    _lyricFullSize = size;
    if (_initialized) {
        _preferences.putUChar("lyricFullSize", _lyricFullSize);
    }
}

String Settings::getScoreSport() {
    return _scoreSport;
}

void Settings::setScoreSport(const String& sport) {
    // Only accept the two sports the Scores plugin knows about.
    if (sport != "cricket" && sport != "football") return;
    _scoreSport = sport;
    if (_initialized) {
        _preferences.putString("scoreSport", _scoreSport);
    }
}

String Settings::getFootballLeagueSlug() { return _footballLeagueSlug; }

void Settings::setFootballLeagueSlug(const String& slug) {
    if (slug.isEmpty() || slug.length() > 48) return;
    _footballLeagueSlug = slug;
    if (_initialized) _preferences.putString("espnLeague", _footballLeagueSlug);
}

String Settings::getFootballEventId() { return _footballEventId; }

void Settings::setFootballEventId(const String& id) {
    if (id.length() > 32) return;
    _footballEventId = id;
    if (_initialized) _preferences.putString("espnEvent", _footballEventId);
}

String Settings::getFootballDate() { return _footballDate; }

void Settings::setFootballDate(const String& date) {
    String digits;
    for (size_t i = 0; i < date.length(); ++i) {
        if (date[i] >= '0' && date[i] <= '9') digits += date[i];
    }
    if (!digits.isEmpty() && digits.length() != 8) return;
    _footballDate = digits;
    if (_initialized) _preferences.putString("espnDate", _footballDate);
}

uint16_t Settings::getFootballPollSeconds() { return _footballPollSeconds; }

void Settings::setFootballPollSeconds(uint16_t seconds) {
    if (seconds < 30) seconds = 30;
    if (seconds > 3600) seconds = 3600;
    _footballPollSeconds = seconds;
    if (_initialized) _preferences.putUShort("espnPollSec", _footballPollSeconds);
}

uint32_t Settings::getCricketLeagueId() { return _cricketLeagueId; }
void Settings::setCricketLeagueId(uint32_t id) { if (!id) return; _cricketLeagueId=id; if (_initialized) _preferences.putULong("cricLeague", id); }
String Settings::getCricketEventId() { return _cricketEventId; }
void Settings::setCricketEventId(const String& id) { if (id.length()>32) return; _cricketEventId=id; if (_initialized) _preferences.putString("cricEvent", id); }
String Settings::getCricketDate() { return _cricketDate; }
void Settings::setCricketDate(const String& date) {
    String digits; for (size_t i=0;i<date.length();++i) if (date[i]>='0' && date[i]<='9') digits+=date[i];
    if (!digits.isEmpty() && digits.length()!=8) return; _cricketDate=digits; if (_initialized) _preferences.putString("cricDate", digits);
}
uint16_t Settings::getCricketPollSeconds() { return _cricketPollSeconds; }
void Settings::setCricketPollSeconds(uint16_t seconds) { if(seconds<30) seconds=30; if(seconds>3600) seconds=3600; _cricketPollSeconds=seconds; if(_initialized) _preferences.putUShort("cricPollSec", seconds); }

void Settings::clearAll() {
    if (_initialized) {
        _preferences.clear();
        _wifiSSID = "";
        _wifiPassword = "";
        _brightness = Config::DEFAULT_BRIGHTNESS;
        _clockText = "";
        _clockStyle = DEFAULT_CLOCK_STYLE;
        _theme = "default";
        _heartColor = DEFAULT_HEART_COLOR;
        _heartColorSet = false;
        _lyricColor = DEFAULT_LYRIC_COLOR;
        _lyricGlow = false;
        _lyricRainbow = false;
        _lyricFull = false;
        _lyricGradient = false;
        _lyricScroll = true;
        _lyricFullSize = DEFAULT_LYRIC_FULL_SIZE;
        _scoreSport = "cricket";
        _footballLeagueSlug = "eng.1";
        _footballEventId = "";
        _footballDate = "";
        _footballPollSeconds = 30;
        _cricketLeagueId = 8048;
        _cricketEventId = "";
        _cricketDate = "";
        _cricketPollSeconds = 30;
        _displayMode = 0;
        _rotation = Config::DEFAULT_ROTATION;
        _slideshowInterval = 5;
        LOG_INFO(TAG, "All settings cleared");
    }
}
