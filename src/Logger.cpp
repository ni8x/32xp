#include "Logger.h"

void Logger::init() {
    Serial.begin(115200);
    // Brief delay to allow serial to stabilize
    delay(100); 
    Serial.println("Logger initialized");
}

void Logger::info(const char* tag, const char* message) {
    Serial.printf("[INFO] [%s] %s\n", tag, message);
}

void Logger::warn(const char* tag, const char* message) {
    Serial.printf("[WARN] [%s] %s\n", tag, message);
}

void Logger::error(const char* tag, const char* message) {
    Serial.printf("[ERROR] [%s] %s\n", tag, message);
}
