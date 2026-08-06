#pragma once

#include <Arduino.h>

namespace Config {
    // Application Info
    constexpr char APP_NAME[] = "ESP32-S3 Dashboard";
    constexpr char FIRMWARE_VERSION[] = "0.1.7";

    // Display Constants
    constexpr uint16_t DISPLAY_WIDTH = 240;
    constexpr uint16_t DISPLAY_HEIGHT = 320;
    constexpr uint8_t DEFAULT_ROTATION = 0;

    // Display GPIOs (Waveshare ESP32-S3-LCD-2 with ST7789T3)
    constexpr int8_t TFT_SCLK = 39;
    constexpr int8_t TFT_MOSI = 38;
    constexpr int8_t TFT_MISO = 40;
    constexpr int8_t TFT_DC   = 42;
    constexpr int8_t TFT_CS   = 45;
    constexpr int8_t TFT_RST  = -1;
    constexpr int8_t TFT_BL   = 1;

    // Settings
    constexpr uint8_t DEFAULT_BRIGHTNESS = 200; // 0-255
}
