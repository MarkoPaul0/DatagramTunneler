#include "control/ControlService.h"

#include <cstdint>
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
        return "dgramtunneler --client -i " + config.udp_iface_ip_ + " -t " +
               endpoint(config.tcp_srv_ip_, config.tcp_srv_port_) + " -u " +
               endpoint(config.udp_dst_ip_, config.udp_dst_port_);
    }

    std::string command = "dgramtunneler --server -i " + config.udp_iface_ip_ +
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
    std::vector<TunnelSummary> summaries;
    summaries.reserve(configuration_.tunnels.size());
    for (const NamedTunnel& named_tunnel : configuration_.tunnels) {
        summaries.push_back({named_tunnel.alias, modeName(named_tunnel), udpDestination(named_tunnel), directCommand(named_tunnel)});
    }
    return summaries;
}

NamedTunnel ControlService::tunnel(std::string_view alias) const {
    return findTunnel(configuration_, std::string(alias));
}

void ControlService::validate(std::string_view alias) const {
    if (!alias.empty()) {
        static_cast<void>(tunnel(alias));
    }
}

} // namespace control
