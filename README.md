# 32XP

ESP32-S3 desk dashboard firmware with a web control panel and an Android lyrics companion.

## What it does

- Drives a 240x320 ST7789 display in portrait or landscape.
- Plugin-based screens for clock, media, lyrics, and sports scores.
- Scrolling, word-wrapped lyrics with a consistent text size.
- Lyrics source: LRCLIB, with manual text entry for testing.
- Android app converts Hindi and Punjabi lyrics to readable Hinglish before sending them to the panel.
- Android remote for lyric colour/size, theme, orientation, brightness, restart, factory reset, and manual cricket/football scores.

## Hardware

- ESP32-S3 development board (Arduino/ESP-IDF via PlatformIO).
- ST7789-family 240x320 SPI display.
- Optional touch/button input as wired in `Config.h`.

## Repository layout

```text
src/                    ESP32 firmware
  plugins/              Clock, media, lyrics, scores screens
  Display.*             Text shaping, emoji, effects
  LyricsManager.*       Lyric state and timing
  Settings.*            NVS-backed settings
  WebServer.*           Web dashboard and /api endpoints
android-lyrics-app/     Android companion (Kotlin)
tools/                  Static consistency checks
```

## Build firmware

```powershell
pio run
pio run -t upload --upload-port COM5
```

Open the serial monitor separately after flashing.

## Build the Android app

```powershell
cd android-lyrics-app
.\gradlew.bat test
.\gradlew.bat assembleDebug
```

The debug APK is written to `app/build/outputs/apk/debug/app-debug.apk`.

## First boot

1. Flash the firmware and open the serial monitor.
2. Join the device's setup Wi-Fi network if it has no saved credentials.
3. Open the dashboard web page, set Wi-Fi, display theme, orientation, brightness, and lyric options.
4. Install the Android app, enter the panel's IP address, and start lyric sync.

Settings are stored in NVS on the device. The web page and Android app both read live state from `/api/status`.

## Notes

- Do not commit local serial logs, build output, `local.properties`, or signing keys.
- ESPN score data is fetched from public endpoints; no API key is required.
- This is application firmware running on FreeRTOS through Arduino/ESP-IDF, not a standalone operating system.

## License

No license has been declared for this repository. All rights reserved unless a license is added.
