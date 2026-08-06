#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "Settings.h"
#include "WiFiManager.h"

class WeatherManager {
public:
    WeatherManager(Settings& settings, WiFiManager& wifi);
    ~WeatherManager();

    void begin();
    void update();

    bool hasData() const;
    float getTemperature() const;
    String getWeatherCondition() const;

private:
    Settings& _settings;
    WiFiManager& _wifi;
    
    bool _hasData;
    float _temperature;
    uint8_t _weatherCode;
    uint32_t _lastFetch;

    bool fetchWeather();
    String codeToCondition(uint8_t code) const;
};
