#pragma once

#include <Arduino.h>

class LyricsManager {
public:
    LyricsManager();
    ~LyricsManager();

    // Called by Android app via API. positionSec/durationSec are optional song
    // time info (0 = unknown) used to draw timestamps on the progress bar.
    void setLine(const String& current, const String& prev = "", const String& next = "",
                 uint8_t progress = 0, uint32_t positionSec = 0, uint32_t durationSec = 0);
    void setNowPlaying(const String& artist, const String& title);
    void clear();
    void stop();

    // Getters for App display logic
    const String& getCurrentLine() const;
    const String& getPrevLine() const;
    const String& getNextLine() const;
    const String& getArtist() const;
    const String& getTitle() const;
    bool isActive() const;
    uint8_t getProgress() const;

    // Song playback time: live-ticking elapsed seconds (ticks locally between
    // line updates) and total duration. Both 0 when the source is unknown.
    uint32_t getElapsedSec() const;
    uint32_t getDurationSec() const;

    // Phone connection tracking
    void setPhoneConnected(bool connected);
    bool isPhoneConnected() const;

private:
    String _currentLine;
    String _prevLine;
    String _nextLine;
    String _artist;
    String _title;
    uint8_t _progress;
    uint32_t _positionSec;  // elapsed sec reported by the app at the last line
    uint32_t _durationSec;  // total song length in sec (0 = unknown)
    uint32_t _posUpdatedAt; // millis() when _positionSec was set
    bool _active;
    bool _phoneConnected;
};
