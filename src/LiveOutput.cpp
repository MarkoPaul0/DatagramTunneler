#include "LiveOutput.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace {

constexpr std::size_t kEventLines = 5;

const char* levelName(LogLevel level) {
    switch (level) {
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARNING";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Death:
        return "DEATH";
    }
    return "LOG";
}

bool enableAnsiTerminal() {
#ifdef _WIN32
    if (_isatty(_fileno(stdout)) == 0) {
        return false;
    }
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (output == INVALID_HANDLE_VALUE || GetConsoleMode(output, &mode) == 0) {
        return false;
    }
    return SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

std::size_t terminalColumns() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info {};
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(output, &info) != 0) {
        return static_cast<std::size_t>(info.srWindow.Right - info.srWindow.Left + 1);
    }
#else
    winsize size {};
    if (ioctl(fileno(stdout), TIOCGWINSZ, &size) == 0 && size.ws_col != 0) {
        return size.ws_col;
    }
#endif
    return 80;
}

std::string fitTerminalLine(std::string_view line, std::size_t columns) {
    // Keep one spare column: some terminals wrap immediately after the final column.
    const std::size_t maximum = columns > 1 ? columns - 1 : 1;
    if (line.size() <= maximum) {
        return std::string(line);
    }
    if (maximum <= 3) {
        return std::string(line.substr(0, maximum));
    }
    return std::string(line.substr(0, maximum - 3)) + "...";
}

class CompactOutput {
public:
    void configureVerbose(bool requested) {
        verbose_ = requested;
    }

    bool verbose() const {
        return verbose_;
    }

    void configure(bool requested, std::string_view context) {
        if (!requested) {
            return;
        }
        if (!enabled_) {
            enabled_ = enableAnsiTerminal();
            if (!enabled_) {
                return;
            }
            started_at_ = std::chrono::steady_clock::now();
            std::fputs("\033[?25l\n\n\n\n\n\n", stdout);
        }
        if (!context.empty()) {
            context_ = context;
        }
        render();
    }

    bool enabled() const {
        return enabled_;
    }

    void log(LogLevel level, const char* message) {
        if (!enabled_) {
            std::fprintf(stdout, "%-9s %s\n", levelName(level), message);
            std::fflush(stdout);
            return;
        }
        appendEvent(std::string(levelName(level)) + "  " + message);
    }

    void logCompact(LogLevel level, const char* message) {
        if (!enabled_) {
            return;
        }
        if (level == LogLevel::Info) {
            appendEvent(message);
        } else {
            appendEvent(std::string(levelName(level)) + "  " + message);
        }
    }

    void record(std::size_t bytes) {
        if (!enabled_) {
            return;
        }
        ++datagram_count_;
        byte_count_ += bytes;
        render();
    }

    void recordLatencyValue(double milliseconds) {
        if (!enabled_) {
            return;
        }
        latency_samples_.push_back(milliseconds);
        if (latency_samples_.size() > kMaxLatencySamples) {
            latency_samples_.pop_front();
        }
        render();
    }

