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
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import kotlinx.coroutines.runBlocking

class MainActivity : AppCompatActivity() {

    private lateinit var ipInput: EditText
    private lateinit var themeSpinner: Spinner
    private lateinit var testButton: Button
    private lateinit var toggleButton: Button
    private lateinit var statusText: TextView
    private lateinit var songText: TextView
    private lateinit var lyricText: TextView
    private lateinit var testTextInput: EditText
    private lateinit var sendTestTextButton: Button

    private var isRunning = false

    // Spinner shows friendly labels; these raw keys are what the ESP32 expects.
    private val themeKeys = arrayOf("default", "pikachu", "cyberpunk", "midnight")

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
        themeSpinner = findViewById(R.id.themeSpinner)
        testButton = findViewById(R.id.testButton)
        toggleButton = findViewById(R.id.toggleButton)
        statusText = findViewById(R.id.statusText)
        songText = findViewById(R.id.songText)
        lyricText = findViewById(R.id.lyricText)
        testTextInput = findViewById(R.id.testTextInput)
        sendTestTextButton = findViewById(R.id.sendTestTextButton)

        // Load saved IP (normalize in case a slashed value was saved before)
        val prefs = getSharedPreferences("config", MODE_PRIVATE)
        ipInput.setText(Esp32Client.normalizeIp(prefs.getString("esp32_ip", "192.168.")))

        themeSpinner.adapter = ArrayAdapter(
            this,
            android.R.layout.simple_spinner_item,
            arrayOf("Default", "Pikachu", "Cyberpunk", "Midnight")
        ).apply {
            setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        }
        val savedThemeIndex = themeKeys.indexOf(prefs.getString("theme", "default")).coerceAtLeast(0)
        themeSpinner.setSelection(savedThemeIndex)

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

        sendTestTextButton.setOnClickListener {
            sendTestText()
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

    private fun enteredIp(): String = Esp32Client.normalizeIp(ipInput.text.toString())

    private fun testConnection() {
        val ip = enteredIp()
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

                // Also send ping to register phone as connected (no body needed)
                try {
                    val pingUrl = java.net.URL("http://$ip/api/lyrics/ping")
                    val pingConn = pingUrl.openConnection() as java.net.HttpURLConnection
                    pingConn.requestMethod = "POST"
                    pingConn.connectTimeout = 2000
                    pingConn.readTimeout = 2000
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

    private fun sendTestText() {
        val ip = enteredIp()
        val message = testTextInput.text.toString().trim()
        if (ip.isEmpty() || message.isEmpty()) {
            Toast.makeText(this, "Enter IP and test text", Toast.LENGTH_SHORT).show()
            return
        }

        Thread {
            try {
                val result = runBlocking { Esp32Client(ip).sendLine(message) }
                runOnUiThread {
                    statusText.text = if (result.isSuccess) {
                        "Test text sent to ESP32"
                    } else {
                        "Test text failed: ${result.exceptionOrNull()?.message}"
                    }
                }
            } catch (e: Exception) {
                runOnUiThread { statusText.text = "Test text failed: ${e.message}" }
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
        val ip = enteredIp()
        if (ip.isEmpty()) {
            Toast.makeText(this, "Enter ESP32 IP address", Toast.LENGTH_SHORT).show()
            return
        }

        // Save IP + theme
        val theme = themeKeys[themeSpinner.selectedItemPosition]
        getSharedPreferences("config", MODE_PRIVATE).edit()
            .putString("esp32_ip", ip)
            .putString("theme", theme)
            .apply()

        val intent = Intent(this, LyricsSyncService::class.java).apply {
            action = LyricsSyncService.ACTION_START
            putExtra(LyricsSyncService.EXTRA_ESP32_IP, ip)
            putExtra(LyricsSyncService.EXTRA_THEME, theme)
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
