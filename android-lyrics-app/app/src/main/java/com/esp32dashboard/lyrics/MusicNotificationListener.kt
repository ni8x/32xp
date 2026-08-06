package com.esp32dashboard.lyrics

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import kotlin.jvm.JvmStatic

class MusicNotificationListener : NotificationListenerService() {

    companion object {
        const val TAG = "MusicListener"

        // Known music app packages
        val MUSIC_PACKAGES = listOf(
            "com.google.android.apps.youtube.music",  // Official YT Music
            "app.morphe.android.apps.youtube.music",    // YT Music ReVanced
            "app.morphe.android.youtube",               // YT ReVanced
            "com.google.android.youtube",                // Official YouTube
            "com.spotify.music",                         // Spotify
        )

        const val ACTION_SONG_CHANGED = "com.esp32dashboard.lyrics.SONG_CHANGED"
        const val EXTRA_ARTIST = "artist"
        const val EXTRA_TITLE = "title"
        const val ACTION_REQUEST_RESCAN = "com.esp32dashboard.lyrics.REQUEST_RESCAN"

        // Track current song to avoid duplicates.
        // @Volatile: read/written from the listener thread AND the service coroutine.
        @Volatile
        var currentArtist = ""
        @Volatile
        var currentTitle = ""

        // Keep reference to the last connected instance for rescan
        @Volatile
        private var instance: MusicNotificationListener? = null

        private fun isMusicApp(pkg: String): Boolean {
            return MUSIC_PACKAGES.any { pkg.equals(it, ignoreCase = true) }
        }

        @JvmStatic
        fun isPermissionGranted(context: Context): Boolean {
            val enabledListeners = android.provider.Settings.Secure.getString(
                context.contentResolver,
                "enabled_notification_listeners"
            ) ?: return false
            return enabledListeners.contains(context.packageName)
        }

        fun rescanActive() {
            val inst = instance ?: return
            Log.d(TAG, "Rescanning active notifications...")
            val notifications = inst.activeNotifications ?: run {
                Log.d(TAG, "No active notifications available")
                return
            }
            Log.d(TAG, "Found ${notifications.size} active notifications total")

            for (sbn in notifications) {
                val pkg = sbn.packageName
                val ongoing = sbn.isOngoing
                val key = sbn.key
                Log.d(TAG, "  Notification: pkg=$pkg ongoing=$ongoing key=$key")

                // Log ALL fields of active notifications, regardless of package
                val extras = sbn.notification.extras
                if (extras != null && extras.keySet().isNotEmpty()) {
                    val title = extras.getCharSequence(android.app.Notification.EXTRA_TITLE)?.toString() ?: "(no title)"
                    val text = extras.getCharSequence(android.app.Notification.EXTRA_TEXT)?.toString() ?: "(no text)"
                    Log.d(TAG, "    title='$title' text='$text'")
                }

                // Only process known music app notifications
                if (!isMusicApp(pkg)) continue
                val notifExtras = sbn.notification.extras ?: continue

                val title = extractTitleFromExtras(notifExtras)
                val artist = extractArtistFromExtras(notifExtras)

                Log.d(TAG, "  Music app ($pkg) extracted: title='$title' artist='$artist'")

                if (title.isEmpty() || artist.isEmpty()) continue
                if (title == currentTitle && artist == currentArtist) continue

                currentTitle = title
                currentArtist = artist

                Log.d(TAG, "Rescanned song: $title - $artist")

                val intent = Intent(ACTION_SONG_CHANGED).apply {
                    setPackage(inst.packageName)
                    putExtra(EXTRA_TITLE, title)
                    putExtra(EXTRA_ARTIST, artist)
                }
                inst.sendBroadcast(intent)
                break // Only process the first/only song
            }

            val musicCount = notifications.count { isMusicApp(it.packageName) }
            if (musicCount == 0) {
                Log.d(TAG, "No music app notifications found among ${notifications.size} active")
            }
        }

        private fun getExtraString(extras: android.os.Bundle, key: String): String {
            return extras.getCharSequence(key)?.toString() ?: ""
        }

        private fun extractTitleFromExtras(extras: android.os.Bundle): String {
            var title = getExtraString(extras, android.app.Notification.EXTRA_TITLE)
            if (title.isEmpty()) {
                title = getExtraString(extras, "android.largeTitle")
            }
            if (title.isEmpty()) {
                title = getExtraString(extras, "android.title.big")
            }
            return title
        }

        private fun extractArtistFromExtras(extras: android.os.Bundle): String {
            var artist = getExtraString(extras, android.app.Notification.EXTRA_TEXT)
            if (artist.isEmpty()) {
                artist = getExtraString(extras, android.app.Notification.EXTRA_SUB_TEXT)
            }
            if (artist.isEmpty()) {
                artist = getExtraString(extras, "android.text.sub")
            }
            return artist
        }
    }

    override fun onListenerConnected() {
        instance = this
        Log.d(TAG, "Notification listener connected")
        // Auto-rescan when first connected
        rescanActive()
    }

    override fun onListenerDisconnected() {
        instance = null
        Log.d(TAG, "Notification listener disconnected")
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        val pkg = sbn.packageName
        val ongoing = sbn.isOngoing
        val key = sbn.key
        Log.d(TAG, "Notification posted: pkg=$pkg ongoing=$ongoing key=$key")

        // Only process known music app notifications (no ongoing filter)
        if (!isMusicApp(pkg)) return

        val extras = sbn.notification.extras ?: run {
            Log.d(TAG, "  No extras in notification")
            return
        }

        // Log ALL extras for debugging
        for (k in extras.keySet()) {
            val value = extras.get(k)
            Log.d(TAG, "  Extra key=$k value=$value")
        }

        // Extract song title and artist using shared helper
        val title = extractTitleFromExtras(extras)
        val artist = extractArtistFromExtras(extras)

        Log.d(TAG, "Extracted: title='$title' artist='$artist'")

        if (title.isEmpty() || artist.isEmpty()) {
            Log.d(TAG, "  Skipping - title or artist empty")
            return
        }

        // Skip if same song (avoid duplicates)
        if (title == currentTitle && artist == currentArtist) {
            Log.d(TAG, "  Skipping - same as current")
            return
        }

        currentTitle = title
        currentArtist = artist

        Log.d(TAG, "Song detected: $title - $artist")

        // Broadcast to our service - set package for reliable delivery
        val intent = Intent(ACTION_SONG_CHANGED).apply {
            setPackage(packageName)
            putExtra(EXTRA_TITLE, title)
            putExtra(EXTRA_ARTIST, artist)
        }
        sendBroadcast(intent)
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        // Don't clear on removal - playback might be paused
    }
}
