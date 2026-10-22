package com.esp32dashboard.lyrics

import java.text.Normalizer

/**
 * Offline romanizer for Hindi (Devanagari) and Punjabi (Gurmukhi) lyrics.
 *
 * The dashboard font renders Latin text most reliably, so native-script lines
 * are converted to readable Hinglish before they are sent to the ESP32. The
 * transliteration is intentionally phonetic rather than scholarly: it aims for
 * the way the words are commonly typed in Latin script.
 *
 * Only Indic runs are touched. Latin text, digits, punctuation, emoji and
 * whitespace pass through unchanged, which keeps mixed lines like
 * "I love you ❤️" intact.
 */
object Romanizer {

    fun hasIndicScript(text: String): Boolean =
        text.any { isDevanagari(it.code) || isGurmukhi(it.code) }

    fun romanize(text: String): String {
        if (text.isEmpty()) return text
        val normalized = Normalizer.normalize(text, Normalizer.Form.NFC)
        if (!hasIndicScript(normalized)) return normalized

        val out = StringBuilder(normalized.length + normalized.length / 2)
        var i = 0
        while (i < normalized.length) {
            val cp = normalized.codePointAt(i)
            val script = scriptOf(cp)
            if (script == Script.NONE) {
                when {
                    cp == DANDA || cp == DOUBLE_DANDA -> out.append('.')
                    cp == ZWJ || cp == ZWNJ -> Unit
                    else -> out.appendCodePoint(cp)
                }
                i += Character.charCount(cp)
            } else {
                val start = i
                while (i < normalized.length) {
                    val next = normalized.codePointAt(i)
                    if (scriptOf(next) != script) break
                    i += Character.charCount(next)
                }
                out.append(romanizeRun(normalized.substring(start, i), script))
            }
        }
        return out.toString()
    }

    // ---------------------------------------------------------------- scripts

    private enum class Script { DEVANAGARI, GURMUKHI, NONE }

    private const val DANDA = 0x0964
    private const val DOUBLE_DANDA = 0x0965
    private const val ZWJ = 0x200D
    private const val ZWNJ = 0x200C

    private fun scriptOf(cp: Int): Script = when (cp) {
        in 0x0900..0x097F -> Script.DEVANAGARI
        in 0x0A00..0x0A7F -> Script.GURMUKHI
        else -> Script.NONE
    }

    private fun isDevanagari(cp: Int) = cp in 0x0900..0x097F
    private fun isGurmukhi(cp: Int) = cp in 0x0A00..0x0A7F

    private fun isVirama(ch: Char) = ch == '\u094D' || ch == '\u0A4D'
    private fun isNukta(ch: Char) = ch == '\u093C' || ch == '\u0A3C'
    private fun isAddak(ch: Char) = ch == '\u0A71'

    private fun isNasal(ch: Char) =
        ch == '\u0901' || ch == '\u0902' || ch == '\u0903' ||
            ch == '\u0A01' || ch == '\u0A02' || ch == '\u0A03' ||
            ch == '\u0A70'

    private fun isVisarga(ch: Char) = ch == '\u0903' || ch == '\u0A03'

    private fun isDanda(ch: Char) = ch == '\u0964' || ch == '\u0965'

    private fun isConsonant(script: Script, ch: Char): Boolean =
        when (script) {
            Script.DEVANAGARI -> ch in DEV_CONSONANTS
            Script.GURMUKHI -> ch in GUR_CONSONANTS
            Script.NONE -> false
        }

    private fun isIndependentVowel(script: Script, ch: Char): Boolean =
        when (script) {
            Script.DEVANAGARI -> ch in DEV_VOWELS
            Script.GURMUKHI -> ch in GUR_VOWELS
            Script.NONE -> false
        }

    private fun isMatra(script: Script, ch: Char): Boolean =
        when (script) {
            Script.DEVANAGARI -> ch in DEV_MATRAS
            Script.GURMUKHI -> ch in GUR_MATRAS
            Script.NONE -> false
        }

    private fun consonants(script: Script): Map<Char, String> =
        if (script == Script.GURMUKHI) GUR_CONSONANTS else DEV_CONSONANTS

    private fun matras(script: Script): Map<Char, String> =
        if (script == Script.GURMUKHI) GUR_MATRAS else DEV_MATRAS

    private fun vowels(script: Script): Map<Char, String> =
        if (script == Script.GURMUKHI) GUR_VOWELS else DEV_VOWELS

    private fun digitValue(ch: Char): Char? = when (ch) {
        in '\u0966'..'\u096F' -> ('0' + (ch.code - 0x0966))
        in '\u0A66'..'\u0A6F' -> ('0' + (ch.code - 0x0A66))
        else -> null
    }

