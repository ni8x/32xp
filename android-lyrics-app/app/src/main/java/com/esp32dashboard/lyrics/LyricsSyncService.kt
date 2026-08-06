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
        const val EXTRA_ESP32_IP = "esp32_ip"
    }

    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private var engine: LyricsEngine? = null
    private var esp32Client: Esp32Client? = null
    private var currentIp = ""

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
                startForeground(NOTIFICATION_ID, buildNotification("Starting..."))
                startSync(ip)
            }
            ACTION_STOP -> {
                stopSync()
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
            }
        }
        return START_STICKY
    }

    private fun startSync(ip: String) {
        val client = Esp32Client(ip)
        esp32Client = client

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
        engine = eng
        eng.start(scope)

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

    private fun stopSync() {
        scope.coroutineContext.cancelChildren()
        engine?.stop()
        engine = null
        val client = esp32Client // capture value before nulling
        esp32Client = null
        if (client != null) {
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
        stopSync()
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
