package com.esp32dashboard.lyrics

import android.app.*
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.*

class LyricsSyncService : Service() {

    companion object {
        const val TAG = "LyricsService"
        const val NOTIFICATION_ID = 1001
        const val CHANNEL_ID = "lyrics_sync"
        const val ACTION_START = "com.esp32dashboard.lyrics.START"
        const val ACTION_STOP = "com.esp32dashboard.lyrics.STOP"
        const val ACTION_SET_ROMANIZE = "com.esp32dashboard.lyrics.SET_ROMANIZE"
        const val EXTRA_ESP32_IP = "esp32_ip"
        const val EXTRA_THEME = "theme"
        const val EXTRA_ROMANIZE = "romanize"

        // Shared with MainActivity: which address the running service posts to,
        // so the UI can retarget it instead of silently keeping a stale one.
        const val PREFS = "config"
        const val KEY_RUNNING = "sync_running"
        const val KEY_RUNNING_IP = "sync_running_ip"
        const val KEY_THEME = "theme"
        const val KEY_ROMANIZE = "romanize_lyrics"
        private const val HEARTBEAT_MS = 15_000L
    }

    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private var engine: LyricsEngine? = null
    private var esp32Client: Esp32Client? = null
    private var currentIp = ""
    private var heartbeatJob: Job? = null

