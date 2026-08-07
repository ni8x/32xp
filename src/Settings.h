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

    uint8_t getRotation();
    void setRotation(uint8_t rotation);

    uint32_t getSlideshowInterval();
    void setSlideshowInterval(uint32_t seconds);

    // A named preset look. Selecting a theme seeds the lyric & heart colors to
    // its palette; every individual customization (web UI) still works on top.
    String getTheme();
    void setTheme(const String& theme);

    // Palette for the current theme. All entries are #RRGGBB strings so callers
    // convert with Display::hexToRGB565(). Returns nullptr for unknown names.
    struct ThemePalette {
        const char* name;
        const char* lyricHex;  // default lyric text color
        const char* heartHex;  // default pixel-heart color
        const char* accentHex; // headers / section labels
        const char* timeHex;   // clock digits
        const char* dimHex;    // secondary text (prev/next lines, idle hints)
        const char* lineHex;   // separator lines
        const char* okHex;     // connected status / active dot
        const char* warnHex;   // waiting / muted hints
        const char* urlHex;    // web dashboard URL
    };
    static const ThemePalette* getThemePalette(const String& name);

    String getHeartColor();
    void setHeartColor(const String& hex);

    // True once the user picked a heart color manually (web UI). The clock then
    // uses that color; otherwise the heart follows the active theme palette.
    bool isHeartColorCustom();

    String getLyricColor();
    void setLyricColor(const String& hex);

    bool getLyricGlow();
    void setLyricGlow(bool enabled);

    bool getLyricRainbow();
    void setLyricRainbow(bool enabled);

    // Full-screen lyrics style: hide prev/next lines and center the current line
    bool getLyricFull();
    void setLyricFull(bool enabled);

    // Default values
    static constexpr const char* DEFAULT_LATITUDE = "28.61";
    static constexpr const char* DEFAULT_LONGITUDE = "77.20";
    static constexpr const char* DEFAULT_HEART_COLOR = "#00d2ff";
    static constexpr const char* DEFAULT_LYRIC_COLOR = "#00ffff"; // cyan

    void clearAll();
    
private:
    Preferences _preferences;
    uint8_t _brightness;
    String _wifiSSID;
    String _wifiPassword;
    String _clockText;
    String _latitude;
    String _longitude;
    String _theme;
    String _heartColor;
    bool _heartColorSet;
    String _lyricColor;
    bool _lyricGlow;
    bool _lyricRainbow;
    bool _lyricFull;
    uint8_t _displayMode;
    uint8_t _rotation;
    uint32_t _slideshowInterval;
    bool _initialized;
};
