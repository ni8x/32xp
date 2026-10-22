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
    // Line timing from the phone, in song milliseconds: where the song is now,
    // and when the current line starts and ends. With it the panel animates the
    // line locally (word fill + continuous glide) and needs no extra traffic
    // between line changes; call it right after setLine(). Without it the line
    // length is estimated from the lines already seen.
    void setLineTiming(uint32_t positionMs, uint32_t lineStartMs, uint32_t lineEndMs);
    // How far through the current line we are: 0..100, or 255 when the line
    // cannot be paced at all (no text, or no timing and no estimate yet).
    uint8_t getLineProgress() const;
    bool hasLineTiming() const;
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

    // Phone connection tracking. Contact is refreshed by every line, now-playing
    // and ping; a phone that has been silent for too long is reported as
    // disconnected even though it once was (see PHONE_SILENCE_MS).
    void setPhoneConnected(bool connected);
    bool isPhoneConnected() const;
    // Seconds since the last contact, or 0xFFFFFFFF when never heard this boot.
    uint32_t getPhoneSilenceSec() const;

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
    // ── Line pacing (published as lineProgress) ──────────────────
    bool _hasLineTiming;    // start/end came from the phone
    bool _hasPosMs;         // the phone also sent a millisecond position
    uint32_t _posMs;        // song position in ms at the last update
    uint32_t _posMsAt;      // millis() when _posMs was set
    uint32_t _lineStartMs;  // song ms the current line starts at
    uint32_t _lineEndMs;    // song ms it ends at (next line's start)
    uint32_t _lineSetAt;    // millis() the current line was received
    uint32_t _estLineMs;    // EMA of how long recent lines lasted
    bool _active;
    bool _phoneConnected;
    uint32_t _lastContactAt; // millis() of the last line/now-playing/ping
};
