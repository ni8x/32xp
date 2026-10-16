#include "Display.h"
#include "Config.h"
#include "EmojiFont.h"
#include "Logger.h"
#include "ScriptShaping.h"
#include <U8g2lib.h> // u8g2_SetFont / u8g2_GetGlyphWidth for real glyph advances

static const char* TAG = "Display";

// Timestamp / status pixel font. Swap here to try other u8g2 fonts, e.g.:
//   u8g2_font_7x13_tr       - clean 7x13 terminal font, very readable (default)
//   u8g2_font_6x10_tr       - compact 6x10, still crisp
//   u8g2_font_5x7_tr        - tiny plain 5x7
//   u8g2_font_press_mel_tr  - Press Start (game console)
//   u8g2_font_pixelpoiiz_tr - arcade pixel (was the old default)
//   u8g2_font_bubble_tr     - bubble letters
// NOTE: like unifont, u8g2 fonts draw their ink ABOVE the given y (the y is
// the baseline). The 7x13 family has ~10px ascent / ~3px descent. App::drawLyricsScreen parks the
// timestamps at barY-4 and erases a 15px band (barY-15..barY-1), so any font
// up to ~13px tall with <=11px ascent fits; retune those two numbers if you
// pick a taller font.
static const uint8_t* const PIXEL_FONT = u8g2_font_7x13_tr;

// Instagram-style edge fade: glyphs are brightest at the line centre and dim to
// ~35% at the two ends.
static constexpr uint8_t GRADIENT_EDGE = 90;

// Where an emoji sprite hangs relative to the text baseline: unifont ink sits
// 14*size px above the baseline and ~2*size below it, and a sprite cell is
// 16*size tall, so this lines the emoji up with the glyphs beside it.
static constexpr int16_t EMOJI_ASCENT = 14;

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

    // Route text through the bundled u8g2 unifont so UTF-8 lyrics
    // (Hindi, Punjabi, Cyrillic, CJK, ...) render instead of gibberish.
    // Requires the U8g2 library (platformio.ini) for U8G2_FONT_SUPPORT.
    _gfx->setFont(u8g2_font_unifont_h_utf8);
    _gfx->setUTF8Print(true);
    // We position text ourselves (shaped glyphs, centered runs); no wrapping.
    _gfx->setTextWrap(false);

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
    if (!_gfx || !text) return;
    if (ScriptShaping::containsShapedScript(text)) {
        drawShapedText(x, y, text, color, size);
        return;
    }
    if (!containsEmoji(text)) {
        // Fast path: one bulk print for the whole string.
        _gfx->setCursor(x, y);
        _gfx->setTextColor(color);
        _gfx->setTextSize(size);
        _gfx->print(text);
        return;
    }

    // Mixed text: walk codepoints so emoji become sprites while everything else
    // keeps printing through the font.
    _gfx->setTextSize(size);
    int16_t cx = x;
    size_t i = 0;
    const size_t len = strlen(text);
    while (i < len) {
        uint32_t cp = 0;
        const uint8_t cpLen = EmojiFont::utf8Decode(text, i, cp);
        if (cpLen == 0) break;
        if (EmojiFont::isFormatCodepoint(cp)) { i += cpLen; continue; }

        if (const EmojiFont::Sprite* sprite = EmojiFont::find(cp)) {
            drawEmojiSprite(cx, y, sprite, size);
        } else {
            char g[5];
            const uint8_t gl = EmojiFont::encodeUtf8(cp, g);
            g[gl] = '\0';
            _gfx->setTextColor(color);
            _gfx->setCursor(cx, y);
            _gfx->print(g);
        }
        cx += codepointAdvance(cp) * (int16_t)size;
        i += cpLen;
    }
}

