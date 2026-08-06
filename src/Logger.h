#pragma once

#include <Arduino.h>

class Logger {
public:
    static void init();
    static void info(const char* tag, const char* message);
    static void warn(const char* tag, const char* message);
    static void error(const char* tag, const char* message);
};

#define LOG_INFO(tag, msg) Logger::info(tag, msg)
#define LOG_WARN(tag, msg) Logger::warn(tag, msg)
#define LOG_ERROR(tag, msg) Logger::error(tag, msg)
