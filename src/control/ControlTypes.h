#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "DatagramTunneler.h"

namespace control {

// Stable lifecycle vocabulary shared by future CLI, daemon, and UI adapters.
enum class TunnelState {
    Stopped,
    Starting,
    Running,
    Stopping,
    Failed,
};

enum class RuntimeKind {
    Tunnel,
    Producer,
};

enum class EventKind {
    Lifecycle,
    Log,
    Metrics,
};

enum class EventSeverity {
    Info,
    Warning,
    Error,
};

struct TunnelMetrics {
    std::uint64_t datagram_count = 0;
    std::uint64_t byte_count = 0;
    double throughput_bytes_per_second = 0.0;
    std::optional<double> average_latency_milliseconds;
    std::optional<double> p50_latency_milliseconds;
    std::optional<double> p99_latency_milliseconds;
    std::optional<double> maximum_latency_milliseconds;
};

struct TunnelSnapshot {
    std::string alias;
    RuntimeKind kind = RuntimeKind::Tunnel;
    TunnelState state = TunnelState::Stopped;
    TunnelMetrics metrics;
    std::string detail;
};

struct ControlEvent {
    EventKind kind = EventKind::Lifecycle;
    EventSeverity severity = EventSeverity::Info;
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();
    std::string alias;
    std::string message;
    std::optional<TunnelSnapshot> snapshot;
};

// Adapters subscribe to structured events instead of consuming terminal text.
class EventSink {
public:
    virtual ~EventSink() = default;
    virtual void publish(const ControlEvent& event) = 0;
};

struct TunnelSummary {
    std::string alias;
    std::string mode;
    std::string udp_destination;
    std::string equivalent_direct_command;
};

} // namespace control
