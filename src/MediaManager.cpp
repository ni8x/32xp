#include "MediaManager.h"
#include "Config.h"
#include "Logger.h"

static const char* TAG = "MediaManager";

Display* MediaManager::activeDisplay = nullptr;
int MediaManager::_gifCanvasW = 0;
int MediaManager::_gifCanvasH = 0;
float MediaManager::_gifScale = 1.0f;
int MediaManager::_gifOffsetX = 0;
int MediaManager::_gifOffsetY = 0;
int MediaManager::_pngCanvasW = 0;
int MediaManager::_pngCanvasH = 0;
PNG* MediaManager::activePng = nullptr;

MediaManager::MediaManager(Display& display, Settings& settings)
    : _display(display), _settings(settings), _currentIndex(0), _lastSwitchTime(0), _nextGifFrameTime(0),
      _gifStartedAt(0), _gifDecodeErrors(0), _isGifPlaying(false),
      _requestMutex(xSemaphoreCreateMutex()) {
    activeDisplay = &_display;
}

MediaManager::~MediaManager() {
    if (_isGifPlaying) {
        _gif.close();
    }
    if (_requestMutex) {
        vSemaphoreDelete(_requestMutex);
    }
}

void MediaManager::stop() {
    if (_isGifPlaying) {
        _gif.close();
        _isGifPlaying = false;
    }
    _currentIndex = 0;
    _lastSwitchTime = 0;
    _nextGifFrameTime = 0;
    _gifStartedAt = 0;
    _gifDecodeErrors = 0;
}

void MediaManager::reset() {
    stop();
    _display.fillScreen(BLACK);
    refreshFileList();
}

