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

class CompactOutput {
public:
    void configure(bool requested) {
        if (!requested || enabled_) {
            return;
        }
        enabled_ = enableAnsiTerminal();
        if (!enabled_) {
            return;
        }
        started_at_ = std::chrono::steady_clock::now();
        std::fputs("\033[?25l\n\n\n\n\n\n", stdout);
        render();
    }

    void log(LogLevel level, const char* message) {
        if (!enabled_) {
            std::fprintf(stdout, "%-9s %s\n", levelName(level), message);
            std::fflush(stdout);
            return;
        }
        events_[next_event_] = std::string(levelName(level)) + "  " + message;
        next_event_ = (next_event_ + 1) % kEventLines;
        if (event_count_ < kEventLines) {
            ++event_count_;
        }
        render();
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
    std::string statisticsLine() const {
        const auto elapsed = std::chrono::steady_clock::now() - started_at_;
        const double elapsed_seconds = std::chrono::duration<double>(elapsed).count();
        const double average_size = datagram_count_ == 0 ? 0.0 :
            static_cast<double>(byte_count_) / static_cast<double>(datagram_count_);
        const double throughput = elapsed_seconds <= 0.0 ? 0.0 :
            static_cast<double>(byte_count_) / elapsed_seconds;
        char buffer[320] {};
        if (latency_samples_.empty()) {
            std::snprintf(buffer, sizeof(buffer),
                          "Stats | datagrams: %zu | average size: %.1f B | throughput: %.1f B/s | latency: unavailable",
                          datagram_count_, average_size, throughput);
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
                      "Stats | datagrams: %zu | average size: %.1f B | throughput: %.1f B/s | latency ms: avg %.2f p50 %.2f p99 %.2f max %.2f",
                      datagram_count_, average_size, throughput, latency_sum / static_cast<double>(latency.size()),
                      percentile(0.50), percentile(0.99), latency.back());
        return buffer;
    }

    void render() const {
        std::fputs("\033[6A\r\033[2K", stdout);
        std::fputs(statisticsLine().c_str(), stdout);
        std::fputc('\n', stdout);
        const std::size_t first_event = event_count_ == kEventLines ? next_event_ : 0;
        for (std::size_t index = 0; index < kEventLines; ++index) {
            std::fputs("\r\033[2K", stdout);
            if (index < event_count_) {
                const std::size_t event_index = (first_event + index) % kEventLines;
                std::fputs(events_[event_index].c_str(), stdout);
            }
            std::fputc('\n', stdout);
        }
        std::fflush(stdout);
    }

    bool enabled_ = false;
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

void configureCompactOutput(bool requested) {
    output().configure(requested);
}

void logMessage(LogLevel level, const char* format, ...) {
    char message[1024] {};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    output().log(level, message);
}

void recordDatagram(std::size_t bytes) {
    output().record(bytes);
}

void recordLatency(double milliseconds) {
    output().recordLatencyValue(milliseconds);
}
