#pragma once

#include <filesystem>
#include <istream>
#include <string>
#include <vector>

#include "DatagramTunneler.h"

struct NamedTunnel {
    std::string alias;
    DatagramTunneler::Config config;
};

struct TunnelConfiguration {
    std::vector<NamedTunnel> tunnels;
};

std::filesystem::path defaultConfigurationPath();
TunnelConfiguration parseConfiguration(std::istream& input);
TunnelConfiguration loadConfiguration(const std::filesystem::path& path);
void writeSampleConfiguration(const std::filesystem::path& path);
const NamedTunnel& findTunnel(const TunnelConfiguration& configuration, const std::string& alias);
