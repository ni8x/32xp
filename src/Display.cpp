#include "Display.h"
#include "Config.h"
#include "Logger.h"

static const char* TAG = "Display";

Display::Display() : _bus(nullptr), _gfx(nullptr) {
}

Display::~Display() {
    if (_gfx) delete _gfx;
    if (_bus) delete _bus;
}

void Display::init(uint8_t brightness) {
    LOG_INFO(TAG, "Initializing Display...");
    
    pinMode(Config::TFT_BL, OUTPUT);
    digitalWrite(Config::TFT_BL, LOW);
    
    _bus = new Arduino_ESP32SPI(
        Config::TFT_DC,
        Config::TFT_CS,
        Config::TFT_SCLK,
        Config::TFT_MOSI,
        Config::TFT_MISO
    );
    
    _gfx = new Arduino_ST7789(
        _bus, 
        Config::TFT_RST, 
        Config::DEFAULT_ROTATION, 
        true, // IPS flag restored to true
        Config::DISPLAY_WIDTH, 
        Config::DISPLAY_HEIGHT,
        0, 0, 0, 0
    );

    if (!_gfx->begin()) {
        LOG_ERROR(TAG, "Display initialization failed!");
        return;
    }

    setBrightness(brightness);
    clear();
    
    LOG_INFO(TAG, "Display initialized successfully");
}

void Display::setBrightness(uint8_t brightness) {
    // Force backlight ON digitally to avoid any PWM/analogWrite issues
    digitalWrite(Config::TFT_BL, HIGH);
}

void Display::setRotation(uint8_t rotation) {
    if (_gfx) {
        _gfx->setRotation(rotation);
    }
}

void Display::clear() {
    fillScreen(BLACK);
}

void Display::fillScreen(uint16_t color) {
    if (_gfx) {
        _gfx->fillScreen(color);
    }
}

void Display::drawText(int16_t x, int16_t y, const char* text, uint16_t color, uint8_t size) {
    if (_gfx) {
        _gfx->setCursor(x, y);
        _gfx->setTextColor(color);
        _gfx->setTextSize(size);
        _gfx->print(text);
    }
}

void Display::drawCenteredText(const char* text, uint16_t color, uint8_t size) {
    if (_gfx) {
        int16_t x1, y1;
        uint16_t w, h;
        
        _gfx->setTextSize(size);
        // Calculate text bounds to center it properly
        _gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
        
        int16_t x = (_gfx->width() - w) / 2;
        int16_t y = (_gfx->height() - h) / 2;
        
        drawText(x, y, text, color, size);
    }
}

void Display::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    if (_gfx) {
        _gfx->drawLine(x0, y0, x1, y1, color);
    }
}

void Display::drawRectangle(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (_gfx) {
        _gfx->drawRect(x, y, w, h, color);
    }
}

void Display::drawFilledRectangle(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (_gfx) {
        _gfx->fillRect(x, y, w, h, color);
    }
}

void Display::drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
    if (_gfx) {
        _gfx->drawCircle(x, y, r, color);
    }
}

void Display::drawCenteredTextAt(int16_t y, const char* text, uint16_t color, uint8_t size) {
    if (!_gfx || !text || strlen(text) == 0) return;
    int16_t x1, y1;
    uint16_t w, h;
    _gfx->setTextSize(size);
    _gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    int16_t x = (_gfx->width() - w) / 2;
    drawText(x, y, text, color, size);
}

void Display::drawPixelHeart(int16_t x, int16_t y, uint16_t color, uint8_t scale) {
    if (!_gfx) return;

    // 8x7 Retro Pixel Heart Matrix
    static const uint8_t heartMap[7][8] = {
        {0, 1, 1, 0, 0, 1, 1, 0},
        {1, 1, 1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 1, 1, 1, 1},
        {0, 1, 1, 1, 1, 1, 1, 0},
        {0, 0, 1, 1, 1, 1, 0, 0},
        {0, 0, 0, 1, 1, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0, 0}
    };

    for (int r = 0; r < 7; r++) {
        for (int c = 0; c < 8; c++) {
            if (heartMap[r][c]) {
                _gfx->fillRect(x + (c * scale), y + (r * scale), scale, scale, color);
            }
        }
    }
}

uint16_t Display::hexToRGB565(const String& hexStr) {
    String hex = hexStr;
    if (hex.startsWith("#")) hex = hex.substring(1);
    long rgb = strtol(hex.c_str(), NULL, 16);
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
