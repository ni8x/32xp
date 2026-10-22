package com.esp32dashboard.lyrics

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class RomanizerTest {

    @Test
    fun hindiWordsUseReadableHinglish() {
        assertEquals("raam", Romanizer.romanize("राम"))
        assertEquals("kamal", Romanizer.romanize("कमल"))
        assertEquals("dil", Romanizer.romanize("दिल"))
        assertEquals("kitaab", Romanizer.romanize("किताब"))
        assertEquals("ladka", Romanizer.romanize("लड़का"))
        assertEquals("hai", Romanizer.romanize("है"))
        assertEquals("nahin", Romanizer.romanize("नहीं"))
        assertEquals("main", Romanizer.romanize("मैं"))
        assertEquals("pyaar", Romanizer.romanize("प्यार"))
        assertEquals("zindagi", Romanizer.romanize("ज़िंदगी"))
        assertEquals("kyon", Romanizer.romanize("क्यों"))
        assertEquals("mohabbat", Romanizer.romanize("मोहब्बत"))
    }

    @Test
    fun punjabiWordsUseReadableHinglish() {
        assertEquals("panjab", Romanizer.romanize("ਪੰਜਾਬ"))
        assertEquals("gurbani", Romanizer.romanize("ਗੁਰਬਾਣੀ"))
        assertEquals("sach", Romanizer.romanize("ਸੱਚ"))
        assertEquals("nahin", Romanizer.romanize("ਨਹੀਂ"))
        assertEquals("main", Romanizer.romanize("ਮੈਂ"))
        assertEquals("toon", Romanizer.romanize("ਤੂੰ"))
        assertEquals("dil", Romanizer.romanize("ਦਿਲ"))
    }

    @Test
    fun mixedLinesKeepLatinTextAndEmoji() {
        assertEquals("I love you ❤️", Romanizer.romanize("I love you ❤️"))
        assertEquals("tera naam", Romanizer.romanize("तेरा नाम"))
        assertEquals("I love you mere yaar", Romanizer.romanize("I love you mere यार"))
        assertEquals("raam, kamal!", Romanizer.romanize("राम, कमल!"))
        assertEquals("raam.", Romanizer.romanize("राम।"))
        assertEquals("raam.", Romanizer.romanize("राम॥"))
    }

    @Test
    fun nasalsAndPunjabiAddakAreHandled() {
        assertEquals("sambhav", Romanizer.romanize("संभव"))
        assertEquals("rang", Romanizer.romanize("ਰੰਗ"))
        assertEquals("pakka", Romanizer.romanize("ਪੱਕਾ"))
    }

    @Test
    fun punctuationWhitespaceAndDigitsPassThrough() {
        assertEquals("", Romanizer.romanize(""))
        assertEquals("   ", Romanizer.romanize("   "))
        assertEquals("123", Romanizer.romanize("१२३"))
        assertEquals("123", Romanizer.romanize("੧੨੩"))
        assertEquals("...!?", Romanizer.romanize("...!?"))
        assertEquals("raam\tkamal", Romanizer.romanize("राम\tकमल"))
    }

    @Test
    fun detectsIndicScript() {
        assertTrue(Romanizer.hasIndicScript("राम"))
        assertTrue(Romanizer.hasIndicScript("ਪੰਜਾਬ"))
        assertTrue(Romanizer.hasIndicScript("hello दुनिया"))
        assertFalse(Romanizer.hasIndicScript("hello world"))
        assertFalse(Romanizer.hasIndicScript(""))
    }
}
