#include "LyricsManager.h"
#include "Logger.h"

static const char* TAG = "LyricsMgr";

LyricsManager::LyricsManager()
    : _progress(0), _active(false), _phoneConnected(false) {
}

LyricsManager::~LyricsManager() {
}

void LyricsManager::setLine(const String& current, const String& prev, const String& next, uint8_t progress) {
    _currentLine = current;
    _prevLine = prev;
    _nextLine = next;
    _progress = progress;
    _active = true;
    _phoneConnected = true;
}

void LyricsManager::setNowPlaying(const String& artist, const String& title) {
    _artist = artist;
    _title = title;
    _active = true;
    _phoneConnected = true;
    LOG_INFO(TAG, ("Now Playing: " + title + " - " + artist).c_str());
}

void LyricsManager::clear() {
    _currentLine = "";
    _prevLine = "";
    _nextLine = "";
    _artist = "";
    _title = "";
    _progress = 0;
    _active = false;
}

void LyricsManager::stop() {
    _currentLine = "";
    _prevLine = "";
    _nextLine = "";
    _progress = 0;
    _active = false;
}

const String& LyricsManager::getCurrentLine() const { return _currentLine; }
const String& LyricsManager::getPrevLine() const { return _prevLine; }
const String& LyricsManager::getNextLine() const { return _nextLine; }
const String& LyricsManager::getArtist() const { return _artist; }
const String& LyricsManager::getTitle() const { return _title; }
bool LyricsManager::isActive() const { return _active; }
uint8_t LyricsManager::getProgress() const { return _progress; }

void LyricsManager::setPhoneConnected(bool connected) {
    _phoneConnected = connected;
    if (connected) {
        LOG_INFO(TAG, "Phone connected via app");
    } else {
        LOG_INFO(TAG, "Phone disconnected");
    }
}

bool LyricsManager::isPhoneConnected() const { return _phoneConnected; }