bool MediaManager::requestDelete(const String& filename) {
    String path = filename;
    path.trim();
    if (path.isEmpty()) return false;
    if (!path.startsWith("/")) path = "/" + path;

    if (!_requestMutex || xSemaphoreTake(_requestMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }
    _pendingDeletes.push_back(path);
    xSemaphoreGive(_requestMutex);
    return true;
}

void MediaManager::processRequests() {
    String path;
    if (_requestMutex && xSemaphoreTake(_requestMutex, 0) == pdTRUE) {
        if (!_pendingDeletes.empty()) {
            path = _pendingDeletes.front();
            _pendingDeletes.erase(_pendingDeletes.begin());
        }
        xSemaphoreGive(_requestMutex);
    }

    if (!path.isEmpty()) {
        // This runs in the display loop, never in the asynchronous web-server task.
        // It is therefore safe to close the GIF decoder before LittleFS removes its file.
        deleteFile(path);
    }
}

void MediaManager::begin() {
    LOG_INFO(TAG, "Initializing LittleFS...");
    if (!LittleFS.begin(true)) {
        LOG_ERROR(TAG, "Failed to mount LittleFS!");
        return;
    }
    LOG_INFO(TAG, "LittleFS mounted successfully.");

    // Setup TJpg_Decoder
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(jpegCallback);

    // Setup AnimatedGIF
    _gif.begin(LITTLE_ENDIAN_PIXELS);

    refreshFileList();
}

void MediaManager::collectMediaFiles(std::vector<String>& out, bool log) {
    out.clear();
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
        if (log) LOG_ERROR(TAG, "Failed to open root directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        String name = file.name();
        if (!name.startsWith("/")) name = "/" + name;

        String lowerName = name;
        lowerName.toLowerCase();
        if (lowerName.endsWith(".jpg") || lowerName.endsWith(".jpeg") ||
            lowerName.endsWith(".gif") || lowerName.endsWith(".png")) {
            out.push_back(name);
            if (log) LOG_INFO(TAG, ("Found media file: " + name).c_str());
        }
        file = root.openNextFile();
    }
}

void MediaManager::refreshFileList() {
    collectMediaFiles(_mediaFiles, true);
}

std::vector<String> MediaManager::getFileList() {
    // This method is called by the asynchronous web-server task.  Do not
    // refresh _mediaFiles here: modifying that vector while update()/playNext()
    // is using it can corrupt the slideshow state.  Build an independent list.
    std::vector<String> files;
    collectMediaFiles(files, false);
    return files;
}

bool MediaManager::deleteFile(const String& filename) {
    // Normalize path - ensure it starts with /
    String path = filename;
    path.trim();
    if (!path.startsWith("/")) path = "/" + path;

    // If a GIF is currently playing, close it first so the file handle is released
    if (_isGifPlaying) {
        _gif.close();
        _isGifPlaying = false;
        _display.fillScreen(BLACK);
        _currentIndex = 0;
        _lastSwitchTime = 0;
    }

    LOG_INFO(TAG, ("Attempting to delete: " + path).c_str());

    if (LittleFS.exists(path)) {
        bool res = LittleFS.remove(path);
        if (res) {
            LOG_INFO(TAG, "File deleted successfully");
        } else {
            LOG_ERROR(TAG, "LittleFS.remove() returned false");
        }
        refreshFileList();
        return res;
    }
    
    LOG_ERROR(TAG, ("File not found on LittleFS: " + path).c_str());
    return false;
}

void MediaManager::update() {
    if (_mediaFiles.empty()) {
        refreshFileList();
        if (_mediaFiles.empty()) {
            static uint32_t lastCheck = 0;
            if (millis() - lastCheck > 2000) {
                lastCheck = millis();
                _display.fillScreen(BLACK);
                _display.drawCenteredText("No Media Files", WHITE, 2);
                _display.drawText(20, _display.height() / 2 + 30, "Upload via Dashboard", YELLOW, 1);
            }
            return;
        }
    }

    uint32_t now = millis();
    uint32_t intervalMs = _settings.getSlideshowInterval() * 1000;

    // If a GIF is playing, keep stepping its frames
    if (_isGifPlaying) {
        if (now < _nextGifFrameTime) return;

        int frameDelay = 0;
        int result = _gif.playFrame(false, &frameDelay);
        _nextGifFrameTime = now + (frameDelay > 0 ? (uint32_t)frameDelay : 10);

        if (result < 0) {
            LOG_ERROR(TAG, ("GIF decode failed: " + String(_gif.getLastError())).c_str());
            _gifDecodeErrors++;
            if (_gifDecodeErrors < 3) {
                _gif.reset();
                _nextGifFrameTime = now + 10;
            } else {
                LOG_ERROR(TAG, "Skipping GIF after repeated decode failures");
                _gif.close();
                _isGifPlaying = false;
                _lastSwitchTime = now;
                playNext();
            }
        } else if (result == 0) {
            _gifDecodeErrors = 0;
            // GIF finished one full loop — check if interval elapsed to move to next
            if (now - _gifStartedAt >= intervalMs) {
                _gif.close();
                _isGifPlaying = false;
                _lastSwitchTime = now;
                playNext();
            } else {
                // Loop the GIF again
                _gif.reset();
            }
        }
        return; // Don't check interval below while GIF is playing
    }

    // JPEG: advance on interval
    if (now - _lastSwitchTime >= intervalMs || _lastSwitchTime == 0) {
        _lastSwitchTime = now;
        playNext();
    }
}

void MediaManager::playNext() {
    if (_mediaFiles.empty()) return;

    if (_currentIndex >= _mediaFiles.size()) {
        _currentIndex = 0;
    }

    String currentFile = _mediaFiles[_currentIndex];
    LOG_INFO(TAG, ("Playing media [" + String(_currentIndex) + "]: " + currentFile).c_str());
    _currentIndex++;

    String lowerName = currentFile;
    lowerName.toLowerCase();

    if (lowerName.endsWith(".gif")) {
        startGif(currentFile);
    } else if (lowerName.endsWith(".png")) {
        renderPng(currentFile);
    } else {
        renderJpeg(currentFile);
    }
}

void MediaManager::renderJpeg(const String& path) {
    _display.fillScreen(BLACK);
    uint16_t w = 0, h = 0;
    TJpgDec.getFsJpgSize(&w, &h, path.c_str(), LittleFS);

    // Use the live display size so scaling/centering follow the current
    // orientation (240x320 portrait or 320x240 landscape).
    int16_t dispW = _display.width();
    int16_t dispH = _display.height();

    uint8_t scale = 1;
    if (w > dispW * 4 || h > dispH * 4) scale = 8;
    else if (w > dispW * 2 || h > dispH * 2) scale = 4;
    else if (w > dispW || h > dispH) scale = 2;
    TJpgDec.setJpgScale(scale);

    uint16_t scaledW = w / scale;
    uint16_t scaledH = h / scale;
    int16_t x = (scaledW < dispW) ? (dispW - scaledW) / 2 : 0;
    int16_t y = (scaledH < dispH) ? (dispH - scaledH) / 2 : 0;
    TJpgDec.drawFsJpg(x, y, path.c_str(), LittleFS);
}

void MediaManager::renderPng(const String& path) {
    _display.fillScreen(BLACK);
    activePng = &_png;
    if (_png.open(path.c_str(), pngOpenCallback, pngCloseCallback, pngReadCallback, pngSeekCallback, pngDrawCallback) != PNG_SUCCESS) {
        LOG_ERROR(TAG, "Failed to open PNG");
        activePng = nullptr;
        return;
    }

    _pngCanvasW = _png.getWidth();
    _pngCanvasH = _png.getHeight();
    int16_t dispW = _display.width();
    int16_t dispH = _display.height();
    if (_pngCanvasW > dispW || _pngCanvasH > dispH) {
        LOG_ERROR(TAG, ("PNG exceeds display size; use a PNG no larger than " +
                         String(dispW) + "x" + String(dispH)).c_str());
        _png.close();
        activePng = nullptr;
        return;
    }

    // PNGdec blends transparent pixels against black before each line is drawn.
    int result = _png.decode(nullptr, 0);
    if (result != PNG_SUCCESS) {
        LOG_ERROR(TAG, ("PNG decode failed: " + String(result)).c_str());
    }
    _png.close();
    activePng = nullptr;
}

void MediaManager::startGif(const String& path) {
    _display.fillScreen(BLACK);
    if (_gif.open(path.c_str(), gifOpenCallback, gifCloseCallback, gifReadCallback, gifSeekCallback, gifDrawCallback)) {
        // Store full canvas dimensions ONCE for use in the draw callback
        _gifCanvasW = _gif.getCanvasWidth();
        _gifCanvasH = _gif.getCanvasHeight();
        _gifScale = min((float)_display.width() / _gifCanvasW,
                        (float)_display.height() / _gifCanvasH);
        int scaledW = (int)(_gifCanvasW * _gifScale + 0.5f);
        int scaledH = (int)(_gifCanvasH * _gifScale + 0.5f);
        _gifOffsetX = (_display.width() - scaledW) / 2;
        _gifOffsetY = (_display.height() - scaledH) / 2;
        _nextGifFrameTime = 0;
        _gifStartedAt = millis();
        _gifDecodeErrors = 0;
        _isGifPlaying = true;
        LOG_INFO(TAG, ("GIF opened. Canvas: " + String(_gifCanvasW) + "x" + String(_gifCanvasH) +
                       ", scale: " + String(_gifScale, 2)).c_str());
    } else {
        LOG_ERROR(TAG, "Failed to open GIF");
        _isGifPlaying = false;
    }
}

// TJpg_Decoder Callback
bool MediaManager::jpegCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (activeDisplay && activeDisplay->getGFX()) {
        activeDisplay->getGFX()->draw16bitRGBBitmap(x, y, bitmap, w, h);
    }
    return true;
}

