#include "WeatherManager.h"
#include "Logger.h"

static const char* TAG = "WeatherManager";
// Fetch every 15 minutes; retry after 1 minute if the last attempt failed
#define FETCH_INTERVAL_MS 900000
#define RETRY_INTERVAL_MS 60000

WeatherManager::WeatherManager(Settings& settings, WiFiManager& wifi)
    : _settings(settings), _wifi(wifi), _hasData(false), _temperature(0), _weatherCode(0), _lastFetch(0) {
}

WeatherManager::~WeatherManager() {
}

void WeatherManager::begin() {
    _hasData = false;
    _lastFetch = 0;
}

void WeatherManager::update() {
    if (!_wifi.isConnected()) return;
    // No coordinates configured yet — nothing to fetch (avoids a 60s retry spam loop)
    if (_settings.getLatitude().isEmpty() || _settings.getLongitude().isEmpty()) return;

    uint32_t now = millis();
    // Fetch immediately on boot once connected, or every 15 mins
    if (!_hasData || (now - _lastFetch > FETCH_INTERVAL_MS)) {
        _lastFetch = now; // Prevent spamming while a fetch is in flight
        if (!fetchWeather()) {
            // Schedule a sooner retry if the last attempt failed
            _lastFetch = now - (FETCH_INTERVAL_MS - RETRY_INTERVAL_MS);
        }
    }
}

bool WeatherManager::fetchWeather() {
    LOG_INFO(TAG, "Fetching weather data from Open-Meteo...");
    
    String lat = _settings.getLatitude();
    String lon = _settings.getLongitude();
    String url = "http://api.open-meteo.com/v1/forecast?latitude=" + lat + "&longitude=" + lon + "&current_weather=true";
    
    HTTPClient http;
    http.setTimeout(5000); // Don't block the main loop indefinitely
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
            _temperature = doc["current_weather"]["temperature"];
            _weatherCode = doc["current_weather"]["weathercode"];
            _hasData = true;
            LOG_INFO(TAG, "Weather fetched successfully");
            http.end();
            return true;
        }
        LOG_ERROR(TAG, "Failed to parse JSON");
    } else {
        LOG_ERROR(TAG, "HTTP Request failed");
    }
    http.end();
    return false;
}

bool WeatherManager::hasData() const {
    return _hasData;
}

float WeatherManager::getTemperature() const {
    return _temperature;
}

String WeatherManager::getWeatherCondition() const {
    return codeToCondition(_weatherCode);
}

String WeatherManager::codeToCondition(uint8_t code) const {
    // Open-Meteo WMO weather interpretation codes
    if (code == 0) return "Clear Sky";
    if (code == 1 || code == 2 || code == 3) return "Partly Cloudy";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 51 && code <= 55) return "Drizzle";
    if (code >= 61 && code <= 65) return "Rain";
    if (code >= 71 && code <= 77) return "Snow";
    if (code >= 80 && code <= 82) return "Showers";
    if (code >= 95 && code <= 99) return "Thunderstorm";
    return "Unknown";
}
