#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <vector>
#include <AnimatedGIF.h>
#include <PNGdec.h>
#include <TJpg_Decoder.h>
#include "Display.h"
#include "Settings.h"

class MediaManager {
public:
    MediaManager(Display& display, Settings& settings);
    ~MediaManager();

    void begin();
    void update();
    void reset(); // Call when re-entering media mode to force immediate playback
    void stop();
    void processRequests();

    std::vector<String> getFileList();
    bool requestDelete(const String& filename);
    
    static Display* activeDisplay; // Static pointer for decoder callbacks
    static int _gifCanvasW;        // Full GIF canvas width (for centering)
    static int _gifCanvasH;        // Full GIF canvas height (for centering)
    static float _gifScale;
    static int _gifOffsetX;
    static int _gifOffsetY;
    static int _pngCanvasW;
    static int _pngCanvasH;

private:
    Display& _display;
    Settings& _settings;
    AnimatedGIF _gif;
    PNG _png;
    static PNG* activePng;

    std::vector<String> _mediaFiles;
    size_t _currentIndex;
    uint32_t _lastSwitchTime;
    uint32_t _nextGifFrameTime;
    uint32_t _gifStartedAt;
    uint8_t _gifDecodeErrors;
    bool _isGifPlaying;
    File _gifFile;
    std::vector<String> _pendingDeletes;
    SemaphoreHandle_t _requestMutex;

    void refreshFileList();
    void collectMediaFiles(std::vector<String>& out, bool log);
    bool deleteFile(const String& filename);
    void playNext();
    void renderJpeg(const String& path);
    void renderPng(const String& path);
    void startGif(const String& path);

    static bool jpegCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
    static void gifDrawCallback(GIFDRAW* pDraw);
    static void* gifOpenCallback(const char* fname, int32_t* pSize);
    static void gifCloseCallback(void* pHandle);
    static int32_t gifReadCallback(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen);
    static int32_t gifSeekCallback(GIFFILE* pFile, int32_t iPosition);
    static int pngDrawCallback(PNGDRAW* pDraw);
    static void* pngOpenCallback(const char* fname, int32_t* pSize);
    static void pngCloseCallback(void* pHandle);
    static int32_t pngReadCallback(PNGFILE* pFile, uint8_t* pBuf, int32_t iLen);
    static int32_t pngSeekCallback(PNGFILE* pFile, int32_t iPosition);
};
