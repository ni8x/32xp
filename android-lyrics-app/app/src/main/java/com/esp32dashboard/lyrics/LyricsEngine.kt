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

data class LrcLine(val timeMs: Long, val text: String, val romanized: String)

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

    /** Native-script lines are converted to Hinglish before they leave the app. */
    @Volatile
    var romanizeEnabled: Boolean = true
        private set

    // Bumped when the user flips the switch. The sync loop watches it and
    // resends the current line immediately instead of waiting for the next
    // lyric or the 10-second position re-anchor.
    @Volatile
    private var romanizeEpoch = 0

    fun setRomanizeEnabled(enabled: Boolean) {
        if (romanizeEnabled == enabled) return
        romanizeEnabled = enabled
        romanizeEpoch++
    }

    private fun displayText(line: LrcLine): String =
        if (romanizeEnabled) line.romanized.ifBlank { line.text } else line.text

    // Set when a LRCLIB request fails (HTTP code, or -1 for network error).
    // Lets the status message distinguish "request failed" from "not found".
    private var lastFetchHttpError = 0

    // Consecutive failed POSTs to the ESP32. "Syncing" used to be shown even
    // when every request failed (wrong address, board offline), so the phone
    // and the device both looked connected while nothing was arriving.
    private var sendFailures = 0

    private fun noteSendFailed() {
        if (sendFailures < 3) sendFailures++
        if (sendFailures == 3) {
            onStatus("ESP32 not responding at ${esp32Client.address} - check the IP")
        }
    }

    private fun noteSendOk(recoveredStatus: String?) {
        if (sendFailures >= 3 && recoveredStatus != null) onStatus(recoveredStatus)
        sendFailures = 0
    }

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
            if (esp32Client.sendNowPlaying(artist, title).isFailure) noteSendFailed()

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

            // Sync to the player's REAL position when available (via the media
            // session), so buffering/queued songs, long intros, and seeks all
            // stay correct. Fall back to a local clock that starts when playback
            // begins; players with no state at all fall back to detection time.
            val detectedAt = System.currentTimeMillis()
            var fallbackStart = -1L
            var lastIndex = -1
            var lastRomanizeEpoch = romanizeEpoch
            var lastPosSync = 0L
            var waitingShown = false
            var waitingSince = 0L
            var gaveUpWaiting = false

            while (isActive()) {
                val now = System.currentTimeMillis()
                var elapsedMs = MusicNotificationListener.currentPlaybackPosition()

                if (elapsedMs == null) {
                    // No position source: run on a local clock.
                    if (fallbackStart < 0) {
                        if (!MusicNotificationListener.playbackSignaled()) {
                            fallbackStart = detectedAt // no state at all: old behavior
                        } else if (MusicNotificationListener.isPlaying) {
                            fallbackStart = now
                        } else {
                            if (!waitingShown) {
                                onStatus("Waiting for playback...")
                                waitingShown = true
                                waitingSince = now
                            }
                            // Grace period: if the player never reports playback
                            // (or misreports paused), start anyway after 5s.
                            if (now - waitingSince < 5000) {
                                delay(250)
                                continue
                            }
                            fallbackStart = detectedAt
                            gaveUpWaiting = true
                        }
                    }
                    // Playback may have started after we gave up waiting: re-anchor
                    // the clock so the wait time isn't included (lyrics would run
                    // early with no position source to correct them).
                    if (gaveUpWaiting && MusicNotificationListener.isPlaying) {
                        fallbackStart = now
                        gaveUpWaiting = false
                    }
                    elapsedMs = System.currentTimeMillis() - fallbackStart
                } else if (elapsedMs <= 0 && !MusicNotificationListener.isPlaying) {
                    // Song known but not playing yet (buffering / queued): hold
                    // the screen instead of showing the first line early.
                    if (!waitingShown) {
                        onStatus("Waiting for playback...")
                        waitingShown = true
                        waitingSince = now
                    }
                    // Grace period: never hold the screen forever for players
                    // that don't report playback state reliably.
                    if (now - waitingSince < 5000) {
                        delay(250)
                        continue
                    }
                }

                if (waitingShown) {
                    waitingShown = false
                    waitingSince = 0L
                    onStatus("Syncing: $title (${lyrics.size} lines)")
                }

                val currentIndex = findLineIndex(elapsedMs)
                val totalDuration = lyrics.last().timeMs
                val progress = if (totalDuration > 0) {
                    (elapsedMs * 100 / totalDuration).toInt().coerceIn(0, 100)
                } else 0
                // A line runs until the next one starts (the last until the track
                // ends). Sent in ms so the panel can report the line's progress
                // against the song clock instead of guessing.
                fun lineEndMs(index: Int): Long =
                    if (index < lyrics.size - 1) lyrics[index + 1].timeMs else totalDuration

                // Toggle changed: resend this line in the newly selected script.
                if (romanizeEpoch != lastRomanizeEpoch) {
                    lastRomanizeEpoch = romanizeEpoch
                    lastIndex = -1
                }

                if (currentIndex != lastIndex) {
                    lastIndex = currentIndex
                    lastPosSync = now
                    val currentLine = lyrics[currentIndex]
                    val prevLine = if (currentIndex > 0) lyrics[currentIndex - 1] else null
                    val nextLine = if (currentIndex < lyrics.size - 1) lyrics[currentIndex + 1] else null

                    onLyricLine(displayText(currentLine))

                    val sent = esp32Client.sendLine(
                        current = displayText(currentLine),
                        prev = prevLine?.let(::displayText) ?: "",
                        next = nextLine?.let(::displayText) ?: "",
                        progress = progress,
                        posSec = (elapsedMs / 1000).toInt(),
                        durSec = (totalDuration / 1000).toInt(),
                        posMs = elapsedMs.toInt(),
                        lineStartMs = currentLine.timeMs.toInt(),
                        lineEndMs = lineEndMs(currentIndex).toInt()
                    )
                    if (sent.isFailure) noteSendFailed()
                    else noteSendOk("Syncing: $title (${lyrics.size} lines)")
                } else if (now - lastPosSync >= 10_000 && lastIndex >= 0) {
                    // Periodic position re-anchor: the ESP32 ticks its own
                    // elapsed clock between updates, so during pauses or long
                    // held lines this keeps the on-screen timestamp accurate.
                    lastPosSync = now
                    val currentLine = lyrics[lastIndex]
                    val sent = esp32Client.sendLine(
                        current = displayText(currentLine),
                        progress = progress,
                        posSec = (elapsedMs / 1000).toInt(),
                        durSec = (totalDuration / 1000).toInt(),
                        posMs = elapsedMs.toInt(),
                        lineStartMs = currentLine.timeMs.toInt(),
                        lineEndMs = lineEndMs(lastIndex).toInt()
                    )
                    if (sent.isFailure) noteSendFailed()
                    else noteSendOk("Syncing: $title (${lyrics.size} lines)")
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
                lines.add(LrcLine(timeMs, text, Romanizer.romanize(text)))
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
