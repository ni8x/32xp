package com.esp32dashboard.lyrics

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody
import java.util.concurrent.TimeUnit

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
    suspend fun sendLine(
        current: String,
        prev: String = "",
        next: String = "",
        progress: Int = 0,
        posSec: Int = 0,
        durSec: Int = 0
    ): Result<Unit> = postForm(
        path = "/api/lyrics/line",
        body = "current=${current.urlEncode()}&prev=${prev.urlEncode()}&next=${next.urlEncode()}" +
            "&progress=$progress&pos=$posSec&dur=$durSec"
    )

    suspend fun clearLyrics(): Result<Unit> = postForm(path = "/api/lyrics/clear", body = "")

    // Theme preset (default / pikachu / cyberpunk / midnight) - same endpoint
    // as the web dashboard's theme dropdown.
    suspend fun setTheme(theme: String): Result<Unit> = postForm(
        path = "/api/theme",
        body = "theme=${theme.urlEncode()}"
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
