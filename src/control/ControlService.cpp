#include "control/ControlService.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace control {
namespace {

std::string endpoint(const std::string& address, uint16_t port) {
    return address + ":" + std::to_string(port);
}

std::string modeName(const NamedTunnel& tunnel) {
    return tunnel.config.is_client_ ? "client" : "server";
}

std::string udpDestination(const NamedTunnel& tunnel) {
    if (!tunnel.config.is_client_ && tunnel.config.use_clt_grp_) {
        return std::string(kReplicateClientDestination);
    }
    return endpoint(tunnel.config.udp_dst_ip_, tunnel.config.udp_dst_port_);
}

std::string directCommand(const NamedTunnel& tunnel) {
    const DatagramTunneler::Config& config = tunnel.config;
    if (config.is_client_) {
        return "dgramtunneler --client -i " + (config.udp_iface_reference_.empty() ? config.udp_iface_ip_ : config.udp_iface_reference_) + " -t " +
               endpoint(config.tcp_srv_ip_, config.tcp_srv_port_) + " -u " +
               endpoint(config.udp_dst_ip_, config.udp_dst_port_);
    }

    std::string command = "dgramtunneler --server -i " + (config.udp_iface_reference_.empty() ? config.udp_iface_ip_ : config.udp_iface_reference_) +
                          " -t " + std::to_string(config.tcp_srv_port_);
    if (!config.use_clt_grp_) {
        command += " -u " + endpoint(config.udp_dst_ip_, config.udp_dst_port_);
    }
    return command;
}

} // namespace

ControlService::ControlService(std::filesystem::path configuration_path)
    : configuration_path_(std::move(configuration_path)), configuration_(loadConfiguration(configuration_path_)) {}

const std::filesystem::path& ControlService::configurationPath() const {
    return configuration_path_;
}

std::vector<TunnelSummary> ControlService::listTunnels() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TunnelSummary> summaries;
    summaries.reserve(configuration_.tunnels.size());
    for (const NamedTunnel& named_tunnel : configuration_.tunnels) {
        summaries.push_back({named_tunnel.alias, modeName(named_tunnel), udpDestination(named_tunnel), directCommand(named_tunnel)});
    }
    return summaries;
}

NamedTunnel ControlService::tunnel(std::string_view alias) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return findTunnel(configuration_, std::string(alias));
}

void ControlService::validate(std::string_view alias) const {
    if (!alias.empty()) {
        static_cast<void>(tunnel(alias));
    }
}

std::string ControlService::configurationToml() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream input(configuration_path_);
    if (!input) {
        throw std::runtime_error("could not read configuration at " + configuration_path_.string());
    }
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

void ControlService::replaceConfiguration(std::string toml) {
    std::istringstream input(toml);
    TunnelConfiguration replacement = parseConfiguration(input);
    const std::lock_guard<std::mutex> lock(mutex_);
    const std::filesystem::path temporary_path = configuration_path_.string() + ".tmp";
    {
        std::ofstream output(temporary_path, std::ios::trunc);
        if (!output) {
            throw std::runtime_error("could not write configuration at " + temporary_path.string());
        }
        output << toml;
        if (!output) {
            throw std::runtime_error("could not write configuration at " + temporary_path.string());
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary_path, configuration_path_, error);
#ifdef _WIN32
    if (error) {
        error.clear();
        std::filesystem::remove(configuration_path_, error);
        if (!error) {
            std::filesystem::rename(temporary_path, configuration_path_, error);
        }
    }
#endif
    if (error) {
        std::filesystem::remove(temporary_path);
        throw std::runtime_error("could not replace configuration at " + configuration_path_.string());
    }
    configuration_ = std::move(replacement);
}

} // namespace control