void Display::drawCenteredText(const char* text, uint16_t color, uint8_t size) {
    if (_gfx && text) {
        if (ScriptShaping::containsShapedScript(text)) {
            int16_t x = (_gfx->width() - (int16_t)measureShapedText(text, size)) / 2;
            int16_t y = (_gfx->height() - (int16_t)(16 * size)) / 2;
            drawShapedText(x, y, text, color, size);
            return;
        }
        if (containsEmoji(text)) {
            // Sprite cells are invisible to the font's ink bounds, so centre the
            // measured line instead.
            const int16_t x = ((int16_t)_gfx->width() - (int16_t)measureText(text, size)) / 2;
            const int16_t y = ((int16_t)_gfx->height() - (int16_t)(16 * size)) / 2;
            drawText(x, y, text, color, size);
            return;
        }
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
    if (ScriptShaping::containsShapedScript(text)) {
        int16_t x = (_gfx->width() - (int16_t)measureShapedText(text, size)) / 2;
        drawShapedText(x, y, text, color, size);
        return;
    }
    if (containsEmoji(text)) {
        // Sprite cells are invisible to the font's ink bounds, so centre the
        // measured line instead (keeps emoji runs level with plain text).
        const int16_t x = ((int16_t)_gfx->width() - (int16_t)measureText(text, size)) / 2;
        drawText(x, y, text, color, size);
        return;
    }
    int16_t x1, y1;
    uint16_t w, h;
    _gfx->setTextSize(size);
    _gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    // getTextBounds reports the ink box (x1 .. x1+w-1). Center THAT box, not
    // just its width: glyphs with a left bearing (narrow glyphs like '1' and
    // ':' in unifont) otherwise push the text off-center.
    int16_t x = (_gfx->width() - w) / 2 - x1;
    drawText(x, y, text, color, size);
}

void Display::drawShapedText(int16_t x, int16_t y, const char* text, uint16_t color, uint8_t size) {
    if (!_gfx || !text) return;
    ShapedLayoutItem items[64];
    int16_t w = 0;
    size_t n = layoutShapedText(text, items, 64, &w);
    if (n == 0) return;

    for (size_t i = 0; i < n; i++) {
        drawGlyph(x + items[i].x * size, y, items[i].codepoint, color, size);
    }
}

void Display::drawGlyph(int16_t x, int16_t y, uint32_t cp, uint16_t color, uint8_t size) {
    if (!_gfx) return;
    // Emoji have no font glyph: blit the sprite instead (its own colours win
    // over the caller's colour, which the layout walkers already faded).
    if (const EmojiFont::Sprite* sprite = EmojiFont::find(cp)) {
        drawEmojiSprite(x, y, sprite, size);
        return;
    }
    // Encode the codepoint back to a single UTF-8 glyph string.
    char buf[5];
    size_t bl = 0;
    if (cp < 0x80) {
        buf[bl++] = (char)cp;
    } else if (cp < 0x800) {
        buf[bl++] = (char)(0xC0 | (cp >> 6));
        buf[bl++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        buf[bl++] = (char)(0xE0 | (cp >> 12));
        buf[bl++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[bl++] = (char)(0x80 | (cp & 0x3F));
    } else {
        buf[bl++] = (char)(0xF0 | (cp >> 18));
        buf[bl++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[bl++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[bl++] = (char)(0x80 | (cp & 0x3F));
    }
    buf[bl] = '\0';

    _gfx->setTextColor(color);
    _gfx->setTextSize(size);
    _gfx->setCursor(x, y);
    _gfx->print(buf);
}

uint16_t Display::dimColor(uint16_t color, uint8_t level) {
    // Cheap ~50%^level brightness: shift RGB565 right each step, masking out
    // the bits shifted into each channel.
    while (level-- > 0) {
        color = (color >> 1) & 0x7BEF;
    }
    return color;
}

uint16_t Display::scaleColor(uint16_t color, uint8_t factor) {
    // Linear brightness scale in RGB565. Splitting the channels loses the low
    // bits, but at these resolutions the result is visually smooth.
    uint16_t r = (color >> 11) & 0x1F;
    uint16_t g = (color >> 5) & 0x3F;
    uint16_t b = color & 0x1F;
    r = (uint16_t)((r * factor) / 255);
    g = (uint16_t)((g * factor) / 255);
    b = (uint16_t)((b * factor) / 255);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

uint16_t Display::gradientBrightness(int16_t gc, int16_t lineX, int16_t lineW, uint8_t edgeFactor) {
    if (lineW <= 0) return 255;
    const int16_t half = lineW / 2;
    if (half == 0) return 255;
    int16_t dist = gc - (lineX + half);
    if (dist < 0) dist = -dist;
    if (dist > half) dist = half;
    return (uint16_t)(255 - (int32_t)(255 - edgeFactor) * dist / half);
}

uint16_t Display::hueToRGB565(uint8_t hue) {
    // Adafruit-style rainbow wheel: red -> yellow -> green -> cyan -> blue -> magenta.
    uint8_t r = 0, g = 0, b = 0;
    if (hue < 85) {            // red -> yellow
        r = 255 - hue * 3;
        g = hue * 3;
    } else if (hue < 170) {    // yellow -> green -> cyan
        hue -= 85;
        r = 0;
        g = 255 - hue * 3;
        b = hue * 3;
    } else {                   // cyan -> blue -> magenta
        hue -= 170;
        r = hue * 3;
        g = 0;
        b = 255 - hue * 3;
    }
    return RGB565(r, g, b);
}

void Display::drawRainbowText(int16_t x, int16_t y, const char* text, uint8_t size, uint8_t hueBase, uint8_t dimLevel) {
    if (!_gfx || !text) return;
    if (ScriptShaping::containsShapedScript(text)) {
        // Should not happen: shaped text is handled by drawCenteredTextEffect.
        drawShapedText(x, y, text, hueToRGB565(hueBase), size);
        return;
    }

    // Plain path: walk codepoints, drawing each with its own hue.
    _gfx->setTextSize(size);
    int16_t cx = x;
    size_t i = 0, gi = 0;
    const size_t len = strlen(text);
    while (i < len) {
        uint32_t cp = 0;
        const uint8_t cpLen = EmojiFont::utf8Decode(text, i, cp);
        if (cpLen == 0) break;
        // Variation selectors / ZWJ / skin tones must not take their own cell.
        if (EmojiFont::isFormatCodepoint(cp)) { i += cpLen; continue; }

        if (const EmojiFont::Sprite* sprite = EmojiFont::find(cp)) {
            // Emoji keep their own colours. The glow halo passes (dimLevel > 0)
            // skip them so the halo still reads as text.
            if (!dimLevel) drawEmojiSprite(cx, y, sprite, size);
            cx += codepointAdvance(cp) * (int16_t)size;
            i += cpLen;
            gi++;
            continue;
        }

        uint16_t c = hueToRGB565(hueBase + (uint8_t)(gi * 22));
        if (dimLevel) c = dimColor(c, dimLevel);
        // Arduino_GFX hides Print::write(buffer,len), so print a NUL-terminated
        // copy of this codepoint through the normal UTF-8 print path.
        char g[5];
        memcpy(g, text + i, cpLen);
        g[cpLen] = '\0';
        _gfx->setTextColor(c);
        _gfx->setCursor(cx, y);
        _gfx->print(g);
        cx += codepointAdvance(cp) * (int16_t)size;
        i += cpLen;
        gi++;
    }
}

void Display::drawGradientText(int16_t x, int16_t y, const char* text, uint8_t size,
                               uint16_t color, int16_t lineW, uint8_t edgeFactor,
                               uint8_t dimLevel, bool rainbow, uint8_t hueBase) {
    if (!_gfx || !text) return;
    if (ScriptShaping::containsShapedScript(text)) {
        // Should not happen: shaped text is handled by drawCenteredTextEffect.
        drawShapedText(x, y, text, scaleColor(color, edgeFactor), size);
        return;
    }

    // Plain path: walk codepoints, shading each glyph by its distance from the
    // line centre (bright core fading toward the ends).
    _gfx->setTextSize(size);
    int16_t cx = x;
    size_t i = 0, gi = 0;
    const size_t len = strlen(text);
    while (i < len) {
        uint32_t cp = 0;
        const uint8_t cpLen = EmojiFont::utf8Decode(text, i, cp);
        if (cpLen == 0) break;
        // Variation selectors / ZWJ / skin tones must not take their own cell.
        if (EmojiFont::isFormatCodepoint(cp)) { i += cpLen; continue; }

        if (const EmojiFont::Sprite* sprite = EmojiFont::find(cp)) {
            // Emoji keep their own colours but still fade toward the line ends;
            // halo passes (dimLevel > 0) skip them so the halo reads as text.
            if (!dimLevel) {
                const int16_t cell = (int16_t)EmojiFont::CELL * size;
                drawEmojiSprite(cx, y, sprite, size,
                                (uint8_t)gradientBrightness(cx + cell / 2, x, lineW, edgeFactor));
            }
            cx += codepointAdvance(cp) * (int16_t)size;
            i += cpLen;
            gi++;
            continue;
        }

        int16_t adv = glyphAdvance(cp) * size;
        int16_t gc = cx + adv / 2; // glyph centre, for the edge fade
        uint16_t c = rainbow ? hueToRGB565(hueBase + (uint8_t)(gi * 22)) : color;
        c = scaleColor(c, (uint8_t)gradientBrightness(gc, x, lineW, edgeFactor));
        if (dimLevel) c = dimColor(c, dimLevel);
        char g[5];
        memcpy(g, text + i, cpLen);
        g[cpLen] = '\0';
        _gfx->setTextColor(c);
        _gfx->setCursor(cx, y);
        _gfx->print(g);
        cx += adv;
        i += cpLen;
        gi++;
    }
}

void Display::drawCenteredTextEffect(int16_t y, const char* text, uint16_t color, uint8_t size,
                                     bool glow, bool rainbow, uint8_t hueBase,
                                     int16_t* outX, int16_t* outW, uint8_t glowDimOffset,
                                     bool gradient) {
    if (!_gfx || !text || strlen(text) == 0) {
        if (outX) *outX = 0;
        if (outW) *outW = 0;
        return;
    }

    int16_t w = 0;
    if (ScriptShaping::containsShapedScript(text)) {
        w = (int16_t)measureShapedText(text, size);
    } else {
        int16_t x1, y1;
        uint16_t ww, h;
        _gfx->setTextSize(size);
        _gfx->getTextBounds(text, 0, 0, &x1, &y1, &ww, &h);
        w = (int16_t)ww;
    }
    int16_t x = ((int16_t)_gfx->width() - w) / 2;
    if (outX) *outX = x;
    if (outW) *outW = w;

    renderTextEffect(x, y, text, color, size, glow, rainbow, gradient, hueBase, w, glowDimOffset);
}

void Display::renderTextEffect(int16_t x, int16_t y, const char* text, uint16_t color, uint8_t size,
                               bool glow, bool rainbow, bool gradient, uint8_t hueBase,
                               int16_t w, uint8_t glowDimOffset) {
    const bool shaped = ScriptShaping::containsShapedScript(text);

    // The shaped layout is computed ONCE per call and reused by every halo
    // pass; re-running the script shaping per pass would waste CPU on every
    // animated frame (25 passes for a 3-ring compass glow + main).
    ShapedLayoutItem items[64];
    size_t nItems = 0;
    if (shaped) {
        int16_t lw = 0;
        nItems = layoutShapedText(text, items, 64, &lw);
        if (nItems == 0) return;
    }

    // One full render pass of the text at a pixel offset with a dim level
    // (0 = full brightness). Rainbow colors per glyph; gradient fades the ends;
    // glow dims everything.
    auto renderPass = [&](int16_t ox, int16_t oy, uint8_t dimLevel) {
        int16_t px = x + ox, py = y + oy;
        if (shaped) {
            for (size_t i = 0; i < nItems; i++) {
                uint16_t c = rainbow ? hueToRGB565(hueBase + (uint8_t)(i * 22)) : color;
                if (gradient) {
                    int16_t gc = px + items[i].x * size +
                                 (glyphAdvance(items[i].codepoint) * size) / 2;
                    c = scaleColor(c, (uint8_t)gradientBrightness(gc, x, w, GRADIENT_EDGE));
                }
                if (dimLevel) c = dimColor(c, dimLevel);
                drawGlyph(px + items[i].x * size, py, items[i].codepoint, c, size);
            }
        } else if (gradient) {
            drawGradientText(px, py, text, size, color, w, GRADIENT_EDGE, dimLevel, rainbow, hueBase);
        } else if (rainbow) {
            drawRainbowText(px, py, text, size, hueBase, dimLevel);
        } else {
            drawText(px, py, text, dimLevel ? dimColor(color, dimLevel) : color, size);
        }
    };

    if (glow) {
        // Radiant halo: three rings at 1..GLOW_RADIUS px (text size 1) using the
        // 8 compass offsets per ring (N/E/S/W + diagonals), with brightness
        // falling off 50% per ring outward. Compass rings keep the glow round
        // and soft instead of a blocky square outline; the extra ring gives it
        // reach without more passes than the old 2-ring square halo.
        static constexpr int16_t GLOW_RADIUS = 3; // px at text size 1 - tune me
        static const int8_t OFF[8][2] = {{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}};
        for (int16_t r = GLOW_RADIUS; r >= 1; r--) {
            uint8_t dimLevel = (uint8_t)(r + glowDimOffset); // ring r dimmed r (or more) times
            for (int k = 0; k < 8; k++) {
                renderPass(OFF[k][0] * r * size, OFF[k][1] * r * size, dimLevel);
            }
        }
    }
    renderPass(0, 0, 0);
}

int16_t Display::codepointAdvance(uint32_t codepoint) {
    return EmojiFont::find(codepoint) ? (int16_t)EmojiFont::CELL : glyphAdvance(codepoint);
}

int16_t Display::glyphAdvance(uint32_t codepoint) {
    // Emoji occupy exactly one sprite cell (the font has no glyph for them, and
    // u8g2_GetGlyphWidth takes a uint16_t that would truncate the codepoint).
    if (EmojiFont::find(codepoint)) return EmojiFont::CELL;
    // The font's REAL horizontal advance (8px Latin, 16px Devanagari/Gurmukhi),
    // via the same u8g2 decoder the renderer uses for printing.
    static u8g2_t u8g2;
    static bool init = false;
    if (!init) {
        memset(&u8g2, 0, sizeof(u8g2));
        u8g2_SetFont(&u8g2, u8g2_font_unifont_h_utf8);
        init = true;
    }
    int8_t w = u8g2_GetGlyphWidth(&u8g2, (uint16_t)codepoint);
    return (w > 0) ? w : 8; // missing glyphs render nothing; keep a sane advance
}

size_t Display::layoutShapedText(const char* text, ShapedLayoutItem* out, size_t maxOut, int16_t* widthPx) {
    if (!text || !out || maxOut == 0) {
        if (widthPx) *widthPx = 0;
        return 0;
    }

    // unifont draws matra ink at the EDGE of its own 16px cell (i-matra stroke
    // at x=0..1, aa/ee bars at x=12..15). Placing the matra cell a full 16px
    // from the consonant leaves the matra floating ~13px away, so pull pre-base
    // matras right and post-base matras left so the ink touches the consonant.
    // (Below matras/virama share the consonant cell and need no adjustment.)
    static constexpr int16_t MATRA_OVERLAP = 14; // px at text size 1 - tune me

    ScriptShaping::PlacedGlyph glyphs[64];
    size_t n = ScriptShaping::shape(text, glyphs, 64);
    size_t written = 0;

    auto emit = [&](uint32_t cp, int16_t x) {
        if (written < maxOut) {
            out[written].codepoint = cp;
            out[written].x = x;
            written++;
        }
    };

    int16_t syllStart = 0;  // x where the current syllable begins
    bool haveSyllable = false;
    int16_t consX = 0;      // x of the current main consonant
    int16_t afterX = 0;     // running x past the consonant + post-base matras
    int16_t lastCellX = 0;  // x of the last placed cell (for above marks)
    int16_t preSum = 0;     // total width of stashed pre-base matras
    uint32_t preCps[4];
    int16_t preWs[4];
    size_t nPre = 0;

    for (size_t i = 0; i < n; i++) {
        uint32_t cp = glyphs[i].codepoint;
        ScriptShaping::GlyphPlace p = glyphs[i].place;

        if (p == ScriptShaping::GLYPH_PRE) {
            // Stash; drawn right before the upcoming main consonant.
            if (nPre < 4) {
                preCps[nPre] = cp;
                preWs[nPre] = glyphAdvance(cp);
                preSum += preWs[nPre];
                nPre++;
            }
            continue;
        }

        if (p == ScriptShaping::GLYPH_MAIN) {
            // A new syllable starts where the previous one ended.
            if (haveSyllable) syllStart = afterX;
            haveSyllable = true;

            consX = syllStart + preSum;
            int16_t mainW = glyphAdvance(cp);
            afterX = consX + mainW;
            lastCellX = consX;

            // Draw stashed pre-base matras, right-aligned just before the
            // consonant, pulled right so their ink touches it.
            int16_t px = consX;
            for (int k = (int)nPre - 1; k >= 0; k--) {
                px -= preWs[k];
                emit(preCps[k], px + MATRA_OVERLAP);
            }
            nPre = 0;
            preSum = 0;

            emit(cp, consX);
            continue;
        }

        // Trailing glyphs of the current syllable.
        if (p == ScriptShaping::GLYPH_SHARE) {
            emit(cp, consX);
        } else if (p == ScriptShaping::GLYPH_AFTER) {
            // Pulled left so the matra bar (right edge of its cell) touches
            // the consonant instead of floating a cell-width away.
            emit(cp, afterX - MATRA_OVERLAP);
            lastCellX = afterX - MATRA_OVERLAP;
            afterX += glyphAdvance(cp);
        } else { // GLYPH_UPPER
            emit(cp, lastCellX);
        }
    }

    if (widthPx) *widthPx = afterX;
    return written;
}

uint16_t Display::measureShapedText(const char* text, uint8_t size) {
    ShapedLayoutItem items[64];
    int16_t w = 0;
    layoutShapedText(text, items, 64, &w);
    return (uint16_t)w * size;
}

void Display::drawPixelText(int16_t x, int16_t y, const char* text, uint16_t color, uint8_t size) {
    if (!_gfx || !text) return;
    _gfx->setFont(PIXEL_FONT); // retro pixel font from the U8g2 lib
    // Paint a black background per glyph cell in the same SPI pass: this erases
    // the previous timestamp in place (no separate black flash -> no blink)
    // and is invisible on the black screen otherwise.
    _gfx->setTextColor(color, BLACK);
    _gfx->setTextSize(size);
    _gfx->setCursor(x, y);
    _gfx->print(text);
    _gfx->setFont(u8g2_font_unifont_h_utf8); // restore the lyric font
}

uint16_t Display::measurePixelText(const char* text, uint8_t size) {
    if (!_gfx || !text) return 0;
    _gfx->setFont(PIXEL_FONT);
    _gfx->setTextSize(size);
    int16_t x1, y1;
    uint16_t w, h;
    _gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    _gfx->setFont(u8g2_font_unifont_h_utf8); // restore the lyric font
    return w;
}

uint16_t Display::measureText(const char* text, uint8_t size) {
    if (!_gfx || !text) return 0;
    if (ScriptShaping::containsShapedScript(text)) {
        return measureShapedText(text, size);
    }
    if (containsEmoji(text)) {
        // Sum the advances: the font's ink bounds ignore the sprite cells, so
        // centred emoji lines would otherwise sit off-centre.
        int16_t w = 0;
        size_t i = 0;
        const size_t len = strlen(text);
        while (i < len) {
            uint32_t cp = 0;
            const uint8_t cpLen = EmojiFont::utf8Decode(text, i, cp);
            if (cpLen == 0) break;
            if (!EmojiFont::isFormatCodepoint(cp)) w += codepointAdvance(cp);
            i += cpLen;
        }
        return (uint16_t)(w * (int16_t)size);
    }
    int16_t x1, y1;
    uint16_t w, h;
    _gfx->setTextSize(size);
    // getTextBounds is UTF-8 aware while _enableUTF8Print is on, so this
    // returns real pixel width for multi-byte scripts too.
    _gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return w;
}

String Display::fitText(const String& text, uint8_t size, int16_t maxWidth) {
    if (text.isEmpty() || maxWidth <= 0) return text;
    if ((int16_t)measureText(text.c_str(), size) <= maxWidth) return text;

    const int16_t suffixW = (int16_t)measureText("..", size);

    if (ScriptShaping::containsShapedScript(text.c_str())) {
        // Syllable-safe truncation: only cut between syllables, so a
        // consonant never loses its matra/virama at the end of the line.
        bool endFlag[64] = { false };
        {
            ScriptShaping::PlacedGlyph glyphs[64];
            size_t n = ScriptShaping::shape(text.c_str(), glyphs, 64);
            for (size_t j = 0; j < n; j++) {
                bool isEnd = (j == n - 1);
                if (!isEnd && j + 1 < n) {
                    ScriptShaping::GlyphPlace next = glyphs[j + 1].place;
                    isEnd = (next == ScriptShaping::GLYPH_PRE || next == ScriptShaping::GLYPH_MAIN);
                }
                endFlag[j] = isEnd;
            }
        }

        String acc;   // committed whole syllables
        String syll;  // current, uncommitted syllable
        size_t i = 0, cpIdx = 0;
        while (i < text.length() && cpIdx < 64) {
            uint8_t b = (uint8_t)text[i];
            size_t cpLen = 1;
            if (b >= 0xF0) cpLen = 4;
            else if (b >= 0xE0) cpLen = 3;
            else if (b >= 0xC0) cpLen = 2;

            syll += text.substring(i, i + cpLen);
            if (endFlag[cpIdx]) {
                if ((int16_t)measureText((acc + syll).c_str(), size) + suffixW <= maxWidth) {
                    acc += syll;
                } else {
                    break;
                }
                syll = "";
            }
            i += cpLen;
            cpIdx++;
        }
        if (!acc.isEmpty()) return acc + "..";

        // Even the first syllable won't fit: keep just the first glyph.
        uint8_t b = (uint8_t)text[0];
        size_t cpLen = (b >= 0xF0) ? 4 : (b >= 0xE0) ? 3 : (b >= 0xC0) ? 2 : 1;
        return text.substring(0, min(cpLen, text.length()));
    }

    // Plain codepoint walk (never cuts a multi-byte glyph in half).
    String acc;
    size_t i = 0;
    while (i < text.length()) {
        uint8_t b = (uint8_t)text[i];
        size_t cpLen = 1;
        if (b >= 0xF0) cpLen = 4;
        else if (b >= 0xE0) cpLen = 3;
        else if (b >= 0xC0) cpLen = 2;

        String candidate = acc + text.substring(i, i + cpLen);
        if ((int16_t)measureText(candidate.c_str(), size) + suffixW > maxWidth) break;
        acc = candidate;
        i += cpLen;
    }

    if (acc.isEmpty()) {
        // Even one glyph plus ".." won't fit: keep just the first glyph.
        uint8_t b = (uint8_t)text[0];
        size_t cpLen = (b >= 0xF0) ? 4 : (b >= 0xE0) ? 3 : (b >= 0xC0) ? 2 : 1;
        return text.substring(0, min(cpLen, text.length()));
    }
    return acc + "..";
}

int16_t Display::width() {
    return _gfx ? _gfx->width() : Config::DISPLAY_WIDTH;
}

int16_t Display::height() {
    return _gfx ? _gfx->height() : Config::DISPLAY_HEIGHT;
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

bool Display::containsEmoji(const char* text) {
    if (!text) return false;
    // Every sprite is U+2600 or above, so its UTF-8 starts with 0xE2 (U+2600..
    // U+2FFF) or 0xF0 (U+1F000+). Latin and Indic text (0xE0/0xE1 leads) and
    // CJK (0xE4..) short-circuit here and keep the bulk-print fast path.
    for (const char* p = text; *p; p++) {
        const uint8_t b = (uint8_t)*p;
        if (b == 0xF0 || b == 0xE2) return true;
    }
    return false;
}

void Display::drawEmoji(int16_t x, int16_t y, uint32_t codepoint, uint8_t size) {
    drawEmojiSprite(x, y, EmojiFont::find(codepoint), size);
}

void Display::drawEmojiCentered(int16_t cx, int16_t cy, uint32_t codepoint, uint8_t size) {
    const EmojiFont::Sprite* sprite = EmojiFont::find(codepoint);
    if (!sprite || size == 0) return;
    const int16_t half = (int16_t)(EmojiFont::CELL / 2) * size;
    // drawEmojiSprite takes a text baseline, so shift up by the sprite ascent.
    drawEmojiSprite(cx - half, cy + (int16_t)(EMOJI_ASCENT - EmojiFont::CELL / 2) * size,
                    sprite, size);
}

bool Display::isEmoji(uint32_t codepoint) {
    return EmojiFont::find(codepoint) != nullptr;
}

void Display::drawEmojiSprite(int16_t x, int16_t baselineY, const EmojiFont::Sprite* sprite,
                              uint8_t size, uint8_t factor) {
    if (!_gfx || !sprite || size == 0) return;

    const int16_t top = baselineY - (int16_t)(EMOJI_ASCENT * size);
    for (uint8_t r = 0; r < EmojiFont::CELL; r++) {
        const uint64_t row = sprite->rows[r];
        int16_t c = 0;
        while (c < EmojiFont::CELL) {
            // Leftmost pixel of the row is the top nibble.
            const uint8_t v = (uint8_t)((row >> (4 * (EmojiFont::CELL - 1 - c))) & 0xF);
            if (v == 0 || v > sprite->colors) { c++; continue; }
            // Merge the horizontal run: an emoji costs ~30 fills, not 256.
            uint8_t run = 1;
            while (c + run < EmojiFont::CELL) {
                const uint8_t nv = (uint8_t)((row >> (4 * (EmojiFont::CELL - 1 - c - run))) & 0xF);
                if (nv != v) break;
                run++;
            }
            uint16_t color = sprite->palette[v - 1];
            if (factor != 255) color = scaleColor(color, factor);
            _gfx->fillRect(x + c * size, top + r * size, run * size, size, color);
            c += run;
        }
    }
}

uint16_t Display::hexToRGB565(const char* hexStr) {
    if (!hexStr) return 0x0000;
    if (*hexStr == '#') hexStr++;
    // Parse up to six hex digits by hand: no String, no allocation.
    uint32_t rgb = 0;
    for (uint8_t i = 0; i < 6; i++) {
        const char c = hexStr[i];
        uint8_t v;
        if (c >= '0' && c <= '9') v = (uint8_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v = (uint8_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = (uint8_t)(c - 'A' + 10);
        else break;
        rgb = (rgb << 4) | v;
    }
    const uint8_t r = (rgb >> 16) & 0xFF;
    const uint8_t g = (rgb >> 8) & 0xFF;
    const uint8_t b = rgb & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

uint16_t Display::hexToRGB565(const String& hexStr) {
    return hexToRGB565(hexStr.c_str());
}
