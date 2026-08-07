#include "ScriptShaping.h"

#include <string.h>

namespace ScriptShaping {

// ---------------------------------------------------------------------------
// Codepoint classification (Devanagari U+0900..U+097F, Gurmukhi U+0A00..U+0A7F)
// ---------------------------------------------------------------------------

static bool isDevanagari(uint32_t cp) { return cp >= 0x0900 && cp <= 0x097F; }
static bool isGurmukhi(uint32_t cp)   { return cp >= 0x0A00 && cp <= 0x0A7F; }

static bool isConsonant(uint32_t cp) {
    // Devanagari क..ह
    if (cp >= 0x0915 && cp <= 0x0939) return true;
    // Gurmukhi ਕ..ਹ plus ਖ਼ ਗ਼ ਜ਼ ੜ ਫ਼
    if (cp >= 0x0A15 && cp <= 0x0A39) return true;
    return (cp >= 0x0A59 && cp <= 0x0A5E);
}

static bool isIndependentVowel(uint32_t cp) {
    return (cp >= 0x0905 && cp <= 0x0914) || (cp >= 0x0A05 && cp <= 0x0A14);
}

// Vowel signs written BEFORE the consonant (ि े ै ो ौ ...)
static bool isPrefixMatra(uint32_t cp) {
    if (cp == 0x093F) return true;                  // ि
    if (cp >= 0x0945 && cp <= 0x094C) return true;  // ॅ े ै ो ौ ...
    if (cp == 0x0A3F || cp == 0x0A40) return true;  // ਿ ੀ
    if (cp == 0x0A47 || cp == 0x0A48) return true;  // ੇ ੈ
    if (cp == 0x0A4B || cp == 0x0A4C) return true;  // ੋ ੌ
    return false;
}

// Vowel signs written AFTER the consonant (ा ी ः)
static bool isAfterMatra(uint32_t cp) {
    if (cp == 0x093E || cp == 0x0940 || cp == 0x0903) return true; // ा ी ः
    if (cp == 0x0A3E || cp == 0x0A03) return true;                 // ਾ ਃ
    return false;
}

// Vowel signs written BELOW the consonant (ु ू ृ ॄ / ੁ ੂ)
static bool isBelowMatra(uint32_t cp) {
    return (cp >= 0x0941 && cp <= 0x0944) || (cp == 0x0A41 || cp == 0x0A42);
}

// Marks written ABOVE the previous glyph (ं ँ / ਂ ਁ ੰ)
static bool isUpperMark(uint32_t cp) {
    return (cp == 0x0901 || cp == 0x0902) ||
           (cp == 0x0A01 || cp == 0x0A02 || cp == 0x0A70);
}

static bool isVirama(uint32_t cp) { return cp == 0x094D || cp == 0x0A4D; } // ् / ੍
static bool isNukta(uint32_t cp)  { return cp == 0x093C || cp == 0x0A3C; }

static bool isModifier(uint32_t cp) {
    return isPrefixMatra(cp) || isAfterMatra(cp) || isBelowMatra(cp) ||
           isUpperMark(cp) || isVirama(cp) || isNukta(cp);
}

static bool isStarter(uint32_t cp) {
    return isConsonant(cp) || isIndependentVowel(cp);
}

// ---------------------------------------------------------------------------
// UTF-8 decoding (Arduino String stores raw bytes)
// ---------------------------------------------------------------------------

static size_t parseUtf8(const char* s, uint32_t* out, size_t max) {
    size_t n = 0;
    size_t i = 0;
    size_t len = strlen(s);
    while (i < len && n < max) {
        uint8_t b = (uint8_t)s[i];
        uint32_t cp;
        if (b >= 0xF0) {
            cp = b & 0x07; i++;
            for (int k = 0; k < 3 && i < len; k++, i++) cp = (cp << 6) | ((uint8_t)s[i] & 0x3F);
        } else if (b >= 0xE0) {
            cp = b & 0x0F; i++;
            for (int k = 0; k < 2 && i < len; k++, i++) cp = (cp << 6) | ((uint8_t)s[i] & 0x3F);
        } else if (b >= 0xC0) {
            cp = b & 0x1F; i++;
            if (i < len) { cp = (cp << 6) | ((uint8_t)s[i] & 0x3F); i++; }
        } else {
            cp = b; i++;
        }
        out[n++] = cp;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool containsShapedScript(const char* text) {
    if (!text) return false;
    uint32_t cps[64];
    size_t n = parseUtf8(text, cps, 64);
    for (size_t i = 0; i < n; i++) {
        if (isDevanagari(cps[i]) || isGurmukhi(cps[i])) return true;
    }
    return false;
}

size_t shape(const char* text, PlacedGlyph* out, size_t maxOut) {
    if (!text || !out || maxOut == 0) return 0;

    uint32_t cps[64];
    size_t n = parseUtf8(text, cps, 64);
    size_t placed = 0;

    auto place = [&](uint32_t cp, GlyphPlace p) {
        if (placed < maxOut) {
            out[placed].codepoint = cp;
            out[placed].place = p;
            placed++;
        }
    };

    size_t i = 0;
    while (i < n) {
        uint32_t cp = cps[i];
        if (isStarter(cp)) {
            // Collect the modifier run attached to this syllable.
            size_t j = i + 1;
            while (j < n && isModifier(cps[j])) j++;

            // Pre-base vowel signs go before the main glyph.
            for (size_t k = i + 1; k < j; k++)
                if (isPrefixMatra(cps[k])) place(cps[k], GLYPH_PRE);

            // Main glyph.
            place(cp, GLYPH_MAIN);

            // Below matras / nukta / virama share the main glyph.
            bool hasVirama = false;
            for (size_t k = i + 1; k < j; k++) {
                if (isBelowMatra(cps[k]) || isNukta(cps[k])) place(cps[k], GLYPH_SHARE);
                if (isVirama(cps[k])) hasVirama = true;
            }
            if (hasVirama) place(0x094D, GLYPH_SHARE);

            // Post-base vowel signs go after the main glyph.
            for (size_t k = i + 1; k < j; k++)
                if (isAfterMatra(cps[k])) place(cps[k], GLYPH_AFTER);

            // Above marks share the last cell of the syllable.
            for (size_t k = i + 1; k < j; k++)
                if (isUpperMark(cps[k])) place(cps[k], GLYPH_UPPER);

            i = j;
        } else {
            place(cp, GLYPH_MAIN);
            i++;
        }
    }
    return placed;
}

} // namespace ScriptShaping
