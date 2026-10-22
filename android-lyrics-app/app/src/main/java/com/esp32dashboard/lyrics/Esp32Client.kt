package com.esp32dashboard.lyrics

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody
import org.json.JSONObject
import java.util.concurrent.TimeUnit

/** Snapshot of the lyric settings and live state published by /api/status. */
data class LyricsPanelStatus(
    val active: Boolean,
    val phoneConnected: Boolean,
    val title: String,
    val artist: String,
    val currentLine: String,
    val progress: Int,
    val color: String,
    val fullScreen: Boolean,
    val scrollAnimation: Boolean,
    val textSize: Int
)

/** Device-wide settings shown on the web dashboard's Appearance card. */
data class DevicePanelStatus(
    val theme: String,
    val rotation: Int,
    val brightness: Int
)

/** Manual/live score state published by the scores plugin. */
data class ScoresPanelStatus(
    val sport: String,
    val teamA: String,
    val scoreA: String,
    val teamB: String,
    val scoreB: String,
    val status: String,
    val live: Boolean,
    val error: String
)

data class PanelStatus(
    val lyrics: LyricsPanelStatus,
    val device: DevicePanelStatus,
    val scores: ScoresPanelStatus
)

class Esp32Client(rawIp: String) {

    companion object {
        private const val TAG = "Esp32Client"

        // Users paste "http://10.0.0.1/", "//10.0.0.1/" (protocol-relative), or
        // "10.0.0.1" into the IP field. Prepending "http://" to a slashed value
        // produced malformed URLs (http:////10.0.0.1//api/...) that the ESP32
        // rejected with HTTP 500 and that broke the ping. Reduce to a bare host
        // (strip scheme, stray slashes, and any pasted path).
        fun normalizeIp(raw: String?): String {
            var ip = raw?.trim() ?: ""
            ip = ip.removePrefix("http://").removePrefix("https://")
            ip = ip.trim('/')
            return ip.substringBefore('/')
        }
    }

    private val esp32Ip = normalizeIp(rawIp)

    /** Host this client posts to; shown in status/error messages. */
    val address: String get() = esp32Ip

    private val client = OkHttpClient.Builder()
        .connectTimeout(3, TimeUnit.SECONDS)
        .readTimeout(3, TimeUnit.SECONDS)
        .build()

    private val formMediaType = "application/x-www-form-urlencoded".toMediaType()

    // The ESP32's GET-with-query parser and its JSON body handler are both
    // unreliable with OkHttp (HTTP 500), but form-encoded POSTs use the same
    // plain handler + parser as the web dashboard's settings (mode, brightness,
    // Wi-Fi), which are proven to work on the device.
    suspend fun sendNowPlaying(artist: String, title: String): Result<Unit> = postForm(
        path = "/api/lyrics/nowplaying",
        body = "artist=${artist.urlEncode()}&title=${title.urlEncode()}"
    )

    // posSec/durSec let the ESP32 show live m:ss timestamps on the progress
    // bar (durSec == 0 means the length is unknown and timestamps are hidden).
    // posMs/lineStartMs/lineEndMs carry the same times in song milliseconds, so
    // the panel can report how far through the current line the song is without
    // needing extra traffic between line changes.
    suspend fun sendLine(
        current: String,
        prev: String = "",
        next: String = "",
        progress: Int = 0,
        posSec: Int = 0,
        durSec: Int = 0,
        posMs: Int = 0,
        lineStartMs: Int = 0,
        lineEndMs: Int = 0
    ): Result<Unit> = postForm(
        path = "/api/lyrics/line",
        body = "current=${current.urlEncode()}&prev=${prev.urlEncode()}&next=${next.urlEncode()}" +
            "&progress=$progress&pos=$posSec&dur=$durSec" +
            "&posms=$posMs&lstart=$lineStartMs&lend=$lineEndMs"
    )

    suspend fun clearLyrics(): Result<Unit> = postForm(path = "/api/lyrics/clear", body = "")

    // Heartbeat: tells the ESP32 this phone is still driving it. The device
    // treats a long silence as "phone gone" (LyricsManager::PHONE_SILENCE_MS),
    // so a dead sync service can no longer leave the panel claiming Connected.
    suspend fun ping(): Result<Unit> = postForm(path = "/api/lyrics/ping", body = "")

    // Theme preset (default / pikachu / cyberpunk / midnight) - same endpoint
    // as the web dashboard's theme dropdown.
    suspend fun setTheme(theme: String): Result<Unit> = postForm(
        path = "/api/theme",
        body = "theme=${theme.urlEncode()}"
    )

