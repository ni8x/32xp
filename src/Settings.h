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

    // Fixed IPv4 address for the dashboard, so its URL survives a reboot.
    // Empty string = DHCP (the default).
    String getStaticIp();
    void setStaticIp(const String& ip);

    // Last DHCP lease: gateway, subnet mask and DNS server. A fixed address is
    // only usable together with the network's own gateway/mask, so the lease is
    // remembered and reused - the user only types the address they want.
    String getNetGateway();
    void setNetGateway(const String& gateway);
    String getNetSubnet();
    void setNetSubnet(const String& subnet);
    String getNetDns();
    void setNetDns(const String& dns);

    String getClockText();
    void setClockText(const String& text);

    // Clock face style: "digital" (default), "analogue", "8bit", "anime",
    // "minimal". Unknown values fall back to the digital face.
    String getClockStyle();
    void setClockStyle(const String& style);

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

    // Full-screen lyrics style: hide the header and progress bar and give the
    // scrolling lyric list the whole panel. On by default.
    bool getLyricFull();
    void setLyricFull(bool enabled);

    // Scroll transition: when the lyric advances, the whole list glides up to
    // centre the new active line instead of snapping.
    bool getLyricScroll();
    void setLyricScroll(bool enabled);

    // Preferred lyric text size (1..LYRIC_FULL_SIZE_MAX). One uniform size is
    // used for every row; the renderer steps down to 1 only if a very long line
    // could not otherwise fit vertically.
    uint8_t getLyricFullSize();
    void setLyricFullSize(uint8_t size);

    // Which sport the Scores plugin displays ("cricket" or "football"),
    // persisted so the plugin comes back on the same sport after a reboot.
    String getScoreSport();
    void setScoreSport(const String& sport);

    // --- Live football scores (ESPN's unofficial soccer scoreboard) ---
    // A league slug (for example "eng.1") is required.  An empty event ID
    // selects ESPN's live match, then the next scheduled match; a non-empty ID
    // pins the display to one match from that league.
    String getFootballLeagueSlug();
    void setFootballLeagueSlug(const String& slug);
    String getFootballEventId();
    void setFootballEventId(const String& id);
    String getFootballDate(); // empty = ESPN's current scoreboard; YYYYMMDD = matchday
    void setFootballDate(const String& date);
    uint16_t getFootballPollSeconds();
    void setFootballPollSeconds(uint16_t seconds);

    uint32_t getCricketLeagueId();
    void setCricketLeagueId(uint32_t id);
    String getCricketEventId();
    void setCricketEventId(const String& id);
    String getCricketDate();
    void setCricketDate(const String& date);
    uint16_t getCricketPollSeconds();
    void setCricketPollSeconds(uint16_t seconds);

    // Default values
    static constexpr const char* DEFAULT_HEART_COLOR = "#00d2ff";
    static constexpr const char* DEFAULT_LYRIC_COLOR = "#00ffff"; // cyan
    static constexpr uint8_t DEFAULT_LYRIC_FULL_SIZE = 2;
    static constexpr uint8_t LYRIC_FULL_SIZE_MAX = 2;
    static constexpr const char* DEFAULT_CLOCK_STYLE = "digital";

    void clearAll();
    
private:
    Preferences _preferences;
    uint8_t _brightness;
    String _wifiSSID;
    String _wifiPassword;
    String _staticIp;
    String _netGateway;
    String _netSubnet;
    String _netDns;
    String _clockText;
    String _clockStyle;
    String _theme;
    String _heartColor;
    bool _heartColorSet;
    String _lyricColor;
    bool _lyricFull;
    bool _lyricScroll;
    uint8_t _lyricFullSize;
    String _scoreSport;
    String _footballLeagueSlug;
    String _footballEventId;
    String _footballDate;
    uint16_t _footballPollSeconds;
    uint32_t _cricketLeagueId;
    String _cricketEventId;
    String _cricketDate;
    uint16_t _cricketPollSeconds;
    uint8_t _displayMode;
    uint8_t _rotation;
    uint32_t _slideshowInterval;
    bool _initialized;
};