    // ------------------------------------------------------------- core logic

    private fun romanizeRun(run: String, script: Script): String {
        OVERRIDES[run]?.let { return it }

        val vowels = vowels(script)
        val consonants = consonants(script)
        val matras = matras(script)
        val out = StringBuilder(run.length + run.length / 2)
        var i = 0
        var lastImplicitA = false
        var geminateNext = false

        while (i < run.length) {
            val ch = run[i]

            if (isDanda(ch)) {
                // A danda is a word boundary inside a Devanagari run: drop the
                // pending inherent "a" before writing the full stop.
                if (lastImplicitA && out.endsWith("a")) out.setLength(out.length - 1)
                out.append('.')
                lastImplicitA = false
                i++
                continue
            }
            val digit = digitValue(ch)
            if (digit != null) {
                out.append(digit)
                lastImplicitA = false
                i++
                continue
            }
            if (isVirama(ch) || isNukta(ch)) {
                i++
                continue
            }
            if (isAddak(ch)) {
                geminateNext = true
                lastImplicitA = false
                i++
                continue
            }
            if (isVisarga(ch)) {
                out.append('h')
                lastImplicitA = false
                i++
                continue
            }
            if (isNasal(ch)) {
                out.append(nasalFor(run, i, script, ch))
                lastImplicitA = false
                i++
                continue
            }
            if (isIndependentVowel(script, ch)) {
                out.append(vowels[ch] ?: ch.toString())
                lastImplicitA = false
                geminateNext = false
                i++
                continue
            }
            if (isConsonant(script, ch)) {
                var next = i + 1
                var key = ch.toString()
                if (next < run.length && isNukta(run[next])) {
                    key += run[next]
                    next++
                }
                var base = NUKTA_CONSONANTS[key] ?: consonants[ch] ?: ch.toString()
                if (geminateNext) {
                    base += base
                    geminateNext = false
                }

                when {
                    next < run.length && isVirama(run[next]) -> {
                        out.append(base)
                        lastImplicitA = false
                        i = next + 1
                    }
                    next < run.length && isMatra(script, run[next]) -> {
                        out.append(base)
                        out.append(matras[run[next]] ?: "")
                        lastImplicitA = false
                        i = next + 1
                    }
                    else -> {
                        out.append(base).append('a')
                        lastImplicitA = true
                        i = next
                    }
                }
                continue
            }

            // Unknown mark inside an Indic run: keep it rather than dropping it.
            out.append(ch)
            lastImplicitA = false
            i++
        }

        // Hindi/Punjabi words normally end in a consonant without the inherent
        // "a" being spoken: राम -> raam, not raama.
        if (lastImplicitA && out.endsWith("a")) {
            out.setLength(out.length - 1)
        }
        return out.toString()
    }

    private fun nasalFor(run: String, index: Int, script: Script, ch: Char): String {
        // Chandrabindu is always a simple nasal; anusvara/bindi/tippi assimilate
        // before a labial (संभव -> sambhav).
        if (ch == '\u0901' || ch == '\u0A01') return "n"
        var i = index + 1
        while (i < run.length && (run[i] == '\u200C' || run[i] == '\u200D')) i++
        if (i < run.length) {
            var key = run[i].toString()
            if (i + 1 < run.length && isNukta(run[i + 1])) key += run[i + 1]
            val base = NUKTA_CONSONANTS[key] ?: consonants(script)[run[i]]
            if (base != null && base.firstOrNull() in listOf('p', 'b', 'm')) return "m"
        }
        return "n"
    }

    // ---------------------------------------------------------------- tables

    private val DEV_VOWELS = mapOf(
        'अ' to "a", 'आ' to "aa", 'इ' to "i", 'ई' to "ee",
        'उ' to "u", 'ऊ' to "oo", 'ऋ' to "ri", 'ॠ' to "ri",
        'ए' to "e", 'ऐ' to "ai", 'ओ' to "o", 'औ' to "au",
        'ऍ' to "e", 'ऑ' to "o", 'ऎ' to "e", 'ऒ' to "o"
    )

    private val DEV_CONSONANTS = mapOf(
        'क' to "k", 'ख' to "kh", 'ग' to "g", 'घ' to "gh", 'ङ' to "ng",
        'च' to "ch", 'छ' to "chh", 'ज' to "j", 'झ' to "jh", 'ञ' to "n",
        'ट' to "t", 'ठ' to "th", 'ड' to "d", 'ढ' to "dh", 'ण' to "n",
        'त' to "t", 'थ' to "th", 'द' to "d", 'ध' to "dh", 'न' to "n",
        'प' to "p", 'फ' to "ph", 'ब' to "b", 'भ' to "bh", 'म' to "m",
        'य' to "y", 'र' to "r", 'ल' to "l", 'व' to "v",
        'श' to "sh", 'ष' to "sh", 'स' to "s", 'ह' to "h",
        'ळ' to "l", 'ऱ' to "r", 'ऴ' to "l", 'ऩ' to "n"
    )

