#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "control/ControlTypes.h"

namespace control::api {

// The HTTP server added in Task 31 implements this contract on loopback only.
inline constexpr std::string_view kVersion = "v1";
inline constexpr std::string_view kBasePath = "/api/v1";
inline constexpr std::string_view kHealthPath = "/api/v1/health";
inline constexpr std::string_view kTunnelsPath = "/api/v1/tunnels";
inline constexpr std::string_view kRuntimesPath = "/api/v1/runtimes";
inline constexpr std::string_view kConfigPath = "/api/v1/config";
inline constexpr std::string_view kEventsPath = "/api/v1/events";

enum class ErrorCode {
    InvalidRequest,
    NotFound,
    Conflict,
    ValidationFailed,
    Internal,
};

struct ErrorResponse {
    ErrorCode code = ErrorCode::Internal;
    std::string message;
};

struct HealthResponse {
    std::string service = "dgramtunneler";
    std::string api_version = std::string(kVersion);
    bool ready = true;
};

struct TunnelListResponse {
    std::vector<TunnelSummary> tunnels;
};

struct TunnelResponse {
    TunnelSummary summary;
    DatagramTunneler::Config configuration;
};

struct RuntimeListResponse {
    std::vector<TunnelSnapshot> runtimes;
};

struct ProducerStartRequest {
    std::uint32_t interval_milliseconds = 1000;
    std::optional<std::uint64_t> count;
    std::string payload_prefix = "Dummy datagram";
};

struct ConfigResponse {
    std::string toml;
};

struct ConfigUpdateRequest {
    std::string toml;
};

struct EventMessage {
    std::string api_version = std::string(kVersion);
    ControlEvent event;
};

inline std::string tunnelPath(std::string_view alias) {
    return std::string(kTunnelsPath) + "/" + std::string(alias);
}

inline std::string tunnelActionPath(std::string_view alias, std::string_view action) {
    return tunnelPath(alias) + "/" + std::string(action);
}

inline std::string producerActionPath(std::string_view alias, std::string_view action) {
    return std::string(kTunnelsPath) + "/" + std::string(alias) + "/producer/" + std::string(action);
}

} // namespace control::api
