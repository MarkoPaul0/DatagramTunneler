#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

#include "Configuration.h"
#include "control/ControlTypes.h"

namespace control {

// Read-only named-tunnel catalog shared by the current CLI and future control
// adapters. Runtime ownership is intentionally deferred to TunnelRuntime.
class ControlService {
public:
    explicit ControlService(std::filesystem::path configuration_path);

    const std::filesystem::path& configurationPath() const;
    std::vector<TunnelSummary> listTunnels() const;
    NamedTunnel tunnel(std::string_view alias) const;
    void validate(std::string_view alias = {}) const;

private:
    std::filesystem::path configuration_path_;
    TunnelConfiguration configuration_;
};

} // namespace control