    /**
     * Read the same JSON the web dashboard polls. Lyric styling lives at the
     * top level (lyricColor/lyricFull/lyricScroll/lyricFullSize), live state in
     * the nested "lyrics" object, device settings at the top level, and scores
     * under "plugins.scores".
     */
    suspend fun getPanelStatus(): Result<PanelStatus> = withContext(Dispatchers.IO) {
        try {
            val request = Request.Builder()
                .url("http://$esp32Ip/api/status")
                .get()
                .build()
            client.newCall(request).execute().use { response ->
                val body = response.body?.string() ?: ""
                if (!response.isSuccessful) {
                    Log.e(TAG, "HTTP ${response.code} from /api/status: $body")
                    return@withContext Result.failure(
                        Exception("ESP32 error: ${response.code}: $body")
                    )
                }
                val root = JSONObject(body)
                val lyrics = root.optJSONObject("lyrics")
                val scores = root.optJSONObject("plugins")?.optJSONObject("scores")
                Result.success(
                    PanelStatus(
                        lyrics = LyricsPanelStatus(
                            active = lyrics?.optBoolean("active", false) ?: false,
                            phoneConnected = lyrics?.optBoolean("phoneConnected", false) ?: false,
                            title = lyrics?.optString("title", "").orEmpty(),
                            artist = lyrics?.optString("artist", "").orEmpty(),
                            currentLine = lyrics?.optString("currentLine", "").orEmpty(),
                            progress = lyrics?.optInt("progress", 0) ?: 0,
                            color = root.optString("lyricColor", "#00FFFF"),
                            fullScreen = root.optBoolean("lyricFull", true),
                            scrollAnimation = root.optBoolean("lyricScroll", true),
                            textSize = root.optInt("lyricFullSize", 2).coerceIn(1, 2)
                        ),
                        device = DevicePanelStatus(
                            theme = root.optString("theme", "default"),
                            rotation = root.optInt("rotation", 0).coerceIn(0, 3),
                            brightness = root.optInt("brightness", 255).coerceIn(0, 255)
                        ),
                        scores = ScoresPanelStatus(
                            sport = scores?.optString("sport", "cricket").orEmpty(),
                            teamA = scores?.optString("teamA", "").orEmpty(),
                            scoreA = scores?.optString("scoreA", "").orEmpty(),
                            teamB = scores?.optString("teamB", "").orEmpty(),
                            scoreB = scores?.optString("scoreB", "").orEmpty(),
                            status = scores?.optString("status", "").orEmpty(),
                            live = scores?.optBoolean("live", false) ?: false,
                            error = scores?.optString("error", "").orEmpty()
                        )
                    )
                )
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to GET /api/status: ${e.message}")
            Result.failure(e)
        }
    }

    /** Same form endpoint as the web dashboard's colour picker. */
    suspend fun setLyricColor(hex: String): Result<Unit> = postForm(
        path = "/api/lyriccolor",
        body = "color=${hex.urlEncode()}"
    )

    /** Mirrors the web dashboard's Lyrics Style controls. */
    suspend fun setLyricEffects(
        fullScreen: Boolean,
        scrollAnimation: Boolean,
        textSize: Int
    ): Result<Unit> = postForm(
        path = "/api/lyriceffect",
        body = "full=${if (fullScreen) 1 else 0}" +
            "&scroll=${if (scrollAnimation) 1 else 0}" +
            "&fullsize=${textSize.coerceIn(1, 2)}"
    )

    suspend fun setOrientation(rotation: Int): Result<Unit> = postForm(
        path = "/api/orientation",
        body = "rotation=${rotation.coerceIn(0, 3)}"
    )

    suspend fun setBrightness(value: Int): Result<Unit> = postForm(
        path = "/api/brightness",
        body = "brightness=${value.coerceIn(0, 255)}"
    )

    suspend fun restart(): Result<Unit> = postForm(path = "/api/restart", body = "")

    suspend fun factoryReset(): Result<Unit> = postForm(path = "/api/reset", body = "")

    suspend fun setScoreSport(sport: String): Result<Unit> = postForm(
        path = "/api/plugin/scores/sport",
        body = "sport=${sport.urlEncode()}"
    )

    suspend fun showScore(
        sport: String,
        teamA: String,
        scoreA: String,
        teamB: String,
        scoreB: String,
        status: String
    ): Result<Unit> = postForm(
        path = "/api/plugin/scores/set",
        body = "sport=${sport.urlEncode()}" +
            "&teamA=${teamA.urlEncode()}" +
            "&scoreA=${scoreA.urlEncode()}" +
            "&teamB=${teamB.urlEncode()}" +
            "&scoreB=${scoreB.urlEncode()}" +
            "&status=${status.urlEncode()}"
    )

    suspend fun clearScore(): Result<Unit> = postForm(
        path = "/api/plugin/scores/clear",
        body = ""
    )

    private suspend fun postForm(path: String, body: String): Result<Unit> =
        withContext(Dispatchers.IO) {
            try {
                val request = Request.Builder()
                    .url("http://$esp32Ip$path")
                    .post(RequestBody.create(formMediaType, body))
                    .build()
                client.newCall(request).execute().use { response ->
                    if (response.isSuccessful) {
                        Log.d(TAG, "OK -> http://$esp32Ip$path")
                        Result.success(Unit)
                    } else {
                        val respBody = response.body?.string() ?: ""
                        Log.e(TAG, "HTTP ${response.code} from http://$esp32Ip$path: $respBody")
                        Result.failure(Exception("ESP32 error: ${response.code}: $respBody"))
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "Failed to POST http://$esp32Ip$path: ${e.message}")
                Result.failure(e)
            }
        }

    private fun String.urlEncode(): String =
        // URLEncoder uses '+' for spaces, but the web dashboard's proven
        // requests use encodeURIComponent which produces '%20'. Match that
        // byte-for-byte so the ESP32 decodes spaces correctly.
        java.net.URLEncoder.encode(this, "UTF-8").replace("+", "%20")
}