// AnimatedGIF Callbacks
void MediaManager::gifDrawCallback(GIFDRAW* pDraw) {
    if (!activeDisplay || !activeDisplay->getGFX()) return;

    uint16_t* usPalette = pDraw->pPalette;
    uint8_t* p = pDraw->pPixels;
    int y = pDraw->iY + pDraw->y;
    int x = pDraw->iX;
    int width = pDraw->iWidth;

    int dstY0 = _gifOffsetY + (int)(y * _gifScale);
    int dstY1 = _gifOffsetY + (int)((y + 1) * _gifScale);

    // For a downscaled GIF, emit only the source pixels that contribute to a
    // destination pixel. Draw opaque runs as scanlines instead of issuing one
    // SPI transaction per pixel; this keeps animated 450x450 GIFs responsive.
    if (_gifScale <= 1.0f) {
        if (dstY1 <= dstY0 || dstY0 < 0 || dstY0 >= activeDisplay->height()) return;
        // Sized for the widest possible row in either orientation.
        static uint16_t line[Config::MAX_DISPLAY_DIM];
        int runStart = -1;
        int runLength = 0;

        auto flushRun = [&]() {
            if (runLength > 0) {
                activeDisplay->getGFX()->draw16bitRGBBitmap(_gifOffsetX + runStart, dstY0,
                                                             line, runLength, 1);
                runStart = -1;
                runLength = 0;
            }
        };

        for (int i = 0; i < width; i++) {
            uint8_t pixel = p[i];
            int srcX = x + i;
            int dstX0 = _gifOffsetX + (int)(srcX * _gifScale);
            int dstX1 = _gifOffsetX + (int)((srcX + 1) * _gifScale);
            if (dstX1 <= dstX0 || dstX0 < 0 || dstX0 >= activeDisplay->width() ||
                (pDraw->ucHasTransparency && pixel == pDraw->ucTransparent)) {
                flushRun();
                continue;
            }

            int relativeX = dstX0 - _gifOffsetX;
            if (runStart < 0 || relativeX != runStart + runLength) {
                flushRun();
                runStart = relativeX;
            }
            line[runLength++] = usPalette[pixel];
        }
        flushRun();
        return;
    }

    for (int i = 0; i < width; i++) {
        uint8_t pixel = p[i];
        if (pDraw->ucHasTransparency && pixel == pDraw->ucTransparent) continue;

        // Map each source pixel into its destination rectangle.  This handles
        // both downscaling large GIFs and enlarging small pixel-art GIFs.
        int srcX = x + i;
        int dstX0 = _gifOffsetX + (int)(srcX * _gifScale);
        int dstX1 = _gifOffsetX + (int)((srcX + 1) * _gifScale);
        if (dstX0 >= activeDisplay->width() || dstX1 <= 0 ||
            dstY0 >= activeDisplay->height() || dstY1 <= 0) continue;

        int drawX = max(0, dstX0);
        int drawY = max(0, dstY0);
        int drawW = min((int)activeDisplay->width(), dstX1) - drawX;
        int drawH = min((int)activeDisplay->height(), dstY1) - drawY;
        if (drawW > 0 && drawH > 0) {
            activeDisplay->getGFX()->fillRect(drawX, drawY, drawW, drawH, usPalette[pixel]);
        }
    }
}

