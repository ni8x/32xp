package com.esp32dashboard.lyrics

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.os.Build
import android.os.SystemClock
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import kotlin.jvm.JvmStatic

class MusicNotificationListener : NotificationListenerService() {

    companion object {
        const val TAG = "MusicListener"

        // Known music app packages
        val MUSIC_PACKAGES = listOf(
            // YouTube Music family
            "com.google.android.apps.youtube.music",       // Official YT Music
            "app.morphe.android.apps.youtube.music",       // YT Music (Morpheus fork)
            "app.revanced.android.apps.youtube.music",     // YT Music ReVanced
            "app.rvx.android.apps.youtube.music",          // YT Music ReVanced Extended
            "com.vanced.android.apps.youtube.music",       // YT Music Vanced (legacy)
            // YouTube family
            "com.google.android.youtube",                   // Official YouTube
            "app.morphe.android.youtube",                   // YouTube (Morpheus fork)
            "app.revanced.android.youtube",                 // YouTube ReVanced
            // Other streaming services
            "com.spotify.music",                            // Spotify
            "com.apple.android.music",                      // Apple Music
            "com.amazon.mp3",                               // Amazon Music
            "com.soundcloud.android",                       // SoundCloud
            "deezer.android.app",                           // Deezer
            "com.gaana.app",                                // Gaana
            "com.jiosaavn.android",                         // JioSaavn
            "com.pandora.android",                          // Pandora
            // Local playback
            "com.maxmpz.audioplayer",                       // Poweramp
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

        // --- Playback position tracking (P1) ---
        // Lyric sync uses the player's REAL position instead of a local timer
        // started at detection, so buffering/queued songs (or long intros) no
        // longer drift. Values are updated by a MediaController attached to the
        // active media session, with notification extras as a fallback.
        @Volatile
        var positionAvailable = false
        @Volatile
        var playbackPositionMs = 0L
        @Volatile
        var positionUpdatedAtElapsed = 0L
        @Volatile
        var isPlaying = false
        @Volatile
        var playbackSpeed = 1f
        @Volatile
        var durationMs = 0L
        @Volatile
        var playbackSignalSeen = false

        /**
         * Interpolated playback position in ms, or null when no usable position
         * source exists (caller falls back to its own clock).
         */
        fun currentPlaybackPosition(): Long? {
            if (!positionAvailable || !playbackSignalSeen) return null
            var pos = playbackPositionMs
            if (positionUpdatedAtElapsed > 0 && isPlaying) {
                val deltaMs = SystemClock.elapsedRealtime() - positionUpdatedAtElapsed
                pos += (deltaMs * playbackSpeed).toLong()
            }
            if (durationMs > 0 && pos > durationMs) pos = durationMs
            return pos.coerceAtLeast(0L)
        }

        /** True once any playback-state info (controller or extras) has arrived. */
        fun playbackSignaled(): Boolean = playbackSignalSeen

        /**
         * New song detected: zero the position and wait for playback to start.
         * Source availability flags are kept, so players without a position
         * source still fall back to the engine's timer.
         */
        fun resetPlaybackState() {
            playbackPositionMs = 0L
            positionUpdatedAtElapsed = SystemClock.elapsedRealtime()
            isPlaying = false
        }

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

                // New song: wait for playback to begin before syncing
                resetPlaybackState()
                inst.readPlaybackExtras(notifExtras)

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

    // Attached media controllers, unregistered on disconnect to avoid leaks
    private val mediaControllers = mutableListOf<Pair<MediaController, MediaController.Callback>>()

    override fun onListenerConnected() {
        instance = this
        Log.d(TAG, "Notification listener connected")
        attachMediaControllers()
        // Auto-rescan when first connected
        rescanActive()
    }

    override fun onListenerDisconnected() {
        instance = null
        for ((controller, callback) in mediaControllers) {
            try {
                controller.unregisterCallback(callback)
            } catch (_: Exception) {}
        }
        mediaControllers.clear()
        Log.d(TAG, "Notification listener disconnected")
    }

    /**
     * Attach to the active media sessions so we can read the real playback
     * position. A notification listener is implicitly authorized to call
     * getActiveSessions without MEDIA_CONTENT_CONTROL.
     */
    private fun attachMediaControllers() {
        try {
            val msm = getSystemService(Context.MEDIA_SESSION_SERVICE) as? MediaSessionManager ?: return
            val component = ComponentName(this, MusicNotificationListener::class.java)
            val sessions = msm.getActiveSessions(component)
            // Prefer music apps; fall back to any session so unlisted/renamed
            // builds (like future ReVanced renames) still get position sync.
            val musicSessions = sessions.filter { isMusicApp(it.packageName) }
            val targets = if (musicSessions.isNotEmpty()) musicSessions else sessions
            // Idempotent: sessions created AFTER the listener connected are only
            // visible on a later getActiveSessions call, so this is re-run on
            // every music notification post. Skip packages already attached.
            val attached = mediaControllers.map { it.first.packageName }.toSet()
            for (controller in targets) {
                if (controller.packageName in attached) continue
                val callback = mediaControllerCallback(controller)
                controller.registerCallback(callback)
                mediaControllers.add(controller to callback)
                // Prime with current state so a mid-song START syncs immediately
                updateFromPlaybackState(controller.playbackState)
                controller.metadata?.let {
                    durationMs = it.getLong(MediaMetadata.METADATA_KEY_DURATION)
                }
                Log.d(TAG, "Attached media controller for ${controller.packageName}")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to attach media controllers: ${e.message}")
        }
    }

    private fun mediaControllerCallback(controller: MediaController) = object : MediaController.Callback() {
        override fun onPlaybackStateChanged(state: PlaybackState?) {
            updateFromPlaybackState(state)
        }

        override fun onMetadataChanged(metadata: MediaMetadata?) {
            metadata?.let { durationMs = it.getLong(MediaMetadata.METADATA_KEY_DURATION) }
        }

        override fun onSessionDestroyed() {
            // Player recreated its session (common on track changes): prune the
            // dead controller so a later getActiveSessions picks up the new one.
            Log.d(TAG, "Media session destroyed for ${controller.packageName}")
            try {
                controller.unregisterCallback(this)
            } catch (_: Exception) {}
            mediaControllers.removeAll { it.first === controller }
        }
    }

    private fun updateFromPlaybackState(state: PlaybackState?) {
        if (state == null) return
        playbackSignalSeen = true
        positionAvailable = true
        isPlaying = state.state == PlaybackState.STATE_PLAYING
        playbackSpeed = state.playbackSpeed
        playbackPositionMs = state.position
        // lastPositionUpdateTime is on the elapsedRealtime clock; 0 means unknown
        positionUpdatedAtElapsed =
            if (state.lastPositionUpdateTime > 0) state.lastPositionUpdateTime
            else SystemClock.elapsedRealtime()
    }

    /**
     * Fallback position source: many players embed playback state in the
     * notification extras. Called on every music notification post so the
     * position stays fresh even without a media controller.
     */
    private fun readPlaybackExtras(extras: android.os.Bundle) {
        try {
            if (!extras.containsKey("android.media.playstatechanged") &&
                !extras.containsKey("android.media.playstate.PLAYBACK_STATE")
            ) {
                return
            }
            playbackSignalSeen = true
            if (extras.containsKey("android.media.playstate.IS_PLAYING")) {
                isPlaying = extras.getBoolean("android.media.playstate.IS_PLAYING")
            }
            if (extras.containsKey("android.media.playstate.PLAYBACK_POSITION")) {
                val pos = extras.getLong("android.media.playstate.PLAYBACK_POSITION")
                var ts = extras.getLong("android.media.playstate.LAST_POSITION_UPDATE_TIMESTAMP")
                if (ts <= 0) ts = SystemClock.elapsedRealtime()
                positionAvailable = true
                playbackPositionMs = pos
                positionUpdatedAtElapsed = ts
            }
            if (extras.containsKey("android.media.metadata.DURATION")) {
                durationMs = extras.getLong("android.media.metadata.DURATION")
            }
        } catch (e: Exception) {
            Log.e(TAG, "readPlaybackExtras failed: ${e.message}")
        }
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

        // Keep the position fresh on every post (some players re-post frequently),
        // and pick up media sessions created after the listener connected.
        readPlaybackExtras(extras)
        attachMediaControllers()

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

        // New song: wait for playback to begin before syncing
        resetPlaybackState()
        readPlaybackExtras(extras)

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
