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

    // Centered text with optional radiant glow (3-ring dim halo) and/or rainbow
    // hue cycling across glyphs. hueBase (0..255) animates the rainbow. When
    // outX/outW are given, the drawn line's bounding box is returned (the halo
    // is not included) so callers can erase exactly that region between frames.
    // glowDimOffset adds extra dimming to every halo ring (e.g. 1 = 25% instead
    // of 50% on the innermost ring) for busy screens where a strong glow blinds.
    // gradient applies an Instagram-style edge fade: each glyph is brightened
    // toward the centre of the line and dimmed toward its two ends (combinable
    // with rainbow, which supplies the per-glyph hue).
    void drawCenteredTextEffect(int16_t y, const char* text, uint16_t color, uint8_t size,
                                bool glow, bool rainbow, uint8_t hueBase,
                                int16_t* outX = nullptr, int16_t* outW = nullptr,
                                uint8_t glowDimOffset = 0, bool gradient = false);

    // HSV hue (0..255) -> RGB565 (rainbow wheel)
    static uint16_t hueToRGB565(uint8_t hue);

    // Linear brightness scale in RGB565 (factor 0 = black, 255 = unchanged).
    // Used by the gradient fade to shade glyphs without banding between the
    // coarse 50%-per-level dimColor() steps.
    static uint16_t scaleColor(uint16_t color, uint8_t factor);

    // UTF-8 aware text measurement & fitting (pixel-based, never splits a codepoint)
    uint16_t measureText(const char* text, uint8_t size = 1);
    String fitText(const String& text, uint8_t size, int16_t maxWidth);

    // Small pixel font (ASCII, default 7x13 - very readable) for timestamps /
    // status readouts. Switches the renderer font for the call and restores
    // the lyric unifont.
    void drawPixelText(int16_t x, int16_t y, const char* text, uint16_t color);
    uint16_t measurePixelText(const char* text);

    // Indic (Devanagari/Gurmukhi) shaped rendering: places matras/virama in
    // the correct positions instead of unifont's linear per-codepoint layout.
    void drawShapedText(int16_t x, int16_t y, const char* text, uint16_t color, uint8_t size);

    // Live dimensions (follow the current rotation)
    int16_t width();
    int16_t height();

    static uint16_t hexToRGB565(const String& hexStr);

    Arduino_GFX* getGFX() { return _gfx; }

private:
    struct ShapedLayoutItem {
        uint32_t codepoint;
        int16_t x; // pixel offset at text size 1
    };

    // Real horizontal advance (px, size 1) of one glyph in the u8g2 unifont.
    int16_t glyphAdvance(uint32_t codepoint);

    // Lay out shaped text into (codepoint, x) pairs in draw order.
    size_t layoutShapedText(const char* text, ShapedLayoutItem* out, size_t maxOut, int16_t* widthPx);

    // Shaped-text width in pixels for the given text size.
    uint16_t measureShapedText(const char* text, uint8_t size);

    // Draw one glyph (codepoint re-encoded to UTF-8) at a pixel position.
    void drawGlyph(int16_t x, int16_t y, uint32_t codepoint, uint16_t color, uint8_t size);

    // Per-glyph rainbow render for PLAIN (non-shaped) text; glyph i gets hue
    // hueBase + i*step. dimLevel>0 renders each glyph at ~50%^dimLevel (glow
    // halo). Shaped text is rendered inline in drawCenteredTextEffect which
    // precomputes the layout once per frame.
    void drawRainbowText(int16_t x, int16_t y, const char* text, uint8_t size, uint8_t hueBase, uint8_t dimLevel);

    // ~50%^level brightness of an RGB565 color (level 1 = half, 2 = quarter...).
    uint16_t dimColor(uint16_t color, uint8_t level = 1);

    // Per-glyph edge-fade render: glyph i is shaded by its horizontal distance
    // from the line centre (bright centre -> dim edges). rainbow supplies a
    // per-glyph hue; dimLevel applies the usual glow/step dimming on top.
    void drawGradientText(int16_t x, int16_t y, const char* text, uint8_t size,
                          uint16_t color, int16_t lineW, uint8_t edgeFactor,
                          uint8_t dimLevel, bool rainbow, uint8_t hueBase);

    // Brightness (0..255) for a glyph centred at gc within a line spanning
    // [lineX, lineX+lineW]: 255 at the centre falling to edgeFactor at the ends.
    static uint16_t gradientBrightness(int16_t gc, int16_t lineX, int16_t lineW,
                                       uint8_t edgeFactor);

    Arduino_DataBus* _bus;
    Arduino_GFX* _gfx;
};
