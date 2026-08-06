package com.esp32dashboard.lyrics

import kotlinx.coroutines.*
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import okhttp3.OkHttpClient
import okhttp3.Request
import java.util.concurrent.TimeUnit

data class LrcLine(val timeMs: Long, val text: String)

// LRCLIB requires a descriptive User-Agent header; default agents (e.g. okhttp/x)
// are rejected with HTTP 520. Without this, every request fails silently.
private const val USER_AGENT = "ESP32DashboardLyrics/1.0 (Android)"

class LyricsEngine(
    private val esp32Client: Esp32Client,
    private val onStatus: (String) -> Unit,
    private val onLyricLine: (String) -> Unit,
    private val onSongDetected: (String, String) -> Unit
) {
    private val httpClient = OkHttpClient.Builder()
        .connectTimeout(5, TimeUnit.SECONDS)
        .readTimeout(10, TimeUnit.SECONDS)
        .addInterceptor { chain ->
            chain.proceed(
                chain.request().newBuilder()
                    .header("User-Agent", USER_AGENT)
                    .build()
            )
        }
        .build()

    private val json = Json { ignoreUnknownKeys = true }
    private var syncJob: Job? = null
    private var scope: CoroutineScope? = null

    private var cachedLyrics: List<LrcLine> = emptyList()
    private var currentArtist = ""
    private var currentTitle = ""

    // Set when a LRCLIB request fails (HTTP code, or -1 for network error).
    // Lets the status message distinguish "request failed" from "not found".
    private var lastFetchHttpError = 0

    fun start(scope: CoroutineScope) {
        this.scope = scope
        onStatus("Listening for songs...")
    }

    fun stop() {
        syncJob?.cancel()
        syncJob = null
        scope = null
        cachedLyrics = emptyList()
        onStatus("Stopped")
    }

    fun onSongChanged(artist: String, title: String) {
        if (artist == currentArtist && title == currentTitle) return

        currentArtist = artist
        currentTitle = title
        onSongDetected(artist, title)
        onStatus("Song: $title - $artist")

        // Cancel any existing sync
        syncJob?.cancel()

        // Start new sync in the coroutine scope
        syncJob = scope?.launch(Dispatchers.IO) {
            fetchAndSync(artist, title)
        }
    }

    private suspend fun fetchAndSync(artist: String, title: String) {
        try {
            onStatus("Fetching lyrics...")
            esp32Client.sendNowPlaying(artist, title)

            val lyrics = fetchLrcLyrics(artist, title)
            if (lyrics.isEmpty()) {
                when {
                    lastFetchHttpError == -1 -> onStatus("Lyrics network error - check internet")
                    lastFetchHttpError != 0 -> onStatus("Lyrics request failed (HTTP ${lastFetchHttpError})")
                    else -> onStatus("No synced lyrics found for: $title")
                }
                return
            }

            cachedLyrics = lyrics
            onStatus("Syncing: $title (${lyrics.size} lines)")

            // Small delay before starting to let the user start the song
            delay(500)

            val startTime = System.currentTimeMillis()
            var lastIndex = -1

            while (isActive()) {
                val elapsedMs = System.currentTimeMillis() - startTime
                val currentIndex = findLineIndex(elapsedMs)

                if (currentIndex != lastIndex) {
                    lastIndex = currentIndex
                    val currentLine = lyrics[currentIndex]
                    val prevLine = if (currentIndex > 0) lyrics[currentIndex - 1] else null
                    val nextLine = if (currentIndex < lyrics.size - 1) lyrics[currentIndex + 1] else null

                    val totalDuration = lyrics.last().timeMs
                    val progress = if (totalDuration > 0) {
                        (elapsedMs * 100 / totalDuration).toInt().coerceIn(0, 100)
                    } else 0

                    onLyricLine(currentLine.text)

                    esp32Client.sendLine(
                        current = currentLine.text,
                        prev = prevLine?.text ?: "",
                        next = nextLine?.text ?: "",
                        progress = progress
                    )
                }

                delay(100) // Check every 100ms
            }
        } catch (e: CancellationException) {
            // Normal cancellation
        } catch (e: Exception) {
            onStatus("Error: ${e.message ?: "Unknown"}")
        }
    }

    private suspend fun fetchLrcLyrics(artist: String, title: String): List<LrcLine> {
        lastFetchHttpError = 0
        return try {
            // 1. Exact match by artist + track
            val exactUrl = "https://lrclib.net/api/get?" +
                    "artist_name=${artist.urlEncode()}&track_name=${title.urlEncode()}"
            val exactLines = fetchBody(exactUrl)?.let { extractSyncedLyrics(it) } ?: emptyList()
            if (exactLines.isNotEmpty()) return exactLines

            // 2. Search by artist + track
            val searchUrl = "https://lrclib.net/api/search?" +
                    "artist_name=${artist.urlEncode()}&track_name=${title.urlEncode()}"
            var results = fetchSearchResults(searchUrl)

            // 3. Broader search by track name only - artist names from notifications
            //    often differ from LRCLIB's (e.g. "Cheema Y & Gur Sidhu" vs "Cheema Y, Gur Sidhu")
            if (results.isEmpty()) {
                results = fetchSearchResults("https://lrclib.net/api/search?track_name=${title.urlEncode()}")
            }

            // 4. Prefer results whose artist matches the detected song, then the
            //    first one with synced lyrics. Search responses already include
            //    syncedLyrics, so no extra /api/get round-trips (LRCLIB rate-limits).
            val ranked = results.sortedByDescending { result ->
                val resultArtist = result.jsonObject["artistName"]?.jsonPrimitive?.content ?: ""
                if (artistsMatch(artist, resultArtist)) 1 else 0
            }
            for (result in ranked) {
                val raw = result.jsonObject["syncedLyrics"]?.jsonPrimitive?.content
                if (!raw.isNullOrBlank()) {
                    val lines = parseLrc(raw)
                    if (lines.isNotEmpty()) return lines
                }
            }
            emptyList()
        } catch (e: Exception) {
            emptyList()
        }
    }

    private fun artistsMatch(detected: String, lrcArtist: String): Boolean {
        fun norm(s: String): String = s.lowercase()
            .replace("&", ",")
            .replace("feat.", ",")
            .replace("featuring", ",")
            .replace(" ft ", " ")
            .replace(Regex("[^a-z0-9, ]"), "")
            .trim()
        val a = norm(detected)
        val b = norm(lrcArtist)
        return a.isNotEmpty() && b.isNotEmpty() && (a == b || a.contains(b) || b.contains(a))
    }

    private fun fetchBody(url: String): String? {
        return try {
            httpClient.newCall(Request.Builder().url(url).build())
                .execute().use { response ->
                    if (response.isSuccessful) {
                        // Reset any stale error so a later "no lyrics" message is accurate
                        lastFetchHttpError = 0
                        response.body?.string()
                    } else {
                        lastFetchHttpError = response.code
                        null
                    }
                }
        } catch (e: Exception) {
            lastFetchHttpError = -1
            null
        }
    }

    private fun fetchSearchResults(url: String): List<JsonElement> {
        val body = fetchBody(url) ?: return emptyList()
        return try {
            json.parseToJsonElement(body).jsonArray
        } catch (e: Exception) {
            emptyList()
        }
    }

    private fun extractSyncedLyrics(jsonBody: String): List<LrcLine> {
        return try {
            val obj = json.parseToJsonElement(jsonBody).jsonObject
            val raw = obj["syncedLyrics"]?.jsonPrimitive?.content ?: return emptyList()
            parseLrc(raw)
        } catch (e: Exception) {
            emptyList()
        }
    }

    private fun parseLrc(lrcText: String): List<LrcLine> {
        val lines = mutableListOf<LrcLine>()
        val regex = Regex("""\[(\d+):(\d+)\.(\d+)\](.*)""")

        for (line in lrcText.lines()) {
            val match = regex.find(line.trim()) ?: continue
            val minutes = match.groupValues[1].toLong()
            val seconds = match.groupValues[2].toLong()
            val centiseconds = match.groupValues[3].padEnd(2, '0').take(2).toLong()
            val text = match.groupValues[4].trim()

            if (text.isNotEmpty()) {
                val timeMs = minutes * 60_000 + seconds * 1000 + centiseconds * 10
                lines.add(LrcLine(timeMs, text))
            }
        }

        lines.sortBy { it.timeMs }
        return lines
    }

    private fun findLineIndex(elapsedMs: Long): Int {
        if (cachedLyrics.isEmpty()) return 0
        var index = 0
        for (i in cachedLyrics.indices) {
            if (cachedLyrics[i].timeMs <= elapsedMs) {
                index = i
            } else {
                break
            }
        }
        return index
    }

    private fun isActive(): Boolean {
        return scope?.isActive == true && syncJob?.isCancelled == false
    }

    private fun String.urlEncode(): String {
        return java.net.URLEncoder.encode(this, "UTF-8")
    }
}