    private val DEV_MATRAS = mapOf(
        'ा' to "aa", 'ि' to "i", 'ी' to "ee", 'ु' to "u", 'ू' to "oo",
        'ृ' to "ri", 'ॄ' to "ri", 'े' to "e", 'ै' to "ai",
        'ो' to "o", 'ौ' to "au", 'ॅ' to "e", 'ॉ' to "o",
        'ॆ' to "e", 'ॊ' to "o"
    )

    private val GUR_VOWELS = mapOf(
        'ਅ' to "a", 'ਆ' to "aa", 'ਇ' to "i", 'ਈ' to "ee",
        'ਉ' to "u", 'ਊ' to "oo", 'ਏ' to "e", 'ਐ' to "ai",
        'ਓ' to "o", 'ਔ' to "au"
    )

    private val GUR_CONSONANTS = mapOf(
        'ਕ' to "k", 'ਖ' to "kh", 'ਗ' to "g", 'ਘ' to "gh", 'ਙ' to "ng",
        'ਚ' to "ch", 'ਛ' to "chh", 'ਜ' to "j", 'ਝ' to "jh", 'ਞ' to "n",
        'ਟ' to "t", 'ਠ' to "th", 'ਡ' to "d", 'ਢ' to "dh", 'ਣ' to "n",
        'ਤ' to "t", 'ਥ' to "th", 'ਦ' to "d", 'ਧ' to "dh", 'ਨ' to "n",
        'ਪ' to "p", 'ਫ' to "ph", 'ਬ' to "b", 'ਭ' to "bh", 'ਮ' to "m",
        'ਯ' to "y", 'ਰ' to "r", 'ਲ' to "l", 'ਵ' to "v", 'ੜ' to "r",
        'ਸ਼' to "sh", 'ਸ' to "s", 'ਹ' to "h"
    )

    private val GUR_MATRAS = mapOf(
        'ਾ' to "a", 'ਿ' to "i", 'ੀ' to "i", 'ੁ' to "u", 'ੂ' to "oo",
        'ੇ' to "e", 'ੈ' to "ai", 'ੋ' to "o", 'ੌ' to "au"
    )

    // Consonants with an explicit nukta. Kept as two-codepoint keys because the
    // decomposed forms are what NFC produces in most lyric files.
    private val NUKTA_CONSONANTS = mapOf(
        "क़" to "q", "ख़" to "kh", "ग़" to "g", "ज़" to "z",
        "ड़" to "r", "ढ़" to "rh", "फ़" to "f", "य़" to "y",
        "ਨ਼" to "n", "ਰ਼" to "r", "ਲ਼" to "l",
        "ਖ਼" to "kh", "ਗ਼" to "g", "ਜ਼" to "z", "ਫ਼" to "f", "ਲ਼" to "l"
    )

    // Word-level spellings that the character rules cannot infer (schwa
    // deletion, irregular vowels, common Hinglish conventions). Applied before
    // the character pass, only for an exact Indic run.
    private val OVERRIDES = mapOf(
        "लड़का" to "ladka",
        "किताब" to "kitaab",
        "नहीं" to "nahin",
        "मैं" to "main",
        "ज़िंदगी" to "zindagi",
        "मोहब्बत" to "mohabbat",
        "क्यों" to "kyon",
        "तेरा" to "tera",
        "में" to "mein",
        "हूँ" to "hoon",
        "क्या" to "kya",
        "और" to "aur",
        "तुम" to "tum",
        "हम" to "ham",
        "वो" to "vo",
        "ये" to "ye",
        "कि" to "ki",
        "से" to "se",
        "को" to "ko",
        "पर" to "par",
        "था" to "tha",
        "थी" to "thi",
        "थे" to "the",
        "रहा" to "raha",
        "रही" to "rahi",
        "रहे" to "rahe",
        "करता" to "karta",
        "करती" to "karti",
        "करते" to "karte",
        "गुरबानी" to "gurbani",
        "ਗੁਰਬਾਣੀ" to "gurbani",
        "ਸੱਚ" to "sach",
        "ਨਹੀਂ" to "nahin",
        "ਮੈਂ" to "main",
        "ਤੂੰ" to "toon",
        "ਦਿਲ" to "dil"
    )
}
