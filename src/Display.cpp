#include "Display.h"
#include "Config.h"
#include "Logger.h"
#include "ScriptShaping.h"
#include <U8g2lib.h> // u8g2_SetFont / u8g2_GetGlyphWidth for real glyph advances

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
    if (_gfx && text) {
        if (ScriptShaping::containsShapedScript(text)) {
            drawShapedText(x, y, text, color, size);
            return;
        }
        _gfx->setCursor(x, y);
        _gfx->setTextColor(color);
        _gfx->setTextSize(size);
        _gfx->print(text);
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
    int16_t x1, y1;
    uint16_t w, h;
    _gfx->setTextSize(size);
    _gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    int16_t x = (_gfx->width() - w) / 2;
    drawText(x, y, text, color, size);
}

void Display::drawShapedText(int16_t x, int16_t y, const char* text, uint16_t color, uint8_t size) {
    if (!_gfx || !text) return;
    ShapedLayoutItem items[64];
    int16_t w = 0;
    size_t n = layoutShapedText(text, items, 64, &w);
    if (n == 0) return;

    _gfx->setTextColor(color);
    _gfx->setTextSize(size);
    for (size_t i = 0; i < n; i++) {
        // Encode the codepoint back to a single UTF-8 glyph string.
        char buf[5];
        size_t bl = 0;
        uint32_t cp = items[i].codepoint;
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

        _gfx->setCursor(x + items[i].x * size, y);
        _gfx->print(buf);
    }
}

int16_t Display::glyphAdvance(uint32_t codepoint) {
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

uint16_t Display::measureText(const char* text, uint8_t size) {
    if (!_gfx || !text) return 0;
    if (ScriptShaping::containsShapedScript(text)) {
        return measureShapedText(text, size);
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

uint16_t Display::hexToRGB565(const String& hexStr) {
    String hex = hexStr;
    if (hex.startsWith("#")) hex = hex.substring(1);
    long rgb = strtol(hex.c_str(), NULL, 16);
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
