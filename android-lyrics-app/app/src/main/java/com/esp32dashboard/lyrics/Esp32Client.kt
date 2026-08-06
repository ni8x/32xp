package com.esp32dashboard.lyrics

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.util.concurrent.TimeUnit

class Esp32Client(private val esp32Ip: String) {

    private val client = OkHttpClient.Builder()
        .connectTimeout(3, TimeUnit.SECONDS)
        .readTimeout(3, TimeUnit.SECONDS)
        .build()

    private val formMediaType = "application/x-www-form-urlencoded".toMediaType()

    suspend fun sendNowPlaying(artist: String, title: String): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            val body = "artist=${artist.urlEncode()}&title=${title.urlEncode()}"
            val request = Request.Builder()
                .url("http://$esp32Ip/api/lyrics/nowplaying")
                .post(body.toRequestBody(formMediaType))
                .build()
            val response = client.newCall(request).execute()
            if (response.isSuccessful) Result.success(Unit)
            else Result.failure(Exception("ESP32 error: ${response.code}"))
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    suspend fun sendLine(current: String, prev: String = "", next: String = "", progress: Int = 0): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            val body = "current=${current.urlEncode()}&prev=${prev.urlEncode()}&next=${next.urlEncode()}&progress=$progress"
            val request = Request.Builder()
                .url("http://$esp32Ip/api/lyrics/line")
                .post(body.toRequestBody(formMediaType))
                .build()
            val response = client.newCall(request).execute()
            if (response.isSuccessful) Result.success(Unit)
            else Result.failure(Exception("ESP32 error: ${response.code}"))
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    suspend fun clearLyrics(): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            val request = Request.Builder()
                .url("http://$esp32Ip/api/lyrics/clear")
                .post("".toRequestBody(formMediaType))
                .build()
            val response = client.newCall(request).execute()
            if (response.isSuccessful) Result.success(Unit)
            else Result.failure(Exception("ESP32 error: ${response.code}"))
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    private fun String.urlEncode(): String {
        return java.net.URLEncoder.encode(this, "UTF-8")
    }
}
