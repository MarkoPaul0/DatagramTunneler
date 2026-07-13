#pragma once
#include <cstdlib>

#include "LiveOutput.h"

#define INFO(_format_,...)  do { logMessage(LogLevel::Info, _format_, ##__VA_ARGS__); } while (0)
#define WARN(_format_,...)  do { logMessage(LogLevel::Warning, _format_, ##__VA_ARGS__); } while (0)
#define ERROR(_format_,...) do { logMessage(LogLevel::Error, _format_, ##__VA_ARGS__); } while (0)
#define DEATH(_format_,...) do { logMessage(LogLevel::Death, _format_, ##__VA_ARGS__); std::exit(1); } while (0)
