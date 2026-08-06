#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

class Display {
public:
    Display();
    ~Display();

    void init(uint8_t brightness);
    
    void setBrightness(uint8_t brightness);
    void setRotation(uint8_t rotation);
    void clear();
    void fillScreen(uint16_t color);
    
    void drawText(int16_t x, int16_t y, const char* text, uint16_t color, uint8_t size = 1);
    void drawCenteredText(const char* text, uint16_t color, uint8_t size = 1);
    
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
    void drawRectangle(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawFilledRectangle(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color);
    void drawPixelHeart(int16_t x, int16_t y, uint16_t color, uint8_t scale = 4);
    void drawCenteredTextAt(int16_t y, const char* text, uint16_t color, uint8_t size = 1);

    static uint16_t hexToRGB565(const String& hexStr);

    Arduino_GFX* getGFX() { return _gfx; }

private:
    Arduino_DataBus* _bus;
    Arduino_GFX* _gfx;
};