    ~CompactOutput() {
        if (enabled_) {
            std::fputs("\033[?25h\n", stdout);
            std::fflush(stdout);
        }
    }

private:
    void appendEvent(std::string event) {
        events_[next_event_] = std::move(event);
        next_event_ = (next_event_ + 1) % kEventLines;
        if (event_count_ < kEventLines) {
            ++event_count_;
        }
        render();
    }
    std::string statisticsLine(std::size_t columns) const {
        const auto elapsed = std::chrono::steady_clock::now() - started_at_;
        const double elapsed_seconds = std::chrono::duration<double>(elapsed).count();
        const double average_size = datagram_count_ == 0 ? 0.0 :
            static_cast<double>(byte_count_) / static_cast<double>(datagram_count_);
        const double throughput = elapsed_seconds <= 0.0 ? 0.0 :
            static_cast<double>(byte_count_) / elapsed_seconds;
        char buffer[320] {};
        const char* const prefix = context_.empty() ? "Stats" : context_.c_str();
        if (latency_samples_.empty()) {
            std::snprintf(buffer, sizeof(buffer),
                          "%s | %zu pkts | %.1f B avg | %.1f B/s | latency: unavailable",
                          prefix, datagram_count_, average_size, throughput);
            const std::string full_line(buffer);
            if (full_line.size() < columns) {
                return full_line;
            }
            std::snprintf(buffer, sizeof(buffer), "%s | %zu pkts | %.1f B | %.1f B/s | lat: n/a",
                          prefix, datagram_count_, average_size, throughput);
            return buffer;
        }
        std::vector<double> latency(latency_samples_.begin(), latency_samples_.end());
        std::sort(latency.begin(), latency.end());
        double latency_sum = 0.0;
        for (const double sample : latency) {
            latency_sum += sample;
        }
        const auto percentile = [&latency](double percentile_value) {
            const std::size_t index = static_cast<std::size_t>(percentile_value *
                static_cast<double>(latency.size() - 1));
            return latency[index];
        };
        std::snprintf(buffer, sizeof(buffer),
                      "%s | %zu pkts | %.1f B avg | %.1f B/s | lat ms: %.2f avg / %.2f p50 / %.2f p99 / %.2f max",
                      prefix, datagram_count_, average_size, throughput, latency_sum / static_cast<double>(latency.size()),
                      percentile(0.50), percentile(0.99), latency.back());
        const std::string full_line(buffer);
        if (full_line.size() < columns) {
            return full_line;
        }
        std::snprintf(buffer, sizeof(buffer),
                      "%s | %zu pkts | %.1f B | %.1f B/s | latencies avg/p50/p99/max %.2f/%.2f/%.2f/%.2f ms",
                      prefix, datagram_count_, average_size, throughput,
                      latency_sum / static_cast<double>(latency.size()), percentile(0.50), percentile(0.99), latency.back());
        return buffer;
    }

    void render() const {
        const std::size_t columns = terminalColumns();
        std::fputs("\033[6A\r\033[2K", stdout);
        std::fputs(fitTerminalLine(statisticsLine(columns), columns).c_str(), stdout);
        std::fputc('\n', stdout);
        const std::size_t first_event = event_count_ == kEventLines ? next_event_ : 0;
        for (std::size_t index = 0; index < kEventLines; ++index) {
            std::fputs("\r\033[2K", stdout);
            if (index < event_count_) {
                const std::size_t event_index = (first_event + index) % kEventLines;
                std::fputs(fitTerminalLine(events_[event_index], columns).c_str(), stdout);
            }
            std::fputc('\n', stdout);
        }
        std::fflush(stdout);
    }

    bool enabled_ = false;
    bool verbose_ = false;
    std::string context_;
    std::array<std::string, kEventLines> events_ {};
    std::size_t next_event_ = 0;
    std::size_t event_count_ = 0;
    std::size_t datagram_count_ = 0;
    std::size_t byte_count_ = 0;
    static constexpr std::size_t kMaxLatencySamples = 1024;
    std::deque<double> latency_samples_;
    std::chrono::steady_clock::time_point started_at_ {};
};

CompactOutput& output() {
    static CompactOutput instance;
    return instance;
}

} // namespace

void configureCompactOutput(bool requested, std::string_view context) {
    output().configure(requested, context);
}

bool compactOutputEnabled() {
    return output().enabled();
}

void configureVerboseOutput(bool requested) {
    output().configureVerbose(requested);
}

bool verboseOutputEnabled() {
    return output().verbose();
}

void logMessage(LogLevel level, const char* format, ...) {
    char message[1024] {};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    output().log(level, message);
}

void logCompactMessage(LogLevel level, const char* format, ...) {
    if (!compactOutputEnabled()) {
        return;
    }
    char message[1024] {};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    output().logCompact(level, message);
}

void recordDatagram(std::size_t bytes) {
    output().record(bytes);
}

void recordLatency(double milliseconds) {
    output().recordLatencyValue(milliseconds);
}
