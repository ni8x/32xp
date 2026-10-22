package com.esp32dashboard.lyrics

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.content.res.ColorStateList
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.EditText
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.widget.doAfterTextChanged
import kotlinx.coroutines.runBlocking

class MainActivity : AppCompatActivity() {

    private lateinit var ipInput: EditText
    private lateinit var themeSpinner: Spinner
    private lateinit var romanizeSwitch: Switch
    private lateinit var testButton: TextView
    private lateinit var toggleButton: TextView
    private lateinit var statusText: TextView
    private lateinit var songText: TextView
    private lateinit var lyricText: TextView
    private lateinit var testTextInput: EditText
    private lateinit var sendTestTextButton: TextView
    private lateinit var lyricSettingsStatus: TextView
    private lateinit var lyricSettingsNowPlaying: TextView
    private lateinit var lyricColorInput: EditText
    private lateinit var lyricColorPreview: View
    private lateinit var lyricSizeSpinner: Spinner
    private lateinit var lyricFullSwitch: Switch
    private lateinit var lyricScrollSwitch: Switch
    private lateinit var refreshLyricSettingsButton: TextView
    private lateinit var saveLyricSettingsButton: TextView
    private lateinit var lyricSettingsResult: TextView
    private lateinit var deviceThemeSpinner: Spinner
    private lateinit var deviceOrientationSpinner: Spinner
    private lateinit var brightnessSeek: SeekBar
    private lateinit var brightnessValue: TextView
    private lateinit var applyDeviceSettingsButton: TextView
    private lateinit var restartButton: TextView
    private lateinit var factoryResetButton: TextView
    private lateinit var deviceSettingsResult: TextView
    private lateinit var scoreSportSpinner: Spinner
    private lateinit var scoreLiveStatus: TextView
    private lateinit var scoreTeamA: EditText
    private lateinit var scoreScoreA: EditText
    private lateinit var scoreTeamB: EditText
    private lateinit var scoreScoreB: EditText
    private lateinit var scoreStatusLine: EditText
    private lateinit var showScoreButton: TextView
    private lateinit var clearScoreButton: TextView
    private lateinit var scoreResult: TextView
    private lateinit var connectionResult: TextView
    private lateinit var sendResult: TextView
    private lateinit var statusDot: View
    private lateinit var topChip: TextView
    private lateinit var navHome: View
    private lateinit var navConnect: View
    private lateinit var navDisplay: View
    private lateinit var sectionHome: View
    private lateinit var sectionConnect: View
    private lateinit var sectionDisplay: View

    private var isRunning = false
    // Address the running foreground service is posting to (empty when idle).
    private var runningIp = ""

