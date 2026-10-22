#include "LyricsManager.h"
#include "Logger.h"

static const char* TAG = "LyricsMgr";

// Line length assumed until real ones have been observed (~3 s per lyric line).
static constexpr uint32_t EST_LINE_MS_DEFAULT = 3000;

// How long the phone may stay silent before it stops counting as connected.
// The app pings at least every 15 s while its sync service runs, so silence this
// long means the service died, the phone left the network, or it is sending to
// the wrong address. Without this the panel claimed "Phone Connected" forever
// after a single ping and hid exactly that failure.
static constexpr uint32_t PHONE_SILENCE_MS = 45000;

LyricsManager::LyricsManager()
    : _progress(0), _positionSec(0), _durationSec(0),      _posUpdatedAt(0),
      _active(false), _phoneConnected(false), _lastContactAt(0),
      _hasLineTiming(false), _hasPosMs(false), _posMs(0), _posMsAt(0),
      _lineStartMs(0), _lineEndMs(0), _lineSetAt(0),
      _estLineMs(EST_LINE_MS_DEFAULT) {
}

LyricsManager::~LyricsManager() {
}

void LyricsManager::setLine(const String& current, const String& prev, const String& next,
                            uint8_t progress, uint32_t positionSec, uint32_t durationSec) {
    const bool lineChanged = (current != _currentLine);
    if (lineChanged) {
        // Learn how long a line lasts from the ones already seen, so the fill and
        // the glide stay paced even when the phone sends no line timing (older
        // app builds, or text typed into the dashboard). Only sane intervals
        // count - a pause or a long intro must not skew the average.
        const uint32_t held = millis() - _lineSetAt;
        if (_lineSetAt != 0 && held > 400 && held < 30000) {
            _estLineMs = (_estLineMs * 3 + held) / 4;
        }
        _lineSetAt = millis();
        // Timing is per line: drop the previous line's until the phone says
        // otherwise (it calls setLineTiming() right after this).
        _hasLineTiming = false;
    }
    _currentLine = current;
    // The Android app re-sends the current line every 10 s without prev/next to
    // re-anchor playback time; keep the context lines in that case.
    if (lineChanged) {
        _prevLine = prev;
        _nextLine = next;
    }
    _progress = progress;
    _positionSec = positionSec;
    _durationSec = durationSec;
    _posUpdatedAt = millis();
    _active = true;
    _phoneConnected = true;
    _lastContactAt = millis();
}

void LyricsManager::setLineTiming(uint32_t positionMs, uint32_t lineStartMs, uint32_t lineEndMs) {
    if (lineEndMs <= lineStartMs) {
        _hasLineTiming = false;
        return;
    }
    _lineStartMs = lineStartMs;
    _lineEndMs = lineEndMs;
    _hasLineTiming = true;
    if (positionMs > 0) {
        _posMs = positionMs;
        _posMsAt = millis();
        _hasPosMs = true;
    }
}

bool LyricsManager::hasLineTiming() const { return _hasLineTiming; }

uint8_t LyricsManager::getLineProgress() const {
    if (_currentLine.isEmpty() || _lineSetAt == 0) return 255;

    if (_hasLineTiming) {
        const uint32_t span = _lineEndMs - _lineStartMs;
        uint32_t into;
        if (_hasPosMs) {
            // Song clock: re-anchored by the phone on every line change and by
            // its periodic position updates, ticking locally in between.
            const uint32_t songMs = _posMs + (millis() - _posMsAt);
            if (songMs <= _lineStartMs) return 0;
            into = songMs - _lineStartMs;
        } else {
            into = millis() - _lineSetAt;   // real span, but timed from arrival
        }
        return into >= span ? 100 : (uint8_t)((uint64_t)into * 100 / span);
    }

    // No timing at all: pace the line against how long recent lines lasted.
    if (_estLineMs == 0) return 255;
    const uint32_t held = millis() - _lineSetAt;
    return held >= _estLineMs ? 100 : (uint8_t)((uint64_t)held * 100 / _estLineMs);
}

void LyricsManager::setNowPlaying(const String& artist, const String& title) {
    _artist = artist;
    _title = title;
    // New song: drop the previous song's time info so stale timestamps can't
    // linger until the first line of the new song arrives.
    _positionSec = 0;
    _durationSec = 0;
    _posUpdatedAt = 0;
    // Line pacing too: the next line starts the new song's timeline.
    _hasLineTiming = false;
    _hasPosMs = false;
    _lineSetAt = 0;
    _estLineMs = EST_LINE_MS_DEFAULT;
    _active = true;
    _phoneConnected = true;
    _lastContactAt = millis();
    LOG_INFO(TAG, ("Now Playing: " + title + " - " + artist).c_str());
}

void LyricsManager::clear() {
    _currentLine = "";
    _prevLine = "";
    _nextLine = "";
    _artist = "";
    _title = "";
    _progress = 0;
    _positionSec = 0;
    _durationSec = 0;
    _posUpdatedAt = 0;
    _hasLineTiming = false;
    _hasPosMs = false;
    _lineSetAt = 0;
    _active = false;
}

void LyricsManager::stop() {
    _currentLine = "";
    _prevLine = "";
    _nextLine = "";
    _progress = 0;
    _positionSec = 0;
    _durationSec = 0;
    _posUpdatedAt = 0;
    _hasLineTiming = false;
    _hasPosMs = false;
    _lineSetAt = 0;
    _active = false;
}

const String& LyricsManager::getCurrentLine() const { return _currentLine; }
const String& LyricsManager::getPrevLine() const { return _prevLine; }
const String& LyricsManager::getNextLine() const { return _nextLine; }
const String& LyricsManager::getArtist() const { return _artist; }
const String& LyricsManager::getTitle() const { return _title; }
bool LyricsManager::isActive() const { return _active; }
uint8_t LyricsManager::getProgress() const { return _progress; }

uint32_t LyricsManager::getElapsedSec() const {
    // Live-tick between app line updates so the timestamp keeps moving.
    if (_durationSec == 0) return 0; // unknown duration -> hide timestamps
    uint32_t elapsed = _positionSec + (millis() - _posUpdatedAt) / 1000;
    return (elapsed > _durationSec) ? _durationSec : elapsed;
}

uint32_t LyricsManager::getDurationSec() const {
    return _durationSec;
}

void LyricsManager::setPhoneConnected(bool connected) {
    _phoneConnected = connected;
    if (connected) {
        _lastContactAt = millis();
        LOG_INFO(TAG, "Phone connected via app");
    } else {
        LOG_INFO(TAG, "Phone disconnected");
    }
}

bool LyricsManager::isPhoneConnected() const {
    if (!_phoneConnected) return false;
    // Any line, now-playing or ping counts as contact (see _lastContactAt).
    return _lastContactAt != 0 && (millis() - _lastContactAt) < PHONE_SILENCE_MS;
}

uint32_t LyricsManager::getPhoneSilenceSec() const {
    if (_lastContactAt == 0) return 0xFFFFFFFFu; // never heard from this boot
    return (millis() - _lastContactAt) / 1000;
}
