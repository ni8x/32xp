# 32XP

A small always-on desk dashboard for the ESP32-S3. It shows the time, the song you are listening to, synced lyrics, and live sports scores on a 240x320 display. You can change everything from a browser, and there is an optional Android app for the sofa.

No cloud account is required. The panel runs on your own Wi-Fi.

## What it does

- **Clock and date** with several themes.
- **Now playing** information from your phone or computer.
- **Synced lyrics** that wrap and scroll smoothly. Lyrics come from LRCLIB.
- **Hindi and Punjabi lyrics** can be shown in easy-to-read Hinglish in the Android app.
- **Cricket and football scores** from ESPN, or your own manual score.
- **Web control panel** for Wi-Fi, themes, brightness, orientation, plugins, lyrics, and scores.
- **Android companion app** for lyrics, panel settings, and score control.
- **Automatic rotation** between dashboard screens.

## What you need

- An ESP32-S3 development board with 16 MB flash. A common `esp32-s3-devkitc-1` board works.
- A 240x320 ST7789 SPI display.
- A USB-C **data** cable. Charge-only cables will not work.
- A 2.4 GHz Wi-Fi network. The ESP32-S3 does not support 5 GHz Wi-Fi.
- A computer for the one-time firmware install.
- Optional: an Android phone for the companion app.

## Quick start

### 1. Install the firmware (one time)

You only need to do this when you first set up the panel or update the firmware.

1. Install [Visual Studio Code](https://code.visualstudio.com/).
2. Open the Extensions view and install **PlatformIO IDE**.
3. Download this project with **Code > Download ZIP**, then extract it.
4. In VS Code, choose **File > Open Folder** and open the extracted project folder.
5. Connect the panel with the USB-C data cable.
6. Open the PlatformIO sidebar and click **Upload**.

If PlatformIO asks which port to use, pick the new USB serial port that appeared when you connected the panel.

The first upload can take a few minutes.

### 2. Connect the panel to your Wi-Fi

On first boot, when no Wi-Fi has been saved, the panel creates its own setup network.

1. On your phone or laptop, open the Wi-Fi settings.
2. Join **`32XP-Setup`**.
3. Use the password **`32xpsetup`**.
4. Open **http://192.168.4.1** in a browser.
5. Go to **Settings > Wi-Fi**.
6. Enter your home Wi-Fi name and password. Use the **2.4 GHz** network, not the 5 GHz one.
7. Click **Save**, then **Connect**.
8. Wait for the panel to join your network. The page will show the panel's new IP address after it connects.
9. Click **Restart** to close the setup network.

After the restart, open the panel's IP address in a browser from any device on the same Wi-Fi. For example:

```text
http://192.168.1.50
```

> [!TIP]
> If you do not see the panel's IP address, open your router's client list and look for a new Espressif/ESP32 device.

### 3. Install the Android app (optional)

The Android app adds lyrics sync, romanization, and a remote control for the panel.

1. Open the `android-lyrics-app` folder in Android Studio.
2. Connect your phone and run the app, or build an APK:

   ```powershell
   cd android-lyrics-app
   .\gradlew.bat assembleDebug
   ```

3. The APK is created at `android-lyrics-app/app/build/outputs/apk/debug/app-debug.apk`.
4. Open the app, enter the panel's IP address on the **Connect** tab, and press start.
5. Allow notification access when Android asks. The app needs it to read the current song.

## Everyday use

Open the panel's IP address in a browser to:

- Change the theme, brightness, or screen orientation.
- Choose which dashboard screens are shown.
- Adjust lyric color, text size, full-screen mode, and scroll animation.
- Show or clear a manual sports score.
- Restart the panel or reset its settings.

The Android app can do the same remotely, plus start and stop lyric sync.

## Troubleshooting

### The `32XP-Setup` network is not visible

- Wait about 10 seconds after powering on.
- Make sure the panel is not connected to an old Wi-Fi network. The setup network only appears when no Wi-Fi is saved.
- Power-cycle the panel and try again.

### `192.168.4.1` does not open

- Stay connected to `32XP-Setup`. Some phones switch back to mobile data when the setup network has no internet.
- Turn off mobile data temporarily, then reopen the page.
- Try another browser.

### The panel does not join my Wi-Fi

- Use a **2.4 GHz** network.
- Check the Wi-Fi password carefully.
- Move the panel closer to the router during setup.
- If the router uses a captive portal or enterprise login, use a normal home Wi-Fi network instead.

### I cannot find the panel's IP address

- Check your router's connected-device list.
- Look for a new Espressif/ESP32 device.
- Restart the panel and watch the serial log at 115200 baud if you have a serial monitor.

### Lyrics do not appear

- Start playback in a supported music app, then start sync in the Android app.
- Make sure notification access is enabled for the Android app.
- LRCLIB may not have synced lyrics for every song. Instrumental or very new tracks are often missing.

### The screen is blank

- Check the display wiring and power.
- Make sure the board matches the `esp32-s3-devkitc-1` PlatformIO environment.
- Reflash the firmware.

## Security

The web control panel has no login. Anyone on the same local network can change settings, restart the panel, or reset it.

- Keep the panel on a trusted home network.
- Do not forward its web port from your router to the internet.
- Use the setup hotspot only while setting up the panel.
- The panel stores Wi-Fi credentials in its own flash. They are not sent to a cloud service.

Lyrics are fetched from the public LRCLIB API. Scores are fetched from the public ESPN API. No private API keys are included.

## License

32XP is licensed under the **GNU General Public License v3.0**. See [LICENSE](LICENSE).

You can use, study, share, and modify the project. If you distribute a modified version, it must remain GPL-3.0 and its source must be available. Commercial use is allowed; closed-source redistribution is not.
