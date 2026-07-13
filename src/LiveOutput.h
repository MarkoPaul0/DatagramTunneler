#pragma once

#include <cstddef>

enum class LogLevel {
    Info,
    Warning,
    Error,
    Death,
};

void configureCompactOutput(bool requested);
void logMessage(LogLevel level, const char* format, ...);
void recordDatagram(std::size_t bytes);
