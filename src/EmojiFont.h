#pragma once

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// Built-in colour emoji sprites.
//
// The panel's text font is u8g2_font_unifont_h_utf8, which only covers the BMP
// and has no emoji glyphs at all (u8g2_GetGlyphWidth even takes a uint16_t, so
// codepoints above U+FFFF cannot be looked up). Rather than pull in a giant
// emoji font, the emoji we support ship as hand-built 16x16 colour sprites:
// each pixel is a 4-bit palette index (0 = transparent) and each sprite carries
// its own small RGB565 palette, so an emoji is a few dozen bytes of flash.
//
// Text is still text: Display walks UTF-8 codepoints, and wherever a codepoint
// has a sprite it blits the sprite (advancing one sprite cell) instead of
// asking the font for a glyph. Anything without a sprite keeps rendering
// exactly as before, so an unsupported emoji simply takes no space instead of
// printing garbage.
//
// Regenerate the table with tools/gen_emoji.py.
// ─────────────────────────────────────────────────────────────────────────────
namespace EmojiFont {

// Sprite box in pixels at text size 1 (the cell an emoji occupies in a line).
static constexpr uint8_t CELL = 16;

// Maximum palette entries one sprite may use. Deliberately NOT named
// MAX_COLORS: macros ignore namespaces, and AnimatedGIF.h already defines
// MAX_COLORS as 256, which broke every translation unit that pulled in both
// (MediaManager.cpp -> MediaManager.h -> Display.h -> here).
static constexpr uint8_t MAX_PALETTE = 8;

struct Sprite {
    uint32_t codepoint;              // canonical codepoint
    const char* name;                // short human label (web UI tooltip)
    uint8_t colors;                  // palette entries actually used (1..MAX_PALETTE)
    uint16_t palette[MAX_PALETTE];   // RGB565; pixel value N maps to palette[N-1]
    uint64_t rows[CELL];             // 16 rows x 4 bits/px (64 bits/row); leftmost pixel = bits 63..60
    const uint32_t* aliases;         // extra codepoints mapping here, 0-terminated (may be nullptr)
};

// Sprite for a codepoint, or nullptr when it is ordinary text.
const Sprite* find(uint32_t codepoint);

// Catalogue size / random access (used to build the web UI picker).
size_t count();
const Sprite* at(size_t index);

// Increment walkers of the catalogue.
const Sprite* begin();
bool next(const Sprite*& sprite);

// Encode a codepoint as UTF-8 into out (>= 4 bytes); returns the byte count.
uint8_t encodeUtf8(uint32_t codepoint, char* out);

// Number of UTF-8 bytes at text[i] (1..4). Never overruns a NUL.
uint8_t utf8Length(const char* text, size_t i);

// Decode the codepoint starting at text[i]; returns the byte count consumed
// (callers advance by that). Malformed input decodes as the raw byte.
uint8_t utf8Decode(const char* text, size_t i, uint32_t& codepoint);

// True for codepoints that only modify a preceding glyph (variation selectors,
// the zero-width joiner, skin-tone modifiers, the BOM-ish zero-width space).
// These are skipped silently so "❤️" (U+2764 U+FE0F) still resolves to a heart.
bool isFormatCodepoint(uint32_t codepoint);

}  // namespace EmojiFont