    private val songReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action == MusicNotificationListener.ACTION_SONG_CHANGED) {
                val title = intent.getStringExtra(MusicNotificationListener.EXTRA_TITLE) ?: return
                val artist = intent.getStringExtra(MusicNotificationListener.EXTRA_ARTIST) ?: return
                Log.d(TAG, "Received song: $title - $artist")
                engine?.onSongChanged(artist, title)
            }
        }
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            RECEIVER_NOT_EXPORTED
        } else {
            0
        }
        registerReceiver(songReceiver, IntentFilter(MusicNotificationListener.ACTION_SONG_CHANGED), flags)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> {
                val ip = intent.getStringExtra(EXTRA_ESP32_IP) ?: return START_STICKY
                currentIp = ip
                markRunning(ip)
                startForeground(NOTIFICATION_ID, buildNotification("Starting..."))
                val romanize = intent.getBooleanExtra(
                    EXTRA_ROMANIZE,
                    getSharedPreferences(PREFS, MODE_PRIVATE).getBoolean(KEY_ROMANIZE, true)
                )
                getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                    .putBoolean(KEY_ROMANIZE, romanize).apply()
                startSync(ip, intent.getStringExtra(EXTRA_THEME) ?: "default", romanize)
            }
            ACTION_SET_ROMANIZE -> {
                val enabled = intent.getBooleanExtra(EXTRA_ROMANIZE, true)
                getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                    .putBoolean(KEY_ROMANIZE, enabled).apply()
                engine?.setRomanizeEnabled(enabled)
            }
            ACTION_STOP -> {
                markStopped()
                stopSync(clearDevice = true)
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
            }
            else -> {
                // START_STICKY restart (process killed, phone rebooted) arrives
                // with a null intent and used to be ignored: the foreground
                // service came back with no engine and lyrics never resumed,
                // silently. Resume the saved session instead.
                val prefs = getSharedPreferences(PREFS, MODE_PRIVATE)
                if (!prefs.getBoolean(KEY_RUNNING, false)) {
                    stopSelf()
                    return START_NOT_STICKY
                }
                val ip = prefs.getString(KEY_RUNNING_IP, null).orEmpty()
                if (ip.isBlank()) {
                    markStopped()
                    stopSelf()
                    return START_NOT_STICKY
                }
                currentIp = ip
                startForeground(NOTIFICATION_ID, buildNotification("Resuming..."))
                startSync(
                    ip,
                    prefs.getString(KEY_THEME, "default") ?: "default",
                    prefs.getBoolean(KEY_ROMANIZE, true)
                )
            }
        }
        return START_STICKY
    }

    private fun markRunning(ip: String) {
        getSharedPreferences(PREFS, MODE_PRIVATE).edit()
            .putBoolean(KEY_RUNNING, true)
            .putString(KEY_RUNNING_IP, ip)
            .apply()
    }

    private fun markStopped() {
        getSharedPreferences(PREFS, MODE_PRIVATE).edit()
            .putBoolean(KEY_RUNNING, false)
            .putString(KEY_RUNNING_IP, "")
            .apply()
    }

    private fun startSync(ip: String, theme: String, romanize: Boolean) {
        // Retargeting (the board's address changed) starts the service again.
        // The previous engine must stop, or it keeps posting to the old address
        // alongside the new one and the two fight over the display.
        engine?.stop()
        engine = null
        heartbeatJob?.cancel()
        heartbeatJob = null

        val client = Esp32Client(ip)
        esp32Client = client

        // Push the app's theme choice so the display matches it. Only send when
        // the user actually picked a theme here - "default" means leave the web
        // dashboard's theme alone (applying a theme would also re-seed any
        // lyric/heart colors customized there).
        if (theme != "default") {
            scope.launch {
                client.setTheme(theme)
                    .onFailure { Log.e(TAG, "Failed to send theme: ${it.message}") }
            }
        }

        val eng = LyricsEngine(
            esp32Client = client,
            onStatus = { status ->
                updateNotification("Status: $status")
                sendStatusBroadcast("status", status)
            },
            onLyricLine = { line ->
                sendStatusBroadcast("line", line)
            },
            onSongDetected = { artist, title ->
                updateNotification("$title - $artist")
                sendStatusBroadcast("song", "$title|$artist")
            }
        )
        eng.setRomanizeEnabled(romanize)
        engine = eng
        eng.start(scope)

        // Liveness independent of lyric traffic: a paused song, a track without
        // synced lyrics, or a wrong address must not leave the ESP32 showing a
        // stale "Phone Connected" (it drops the phone after PHONE_SILENCE_MS).
        heartbeatJob = scope.launch {
            var failures = 0
            var beats = 0
            while (isActive) {
                if (client.ping().isSuccess) {
                    if (failures >= 3) sendStatusBroadcast("status", "Reconnected to $ip")
                    failures = 0
                    // Re-assert the song header: a board reboot wipes it and the
                    // engine only sends it on a song change, so the panel would
                    // otherwise show the line with no title until the next track.
                    if (beats % 4 == 0) {
                        val title = MusicNotificationListener.currentTitle
                        val artist = MusicNotificationListener.currentArtist
                        if (title.isNotBlank()) client.sendNowPlaying(artist, title)
                    }
                } else if (++failures == 3) {
                    val message = "ESP32 not responding at $ip - check the IP"
                    updateNotification(message)
                    sendStatusBroadcast("status", message)
                }
                beats++
                delay(HEARTBEAT_MS)
            }
        }

        // The notification listener can detect the current song before this
        // service is started. In that case rescanActive() intentionally ignores
        // it as a duplicate, so send the cached song to this new engine now.
        val cachedTitle = MusicNotificationListener.currentTitle
        val cachedArtist = MusicNotificationListener.currentArtist
        if (cachedTitle.isNotBlank() && cachedArtist.isNotBlank()) {
            Log.d(TAG, "Starting sync with cached song: $cachedTitle - $cachedArtist")
            eng.onSongChanged(cachedArtist, cachedTitle)
        }

        // Repeatedly try to pick up any song already playing
        // (listener might connect at any time on some devices)
        scope.launch {
            repeat(5) { attempt ->
                delay(2000) // check every 2 seconds
                Log.d(TAG, "Rescan attempt ${attempt + 1}/5")
                MusicNotificationListener.rescanActive()
            }
        }
    }

    private fun stopSync(clearDevice: Boolean = true) {
        heartbeatJob?.cancel()
        heartbeatJob = null
        scope.coroutineContext.cancelChildren()
        engine?.stop()
        engine = null
        val client = esp32Client // capture value before nulling
        esp32Client = null
        if (clearDevice && client != null) {
            // Use a dedicated scope: when stopSync is called from onDestroy, the
            // service scope is cancelled immediately after, which would kill the
            // clear request before it could be sent.
            CoroutineScope(Dispatchers.IO).launch {
                try {
                    client.clearLyrics()
                } catch (_: Exception) {}
            }
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        // Do not blank the panel here: the system may be restarting us
        // (START_STICKY), in which case a clear would flash the idle screen
        // before the saved session resumes.
        stopSync(clearDevice = false)
        unregisterReceiver(songReceiver)
        scope.cancel()
        super.onDestroy()
    }

    private fun createNotificationChannel() {
        val channel = NotificationChannel(
            CHANNEL_ID, "Lyrics Sync",
            NotificationManager.IMPORTANCE_LOW
        ).apply {
            description = "Shows current lyrics sync status"
        }
        (getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
            .createNotificationChannel(channel)
    }

    private fun buildNotification(text: String): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("ESP32 Lyrics Sync")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
    }

    private fun updateNotification(text: String) {
        val notification = buildNotification(text)
        (getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
            .notify(NOTIFICATION_ID, notification)
    }

    private fun sendStatusBroadcast(type: String, value: String) {
        val intent = Intent("com.esp32dashboard.lyrics.STATUS_UPDATE").apply {
            setPackage(packageName)
            putExtra("type", type)
            putExtra("value", value)
        }
        sendBroadcast(intent)
    }
}