    // Spinner shows friendly labels; these raw keys are what the ESP32 expects.
    private val themeKeys = arrayOf("default", "pikachu", "cyberpunk", "midnight")
    private var lastScoreSportPosition = -1

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
        romanizeSwitch = findViewById(R.id.romanizeSwitch)
        testButton = findViewById(R.id.testButton)
        toggleButton = findViewById(R.id.toggleButton)
        statusText = findViewById(R.id.statusText)
        songText = findViewById(R.id.songText)
        lyricText = findViewById(R.id.lyricText)
        testTextInput = findViewById(R.id.testTextInput)
        sendTestTextButton = findViewById(R.id.sendTestTextButton)
        lyricSettingsStatus = findViewById(R.id.lyricSettingsStatus)
        lyricSettingsNowPlaying = findViewById(R.id.lyricSettingsNowPlaying)
        lyricColorInput = findViewById(R.id.lyricColorInput)
        lyricColorPreview = findViewById(R.id.lyricColorPreview)
        lyricSizeSpinner = findViewById(R.id.lyricSizeSpinner)
        lyricFullSwitch = findViewById(R.id.lyricFullSwitch)
        lyricScrollSwitch = findViewById(R.id.lyricScrollSwitch)
        refreshLyricSettingsButton = findViewById(R.id.refreshLyricSettingsButton)
        saveLyricSettingsButton = findViewById(R.id.saveLyricSettingsButton)
        lyricSettingsResult = findViewById(R.id.lyricSettingsResult)
        deviceThemeSpinner = findViewById(R.id.deviceThemeSpinner)
        deviceOrientationSpinner = findViewById(R.id.deviceOrientationSpinner)
        brightnessSeek = findViewById(R.id.brightnessSeek)
        brightnessValue = findViewById(R.id.brightnessValue)
        applyDeviceSettingsButton = findViewById(R.id.applyDeviceSettingsButton)
        restartButton = findViewById(R.id.restartButton)
        factoryResetButton = findViewById(R.id.factoryResetButton)
        deviceSettingsResult = findViewById(R.id.deviceSettingsResult)
        scoreSportSpinner = findViewById(R.id.scoreSportSpinner)
        scoreLiveStatus = findViewById(R.id.scoreLiveStatus)
        scoreTeamA = findViewById(R.id.scoreTeamA)
        scoreScoreA = findViewById(R.id.scoreScoreA)
        scoreTeamB = findViewById(R.id.scoreTeamB)
        scoreScoreB = findViewById(R.id.scoreScoreB)
        scoreStatusLine = findViewById(R.id.scoreStatusLine)
        showScoreButton = findViewById(R.id.showScoreButton)
        clearScoreButton = findViewById(R.id.clearScoreButton)
        scoreResult = findViewById(R.id.scoreResult)
        connectionResult = findViewById(R.id.connectionResult)
        sendResult = findViewById(R.id.sendResult)
        statusDot = findViewById(R.id.statusDot)
        topChip = findViewById(R.id.topChip)
        navHome = findViewById(R.id.navHome)
        navConnect = findViewById(R.id.navConnect)
        navDisplay = findViewById(R.id.navDisplay)
        sectionHome = findViewById(R.id.sectionHome)
        sectionConnect = findViewById(R.id.sectionConnect)
        sectionDisplay = findViewById(R.id.sectionDisplay)

        navHome.setOnClickListener { selectTab(0) }
        navConnect.setOnClickListener { selectTab(1) }
        navDisplay.setOnClickListener {
            selectTab(2)
            refreshLyricSettings()
        }
        selectTab(0)
        setConnectionIndicator(false)

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

        lyricSizeSpinner.adapter = ArrayAdapter(
            this,
            android.R.layout.simple_spinner_item,
            arrayOf("Compact", "Normal")
        ).apply {
            setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        }
        lyricSizeSpinner.setSelection(1)
        updateLyricColorPreview("#00FFFF")
        lyricColorInput.doAfterTextChanged { editable ->
            updateLyricColorPreview(editable?.toString().orEmpty())
        }
        refreshLyricSettingsButton.setOnClickListener { refreshLyricSettings() }
        saveLyricSettingsButton.setOnClickListener { saveLyricSettings() }

