#pragma once

#include <Arduino.h>

// Simple Indic script shaping for the u8g2 unifont_h font.
//
// unifont renders every codepoint in its own fixed-width cell, so Devanagari
// and Gurmukhi text comes out with matras in the wrong position (i-matra after
// the consonant, below-matra in its own cell, virama detached). This module
// classifies the glyphs of each syllable so the Display layer can place them
// where a reader expects:
//   - pre-base vowel signs (ि े ੈ ੋ ੌ ...) -> just BEFORE the consonant
//   - below vowel signs (ु ੂ ੁ ...), nukta and virama (्) -> over the consonant
//   - post-base vowel signs (ा ी ...) -> after the consonant
//   - above marks (ं ँ ੰ ...) -> over the last cell of the syllable
// Conjuncts (क् + क -> क्क) are NOT ligated; the halant is drawn under the
// first consonant, which stays readable.
//
// Pixel positions are computed by the Display layer with the font's real glyph
// advances (u8g2_GetGlyphWidth), because unifont_h cells are 16px wide for
// Devanagari/Gurmukhi glyphs but 8px for Latin.

namespace ScriptShaping {

// Where a glyph must be placed within its syllable.
enum GlyphPlace : uint8_t {
    GLYPH_MAIN = 0, // consonant, independent vowel, or standalone char (own cell)
    GLYPH_PRE,      // pre-base vowel sign: drawn just before the main glyph
    GLYPH_SHARE,    // below matra / nukta / virama: drawn over the main glyph
    GLYPH_AFTER,    // post-base vowel sign: drawn in the cell after the main glyph
    GLYPH_UPPER     // above mark: drawn over the last cell of the syllable
};

struct PlacedGlyph {
    uint32_t codepoint;
    GlyphPlace place;
};

// True if the text contains any Devanagari (Hindi) or Gurmukhi (Punjabi)
// characters that should go through shaped rendering.
bool containsShapedScript(const char* text);

// Classify `text` into placed glyphs in draw order, grouped per syllable as
// [PRE*][MAIN][SHARE*][AFTER*][UPPER*]. Returns the number written to `out`.
size_t shape(const char* text, PlacedGlyph* out, size_t maxOut);

} // namespace ScriptShaping
