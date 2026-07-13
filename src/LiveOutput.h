#pragma once

#include <cstddef>
#include <string_view>

enum class LogLevel {
    Info,
    Warning,
    Error,
    Death,
};

void configureCompactOutput(bool requested, std::string_view context = {});
bool compactOutputEnabled();
void logMessage(LogLevel level, const char* format, ...);
void logCompactMessage(LogLevel level, const char* format, ...);
void recordDatagram(std::size_t bytes);
void recordLatency(double milliseconds);