        deviceThemeSpinner.adapter = ArrayAdapter(
            this,
            android.R.layout.simple_spinner_item,
            arrayOf("Default", "Pikachu", "Cyberpunk", "Midnight")
        ).apply {
            setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        }
        deviceOrientationSpinner.adapter = ArrayAdapter(
            this,
            android.R.layout.simple_spinner_item,
            arrayOf("Portrait", "Landscape", "Portrait Flipped", "Landscape Flipped")
        ).apply {
            setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        }
        brightnessSeek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                brightnessValue.text = progress.toString()
            }

            override fun onStartTrackingTouch(seekBar: SeekBar?) = Unit
            override fun onStopTrackingTouch(seekBar: SeekBar?) = Unit
        })
        brightnessValue.text = brightnessSeek.progress.toString()
        applyDeviceSettingsButton.setOnClickListener { applyDeviceSettings() }
        restartButton.setOnClickListener { confirmRestart() }
        factoryResetButton.setOnClickListener { confirmFactoryReset() }

        scoreSportSpinner.adapter = ArrayAdapter(
            this,
            android.R.layout.simple_spinner_item,
            arrayOf("Cricket", "Football")
        ).apply {
            setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        }
        scoreSportSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(
                parent: AdapterView<*>?,
                view: View?,
                position: Int,
                id: Long
            ) {
                // Clear manual fields when switching sport so cricket values do
                // not get submitted as football scores.
                if (position != lastScoreSportPosition) {
                    lastScoreSportPosition = position
                    scoreTeamA.setText("")
                    scoreScoreA.setText("")
                    scoreTeamB.setText("")
                    scoreScoreB.setText("")
                    scoreStatusLine.setText("")
                }
            }

            override fun onNothingSelected(parent: AdapterView<*>?) = Unit
        }
        showScoreButton.setOnClickListener { showScore() }
        clearScoreButton.setOnClickListener { clearScore() }

        romanizeSwitch.isChecked = prefs.getBoolean(LyricsSyncService.KEY_ROMANIZE, true)
        romanizeSwitch.setOnCheckedChangeListener { _, enabled ->
            // Persist first: a service restart (START_STICKY) reads this pref.
            getSharedPreferences(LyricsSyncService.PREFS, MODE_PRIVATE).edit()
                .putBoolean(LyricsSyncService.KEY_ROMANIZE, enabled).apply()
            if (isRunning) {
                startService(Intent(this, LyricsSyncService::class.java).apply {
                    action = LyricsSyncService.ACTION_SET_ROMANIZE
                    putExtra(LyricsSyncService.EXTRA_ROMANIZE, enabled)
                })
            }
        }

        val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            RECEIVER_NOT_EXPORTED
        } else {
            0
        }
        registerReceiver(statusReceiver,
            IntentFilter("com.esp32dashboard.lyrics.STATUS_UPDATE"), flags)

        // The foreground service keeps the address it was started with. Editing
        // the IP while syncing used to leave it posting to the old one forever,
        // so a board that moved (DHCP) showed no lyrics while the app looked
        // fine. Follow the field as soon as it loses focus.
        ipInput.setOnFocusChangeListener { _, hasFocus ->
            if (!hasFocus) retargetIfNeeded()
        }

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

        refreshSyncState()
        checkPermissions()
    }

    override fun onResume() {
        super.onResume()
        // The service may have been (re)started while the app was in the
        // background; the button and target must reflect that.
        refreshSyncState()
        retargetIfNeeded()
    }

    private fun enteredIp(): String = Esp32Client.normalizeIp(ipInput.text.toString())

    private fun isValidIp(ip: String): Boolean =
        Regex("""^\d{1,3}(\.\d{1,3}){3}$""").matches(ip) &&
            ip.split('.').all { it.toIntOrNull()?.let { n -> n in 0..255 } == true }

    /** Adopt the IP field as the sync target if the service is running elsewhere. */
    private fun retargetIfNeeded() {
        val ip = enteredIp()
        if (!isRunning || !isValidIp(ip) || ip == runningIp) return
        Toast.makeText(this, "Sync re-targeted to $ip", Toast.LENGTH_SHORT).show()
        startService()
    }

    /** Mirror the service's saved state into the toggle/button. */
    private fun refreshSyncState() {
        val prefs = getSharedPreferences(LyricsSyncService.PREFS, MODE_PRIVATE)
        isRunning = prefs.getBoolean(LyricsSyncService.KEY_RUNNING, false)
        runningIp = prefs.getString(LyricsSyncService.KEY_RUNNING_IP, "").orEmpty()
        toggleButton.text = if (isRunning) "STOP" else "START SYNC"
    }

    private fun testConnection() {
        val ip = enteredIp()
        if (ip.isEmpty()) {
            Toast.makeText(this, "Enter ESP32 IP address", Toast.LENGTH_SHORT).show()
            return
        }

        testButton.isEnabled = false
        testButton.text = "TESTING..."
        connectionResult.text = "Testing connection..."
        connectionResult.setTextColor(ContextCompat.getColor(this, R.color.brown_muted))

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
                        connectionResult.text = "✓ Connected to ESP32"
                        connectionResult.setTextColor(ContextCompat.getColor(this, R.color.olive))
                        setConnectionIndicator(true)
                        // A successful test is also the moment the user fixes a
                        // stale address; move the running sync to it now.
                        retargetIfNeeded()
                    } else {
                        connectionResult.text = "✗ Server error: HTTP $code"
                        connectionResult.setTextColor(ContextCompat.getColor(this, R.color.brick))
                        setConnectionIndicator(false)
                    }
                }
            } catch (e: java.net.SocketTimeoutException) {
                runOnUiThread {
                    connectionResult.text = "✗ Connection timed out"
                    connectionResult.setTextColor(ContextCompat.getColor(this, R.color.brick))
                    setConnectionIndicator(false)
                }
            } catch (e: java.net.ConnectException) {
                runOnUiThread {
                    connectionResult.text = "✗ Cannot reach ESP32 at $ip"
                    connectionResult.setTextColor(ContextCompat.getColor(this, R.color.brick))
                    setConnectionIndicator(false)
                }
            } catch (e: Exception) {
                runOnUiThread {
                    connectionResult.text = "✗ Error: ${e.message ?: "Unknown"}"
                    connectionResult.setTextColor(ContextCompat.getColor(this, R.color.brick))
                    setConnectionIndicator(false)
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

        sendResult.text = "Sending..."
        sendResult.setTextColor(ContextCompat.getColor(this, R.color.brown_muted))

        Thread {
            try {
                val result = runBlocking { Esp32Client(ip).sendLine(message) }
                runOnUiThread {
                    sendResult.text = if (result.isSuccess) {
                        "✓ Sent to ESP32"
                    } else {
                        "✗ Failed: ${result.exceptionOrNull()?.message}"
                    }
                    sendResult.setTextColor(
                        ContextCompat.getColor(this, if (result.isSuccess) R.color.olive else R.color.brick)
                    )
                }
            } catch (e: Exception) {
                runOnUiThread {
                    sendResult.text = "✗ Failed: ${e.message}"
                    sendResult.setTextColor(ContextCompat.getColor(this, R.color.brick))
                }
            }
        }.start()
    }

    /** Pull the same settings the ESP32 web dashboard shows. */
    private fun refreshLyricSettings() {
        val ip = enteredIp()
        if (!isValidIp(ip)) {
            lyricSettingsStatus.text = "Enter a valid ESP32 IP"
            lyricSettingsStatus.setTextColor(ContextCompat.getColor(this, R.color.brick))
            return
        }

        refreshLyricSettingsButton.isEnabled = false
        lyricSettingsResult.text = "Reading panel settings..."
        lyricSettingsResult.setTextColor(ContextCompat.getColor(this, R.color.brown_muted))

        Thread {
            val result = runBlocking { Esp32Client(ip).getPanelStatus() }
            runOnUiThread {
                refreshLyricSettingsButton.isEnabled = true
                result.onSuccess { panel ->
                    val status = panel.lyrics
                    val state = if (status.active) "Active" else "Idle"
                    val phone = if (status.phoneConnected) "phone connected" else "phone offline"
                    lyricSettingsStatus.text = "$state · $phone · ${status.progress}%"
                    lyricSettingsStatus.setTextColor(ContextCompat.getColor(this, R.color.olive))
                    lyricSettingsNowPlaying.text = buildString {
                        val song = listOf(status.title, status.artist)
                            .filter { it.isNotBlank() }
                            .joinToString(" — ")
                        append(song.ifBlank { "No song info" })
                        if (status.currentLine.isNotBlank()) {
                            append("\n")
                            append(status.currentLine)
                        }
                    }
                    lyricColorInput.setText(status.color)
                    lyricSizeSpinner.setSelection((status.textSize - 1).coerceIn(0, 1))
                    lyricFullSwitch.isChecked = status.fullScreen
                    lyricScrollSwitch.isChecked = status.scrollAnimation

                    val device = panel.device
                    deviceThemeSpinner.setSelection(
                        themeKeys.indexOf(device.theme).coerceAtLeast(0)
                    )
                    deviceOrientationSpinner.setSelection(device.rotation.coerceIn(0, 3))
                    brightnessSeek.progress = device.brightness
                    brightnessValue.text = device.brightness.toString()

                    val scores = panel.scores
                    val sportLabel = if (scores.sport == "football") "Football" else "Cricket"
                    val feedLabel = if (scores.live) "Live feed" else "Manual / idle"
                    val hasScore = listOf(
                        scores.teamA, scores.scoreA, scores.teamB, scores.scoreB, scores.status
                    ).any { it.isNotBlank() }
                    scoreLiveStatus.text = if (hasScore) {
                        buildString {
                            append("$sportLabel · $feedLabel")
                            append("\n${scores.teamA}  ${scores.scoreA}  —  ${scores.scoreB}  ${scores.teamB}")
                            if (scores.status.isNotBlank()) append("\n${scores.status}")
                            if (scores.error.isNotBlank()) append("\n⚠ ${scores.error}")
                        }
                    } else {
                        "$sportLabel · $feedLabel · no score loaded"
                    }
                    scoreSportSpinner.setSelection(if (scores.sport == "football") 1 else 0)
                    scoreTeamA.setText(scores.teamA)
                    scoreScoreA.setText(scores.scoreA)
                    scoreTeamB.setText(scores.teamB)
                    scoreScoreB.setText(scores.scoreB)
                    scoreStatusLine.setText(scores.status)

                    lyricSettingsResult.text = "Loaded from $ip"
                    lyricSettingsResult.setTextColor(ContextCompat.getColor(this, R.color.olive))
                    deviceSettingsResult.text = "Loaded from $ip"
                    deviceSettingsResult.setTextColor(ContextCompat.getColor(this, R.color.olive))
                    scoreResult.text = "Loaded from $ip"
                    scoreResult.setTextColor(ContextCompat.getColor(this, R.color.olive))
                    setConnectionIndicator(true)
                }.onFailure { error ->
                    lyricSettingsStatus.text = "Panel offline"
                    lyricSettingsStatus.setTextColor(ContextCompat.getColor(this, R.color.brick))
                    val message = "✗ ${error.message ?: "Could not read settings"}"
                    lyricSettingsResult.text = message
                    lyricSettingsResult.setTextColor(ContextCompat.getColor(this, R.color.brick))
                    deviceSettingsResult.text = message
                    deviceSettingsResult.setTextColor(ContextCompat.getColor(this, R.color.brick))
                    scoreResult.text = message
                    scoreResult.setTextColor(ContextCompat.getColor(this, R.color.brick))
                    setConnectionIndicator(false)
                }
            }
        }.start()
    }

    /** POST the colour and style controls to the ESP32. */
    private fun saveLyricSettings() {
        val ip = enteredIp()
        if (!isValidIp(ip)) {
            Toast.makeText(this, "Enter a valid ESP32 IP address", Toast.LENGTH_SHORT).show()
            return
        }

        val color = lyricColorInput.text.toString().trim().uppercase()
        if (!Regex("^#[0-9A-F]{6}$").matches(color)) {
            lyricSettingsResult.text = "✗ Color must be #RRGGBB"
            lyricSettingsResult.setTextColor(ContextCompat.getColor(this, R.color.brick))
            return
        }

        val fullScreen = lyricFullSwitch.isChecked
        val scrollAnimation = lyricScrollSwitch.isChecked
        val textSize = lyricSizeSpinner.selectedItemPosition + 1

        saveLyricSettingsButton.isEnabled = false
        lyricSettingsResult.text = "Saving to panel..."
        lyricSettingsResult.setTextColor(ContextCompat.getColor(this, R.color.brown_muted))

        Thread {
            val result = try {
                val client = Esp32Client(ip)
                runBlocking {
                    val colorResult = client.setLyricColor(color)
                    if (colorResult.isFailure) {
                        colorResult
                    } else {
                        client.setLyricEffects(fullScreen, scrollAnimation, textSize)
                    }
                }
            } catch (e: Exception) {
                Result.failure(e)
            }

            runOnUiThread {
                saveLyricSettingsButton.isEnabled = true
                if (result.isSuccess) {
                    updateLyricColorPreview(color)
                    lyricSettingsResult.text = "✓ Saved to $ip"
                    lyricSettingsResult.setTextColor(ContextCompat.getColor(this, R.color.olive))
                    lyricSettingsStatus.text = "Settings updated"
                    lyricSettingsStatus.setTextColor(ContextCompat.getColor(this, R.color.olive))
                    setConnectionIndicator(true)
                } else {
                    lyricSettingsResult.text = "✗ ${result.exceptionOrNull()?.message ?: "Save failed"}"
                    lyricSettingsResult.setTextColor(ContextCompat.getColor(this, R.color.brick))
                    setConnectionIndicator(false)
                }
            }
        }.start()
    }

    private fun applyDeviceSettings() {
        val theme = themeKeys[deviceThemeSpinner.selectedItemPosition.coerceIn(0, 3)]
        val rotation = deviceOrientationSpinner.selectedItemPosition.coerceIn(0, 3)
        val brightness = brightnessSeek.progress

        runPanelCommand(
            button = applyDeviceSettingsButton,
            resultView = deviceSettingsResult,
            pending = "Applying device settings...",
            success = "✓ Device settings applied",
            onSuccess = {
                getSharedPreferences(LyricsSyncService.PREFS, MODE_PRIVATE)
                    .edit().putString("theme", theme).apply()
                val themeIndex = themeKeys.indexOf(theme).coerceAtLeast(0)
                deviceThemeSpinner.setSelection(themeIndex)
                themeSpinner.setSelection(themeIndex)
            }
        ) { client ->
            val themeResult = client.setTheme(theme)
            if (themeResult.isFailure) return@runPanelCommand themeResult
            val orientationResult = client.setOrientation(rotation)
            if (orientationResult.isFailure) return@runPanelCommand orientationResult
            client.setBrightness(brightness)
        }
    }

    private fun confirmRestart() {
        AlertDialog.Builder(this)
            .setTitle("Restart panel?")
            .setMessage("The ESP32 will reboot and be unavailable for a few seconds.")
            .setPositiveButton("Restart") { _, _ ->
                runPanelCommand(
                    button = restartButton,
                    resultView = deviceSettingsResult,
                    pending = "Sending restart...",
                    success = "✓ Panel is restarting"
                ) { client -> client.restart() }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun confirmFactoryReset() {
        AlertDialog.Builder(this)
            .setTitle("Factory reset panel?")
            .setMessage(
                "This erases Wi-Fi, theme, lyric and score settings on the ESP32. " +
                    "The panel will reboot into setup mode. This cannot be undone."
            )
            .setPositiveButton("Factory Reset") { _, _ ->
                runPanelCommand(
                    button = factoryResetButton,
                    resultView = deviceSettingsResult,
                    pending = "Resetting panel...",
                    success = "✓ Factory reset sent. Reconnect to the panel's Wi-Fi."
                ) { client -> client.factoryReset() }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun showScore() {
        val sport = if (scoreSportSpinner.selectedItemPosition == 1) "football" else "cricket"
        val teamA = scoreTeamA.text.toString().trim()
        val scoreA = scoreScoreA.text.toString().trim()
        val teamB = scoreTeamB.text.toString().trim()
        val scoreB = scoreScoreB.text.toString().trim()
        val status = scoreStatusLine.text.toString().trim()

        if (listOf(teamA, scoreA, teamB, scoreB, status).all { it.isBlank() }) {
            scoreResult.text = "✗ Enter a team or score first"
            scoreResult.setTextColor(ContextCompat.getColor(this, R.color.brick))
            return
        }

        runPanelCommand(
            button = showScoreButton,
            resultView = scoreResult,
            pending = "Sending score...",
            success = "✓ Score shown on panel",
            onSuccess = {
                scoreLiveStatus.text = buildString {
                    append(if (sport == "football") "Football" else "Cricket")
                    append(" · Manual\n$teamA  $scoreA  —  $scoreB  $teamB")
                    if (status.isNotBlank()) append("\n$status")
                }
            }
        ) { client -> client.showScore(sport, teamA, scoreA, teamB, scoreB, status) }
    }

    private fun clearScore() {
        runPanelCommand(
            button = clearScoreButton,
            resultView = scoreResult,
            pending = "Clearing score...",
            success = "✓ Score cleared",
            onSuccess = {
                scoreTeamA.setText("")
                scoreScoreA.setText("")
                scoreTeamB.setText("")
                scoreScoreB.setText("")
                scoreStatusLine.setText("")
                scoreLiveStatus.text = "No manual score"
            }
        ) { client -> client.clearScore() }
    }

    /**
     * Run one admin POST on a worker thread and report into [resultView].
     * Keeps the IP validation / button disabling / error colouring in one place.
     */
    private fun runPanelCommand(
        button: TextView,
        resultView: TextView,
        pending: String,
        success: String,
        onSuccess: (() -> Unit)? = null,
        command: suspend (Esp32Client) -> Result<Unit>
    ) {
        val ip = enteredIp()
        if (!isValidIp(ip)) {
            resultView.text = "✗ Enter a valid ESP32 IP address"
            resultView.setTextColor(ContextCompat.getColor(this, R.color.brick))
            return
        }

        button.isEnabled = false
        resultView.text = pending
        resultView.setTextColor(ContextCompat.getColor(this, R.color.brown_muted))

        Thread {
            val result = try {
                runBlocking { command(Esp32Client(ip)) }
            } catch (e: Exception) {
                Result.failure(e)
            }
            runOnUiThread {
                button.isEnabled = true
                if (result.isSuccess) {
                    resultView.text = success
                    resultView.setTextColor(ContextCompat.getColor(this, R.color.olive))
                    onSuccess?.invoke()
                    setConnectionIndicator(true)
                } else {
                    resultView.text = "✗ ${result.exceptionOrNull()?.message ?: "Command failed"}"
                    resultView.setTextColor(ContextCompat.getColor(this, R.color.brick))
                    setConnectionIndicator(false)
                }
            }
        }.start()
    }

    private fun updateLyricColorPreview(raw: String) {
        val hex = raw.trim()
        val color = if (Regex("^#[0-9a-fA-F]{6}$").matches(hex)) {
            try {
                Color.parseColor(hex)
            } catch (_: IllegalArgumentException) {
                null
            }
        } else {
            null
        }
        val tint = color ?: ContextCompat.getColor(this, R.color.beige_border)
        lyricColorPreview.backgroundTintList = ColorStateList.valueOf(tint)
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

        // Save IP + theme (and the running target the service resumes/retargets from)
        val theme = themeKeys[themeSpinner.selectedItemPosition]
        getSharedPreferences(LyricsSyncService.PREFS, MODE_PRIVATE).edit()
            .putString("esp32_ip", ip)
            .putString(LyricsSyncService.KEY_THEME, theme)
            .putBoolean(LyricsSyncService.KEY_RUNNING, true)
            .putString(LyricsSyncService.KEY_RUNNING_IP, ip)
            .putBoolean(LyricsSyncService.KEY_ROMANIZE, romanizeSwitch.isChecked)
            .apply()

        val intent = Intent(this, LyricsSyncService::class.java).apply {
            action = LyricsSyncService.ACTION_START
            putExtra(LyricsSyncService.EXTRA_ESP32_IP, ip)
            putExtra(LyricsSyncService.EXTRA_THEME, theme)
            putExtra(LyricsSyncService.EXTRA_ROMANIZE, romanizeSwitch.isChecked)
        }
        startForegroundService(intent)

        isRunning = true
        runningIp = ip
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

        getSharedPreferences(LyricsSyncService.PREFS, MODE_PRIVATE).edit()
            .putBoolean(LyricsSyncService.KEY_RUNNING, false)
            .putString(LyricsSyncService.KEY_RUNNING_IP, "")
            .apply()
        isRunning = false
        runningIp = ""
        toggleButton.text = "START SYNC"
        statusText.text = "Stopped"
        songText.text = ""
        lyricText.text = ""
    }

    /** Show exactly one section and highlight its bottom-nav item. */
    private fun selectTab(index: Int) {
        val navs = listOf(navHome, navConnect, navDisplay)
        val sections = listOf(sectionHome, sectionConnect, sectionDisplay)
        navs.forEachIndexed { i, v -> v.isSelected = (i == index) }
        sections.forEachIndexed { i, v ->
            v.visibility = if (i == index) View.VISIBLE else View.GONE
        }
    }

    /** Update the top-bar dot + chip to reflect ESP32 reachability. */
    private fun setConnectionIndicator(online: Boolean) {
        val color = ContextCompat.getColor(this, if (online) R.color.olive else R.color.brown_muted)
        statusDot.backgroundTintList = android.content.res.ColorStateList.valueOf(color)
        topChip.text = if (online) "ONLINE" else "OFFLINE"
        topChip.setTextColor(color)
    }

    override fun onDestroy() {
        unregisterReceiver(statusReceiver)
        super.onDestroy()
    }
}