void* MediaManager::gifOpenCallback(const char* fname, int32_t* pSize) {
    File f = LittleFS.open(fname, "r");
    if (!f) return nullptr;
    *pSize = f.size();
    File* handle = new File(f);
    return (void*)handle;
}

void MediaManager::gifCloseCallback(void* pHandle) {
    File* f = (File*)pHandle;
    if (f) {
        f->close();
        delete f;
    }
}

int32_t MediaManager::gifReadCallback(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen) {
    File* f = (File*)pFile->fHandle;
    if (!f) return 0;
    int32_t bytesRead = f->read(pBuf, iLen);
    pFile->iPos += bytesRead;
    return bytesRead;
}

int32_t MediaManager::gifSeekCallback(GIFFILE* pFile, int32_t iPosition) {
    File* f = (File*)pFile->fHandle;
    if (!f) return 0;
    if (!f->seek(iPosition)) return -1;
    pFile->iPos = iPosition;
    return iPosition;
}

int MediaManager::pngDrawCallback(PNGDRAW* pDraw) {
    if (!activeDisplay || !activeDisplay->getGFX() || !activePng || pDraw->iWidth > activeDisplay->width()) return 0;

    // Sized for the widest possible row in either orientation.
    static uint16_t line[Config::MAX_DISPLAY_DIM];
    int x = (activeDisplay->width() - _pngCanvasW) / 2;
    int y = (activeDisplay->height() - _pngCanvasH) / 2 + pDraw->y;
    activePng->getLineAsRGB565(pDraw, line, PNG_RGB565_LITTLE_ENDIAN, 0x00000000);
    activeDisplay->getGFX()->draw16bitRGBBitmap(x, y, line, pDraw->iWidth, 1);
    return 1;
}

void* MediaManager::pngOpenCallback(const char* fname, int32_t* pSize) {
    File f = LittleFS.open(fname, "r");
    if (!f) return nullptr;
    *pSize = f.size();
    return new File(f);
}

void MediaManager::pngCloseCallback(void* pHandle) {
    File* f = static_cast<File*>(pHandle);
    if (f) {
        f->close();
        delete f;
    }
}

int32_t MediaManager::pngReadCallback(PNGFILE* pFile, uint8_t* pBuf, int32_t iLen) {
    File* f = static_cast<File*>(pFile->fHandle);
    if (!f) return 0;
    int32_t bytesRead = f->read(pBuf, iLen);
    pFile->iPos += bytesRead;
    return bytesRead;
}

int32_t MediaManager::pngSeekCallback(PNGFILE* pFile, int32_t iPosition) {
    File* f = static_cast<File*>(pFile->fHandle);
    if (!f) return 0;
    if (!f->seek(iPosition)) return -1;
    pFile->iPos = iPosition;
    return iPosition;
}
