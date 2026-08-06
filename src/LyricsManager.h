#pragma once

#include <Arduino.h>

class LyricsManager {
public:
    LyricsManager();
    ~LyricsManager();

    // Called by Android app via API
    void setLine(const String& current, const String& prev = "", const String& next = "", uint8_t progress = 0);
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
    bool _active;
    bool _phoneConnected;
};
