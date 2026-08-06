package com.esp32dashboard.lyrics

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {

    private lateinit var ipInput: EditText
    private lateinit var testButton: Button
    private lateinit var toggleButton: Button
    private lateinit var statusText: TextView
    private lateinit var songText: TextView
    private lateinit var lyricText: TextView

    private var isRunning = false

    private val statusReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action == "com.esp32dashboard.lyrics.STATUS_UPDATE") {
                val type = intent.getStringExtra("type")
                val value = intent.getStringExtra("value") ?: return
                runOnUiThread {
                    when (type) {
                        "status" -> statusText.text = value
                        "line" -> lyricText.text = value
                        "song" -> {
                            val parts = value.split("|")
                            if (parts.size == 2) {
                                songText.text = "${parts[1]} - ${parts[0]}"
                            }
                        }
                    }
                }
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        ipInput = findViewById(R.id.ipInput)
        testButton = findViewById(R.id.testButton)
        toggleButton = findViewById(R.id.toggleButton)
        statusText = findViewById(R.id.statusText)
        songText = findViewById(R.id.songText)
        lyricText = findViewById(R.id.lyricText)

        // Load saved IP
        val prefs = getSharedPreferences("config", MODE_PRIVATE)
        ipInput.setText(prefs.getString("esp32_ip", "192.168."))

        val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            RECEIVER_NOT_EXPORTED
        } else {
            0
        }
        registerReceiver(statusReceiver,
            IntentFilter("com.esp32dashboard.lyrics.STATUS_UPDATE"), flags)

        testButton.setOnClickListener {
            testConnection()
        }

        toggleButton.setOnClickListener {
            if (isRunning) {
                stopService()
            } else {
                startService()
            }
        }

        checkPermissions()
    }

    private fun testConnection() {
        val ip = ipInput.text.toString().trim()
        if (ip.isEmpty()) {
            Toast.makeText(this, "Enter ESP32 IP address", Toast.LENGTH_SHORT).show()
            return
        }

        testButton.isEnabled = false
        testButton.text = "TESTING..."
        statusText.text = "Testing connection..."

        Thread {
            try {
                val url = java.net.URL("http://$ip/api/status")
                val conn = url.openConnection() as java.net.HttpURLConnection
                conn.connectTimeout = 3000
                conn.readTimeout = 3000
                val code = conn.responseCode

                // Also send ping to register phone as connected
                try {
                    val pingUrl = java.net.URL("http://$ip/api/lyrics/ping")
                    val pingConn = pingUrl.openConnection() as java.net.HttpURLConnection
                    pingConn.requestMethod = "POST"
                    pingConn.doOutput = true
                    pingConn.connectTimeout = 2000
                    pingConn.readTimeout = 2000
                    pingConn.outputStream.write("connected=true".toByteArray())
                    pingConn.outputStream.close()
                    pingConn.responseCode
                } catch (_: Exception) {}

                runOnUiThread {
                    if (code == 200) {
                        statusText.text = "✓ Connected to ESP32"
                    } else {
                        statusText.text = "✗ Server error: HTTP $code"
                    }
                }
            } catch (e: java.net.SocketTimeoutException) {
                runOnUiThread {
                    statusText.text = "✗ Connection timed out"
                }
            } catch (e: java.net.ConnectException) {
                runOnUiThread {
                    statusText.text = "✗ Cannot reach ESP32 at $ip"
                }
            } catch (e: Exception) {
                runOnUiThread {
                    statusText.text = "✗ Error: ${e.message ?: "Unknown"}"
                }
            } finally {
                runOnUiThread {
                    testButton.isEnabled = true
                    testButton.text = "TEST"
                }
            }
        }.start()
    }

    private fun checkPermissions() {
        // Notification permission (Android 13+)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED
            ) {
                requestPermissions(
                    arrayOf(Manifest.permission.POST_NOTIFICATIONS), 100
                )
            }
        }

        // Notification listener permission
        if (!MusicNotificationListener.isPermissionGranted(this)) {
            AlertDialog.Builder(this)
                .setTitle("Enable Notification Access")
                .setMessage("This app needs notification access to detect what song is playing on YouTube Music.")
                .setPositiveButton("Open Settings") { _, _ ->
                    startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
                }
                .setNegativeButton("Cancel", null)
                .show()
        }
    }

    private fun startService() {
        val ip = ipInput.text.toString().trim()
        if (ip.isEmpty()) {
            Toast.makeText(this, "Enter ESP32 IP address", Toast.LENGTH_SHORT).show()
            return
        }

        // Save IP
        getSharedPreferences("config", MODE_PRIVATE).edit()
            .putString("esp32_ip", ip).apply()

        val intent = Intent(this, LyricsSyncService::class.java).apply {
            action = LyricsSyncService.ACTION_START
            putExtra(LyricsSyncService.EXTRA_ESP32_IP, ip)
        }
        startForegroundService(intent)

        isRunning = true
        toggleButton.text = "STOP"
        statusText.text = "Starting..."
        songText.text = "Waiting for song..."
        lyricText.text = ""
    }

    private fun stopService() {
        val intent = Intent(this, LyricsSyncService::class.java).apply {
            action = LyricsSyncService.ACTION_STOP
        }
        startService(intent)

        isRunning = false
        toggleButton.text = "START SYNC"
        statusText.text = "Stopped"
        songText.text = ""
        lyricText.text = ""
    }

    override fun onDestroy() {
        unregisterReceiver(statusReceiver)
        super.onDestroy()
    }
}
