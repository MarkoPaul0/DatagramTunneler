#include "Configuration.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

#include "Network.h"

namespace {

using TunnelFields = std::map<std::string, std::string>;

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) {
        return std::isspace(character) != 0;
    }).base();
    return first < last ? std::string(first, last) : std::string();
}

std::string removeComment(const std::string& line) {
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        if (line[index] == '"') {
            quoted = !quoted;
        } else if (line[index] == '#' && !quoted) {
            return line.substr(0, index);
        }
    }
    return line;
}

bool isAlias(const std::string& alias) {
    return !alias.empty() && alias.size() <= 64 && std::all_of(alias.begin(), alias.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '-' || character == '_';
    });
}

bool isIpv4Address(const std::string& address) {
    in_addr parsed {};
    return inet_pton(AF_INET, address.c_str(), &parsed) == 1;
}

uint16_t parsePort(const std::string& value, const std::string& field_name) {
    try {
        const unsigned long port = std::stoul(value);
        if (port == 0 || port > UINT16_MAX) {
            throw std::out_of_range("port");
        }
        return static_cast<uint16_t>(port);
    } catch (const std::exception&) {
        throw std::runtime_error(field_name + " must be a port in the range 1-65535");
    }
}

std::pair<std::string, uint16_t> parseEndpoint(const std::string& value, const std::string& field_name) {
    const std::size_t separator = value.rfind(':');
    if (separator == std::string::npos || separator == 0 || separator + 1 == value.size()) {
        throw std::runtime_error(field_name + " must be an IPv4 address followed by :port");
    }
    const std::string address = value.substr(0, separator);
    if (!isIpv4Address(address)) {
        throw std::runtime_error(field_name + " must contain a valid IPv4 address");
    }
    return {address, parsePort(value.substr(separator + 1), field_name)};
}

std::string parseValue(const std::string& value, const std::string& field_name) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        const std::string parsed = value.substr(1, value.size() - 2);
        if (parsed.find('"') != std::string::npos) {
            throw std::runtime_error(field_name + " contains an unsupported quote character");
        }
        return parsed;
    }
    if (!value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isdigit(character) != 0;
    })) {
        return value;
    }
    throw std::runtime_error(field_name + " must be a quoted string or integer");
}

const std::string& requiredField(const TunnelFields& fields, const std::string& name, const std::string& alias) {
    const auto field = fields.find(name);
    if (field == fields.end() || field->second.empty()) {
        throw std::runtime_error("tunnel '" + alias + "' is missing required field '" + name + "'");
    }
    return field->second;
}

NamedTunnel makeTunnel(const std::string& alias, const TunnelFields& fields) {
    static const std::vector<std::string> allowed_fields = {
        "mode", "udp_interface", "udp_group", "tcp_server", "tcp_listen_port", "udp_destination"
    };
    for (const auto& field : fields) {
        if (std::find(allowed_fields.begin(), allowed_fields.end(), field.first) == allowed_fields.end()) {
            throw std::runtime_error("tunnel '" + alias + "' has unknown field '" + field.first + "'");
        }
    }

    NamedTunnel tunnel;
    tunnel.alias = alias;
    tunnel.config.udp_iface_reference_ = requiredField(fields, "udp_interface", alias);
    const auto interface_address = resolveInterfaceIpv4(tunnel.config.udp_iface_reference_);
    if (!interface_address.has_value()) {
        throw std::runtime_error("tunnel '" + alias + "' has an invalid udp_interface; use a local interface name or IPv4 address");
    }
    tunnel.config.udp_iface_ip_ = *interface_address;

    const std::string& mode = requiredField(fields, "mode", alias);
    if (mode == "client") {
        if (fields.contains("tcp_listen_port") || fields.contains("udp_destination")) {
            throw std::runtime_error("client tunnel '" + alias + "' contains server-only fields");
        }
        tunnel.config.is_client_ = true;
        const auto [server_address, server_port] = parseEndpoint(requiredField(fields, "tcp_server", alias), "tcp_server");
        const auto [group_address, group_port] = parseEndpoint(requiredField(fields, "udp_group", alias), "udp_group");
        tunnel.config.tcp_srv_ip_ = server_address;
        tunnel.config.tcp_srv_port_ = server_port;
        tunnel.config.udp_dst_ip_ = group_address;
        tunnel.config.udp_dst_port_ = group_port;
        tunnel.config.use_clt_grp_ = false;
    } else if (mode == "server") {
        if (fields.contains("tcp_server") || fields.contains("udp_group")) {
            throw std::runtime_error("server tunnel '" + alias + "' contains client-only fields");
        }
        tunnel.config.is_client_ = false;
        tunnel.config.tcp_srv_port_ = parsePort(requiredField(fields, "tcp_listen_port", alias), "tcp_listen_port");
        const auto destination = fields.find("udp_destination");
        tunnel.config.use_clt_grp_ = destination == fields.end() || destination->second == kReplicateClientDestination;
        if (!tunnel.config.use_clt_grp_) {
            const auto [address, port] = parseEndpoint(destination->second, "udp_destination");
            tunnel.config.udp_dst_ip_ = address;
            tunnel.config.udp_dst_port_ = port;
        }
    } else {
        throw std::runtime_error("tunnel '" + alias + "' has unsupported mode '" + mode + "'");
    }

    if (!tunnel.config.isComplete()) {
        throw std::runtime_error("tunnel '" + alias + "' is incomplete");
    }
    return tunnel;
}

} // namespace


std::filesystem::path defaultConfigurationPath() {
#ifdef _WIN32
    const char* const app_data = std::getenv("APPDATA");
    if (app_data != nullptr && *app_data != '\0') {
        return std::filesystem::path(app_data) / "DatagramTunneler" / "config.toml";
    }
#elif defined(__APPLE__)
    const char* const home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / "Library" / "Application Support" / "DatagramTunneler" / "config.toml";
    }
#else
    const char* const config_home = std::getenv("XDG_CONFIG_HOME");
    if (config_home != nullptr && *config_home != '\0') {
        return std::filesystem::path(config_home) / "dgramtunneler" / "config.toml";
    }
    const char* const home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config" / "dgramtunneler" / "config.toml";
    }
#endif
    throw std::runtime_error("could not determine the default configuration directory; use --config <path>");
}


TunnelConfiguration parseConfiguration(std::istream& input) {
    int version = 0;
    std::map<std::string, TunnelFields> tunnel_fields;
    std::string active_alias;
    std::string line;
    std::size_t line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;
        const std::string content = trim(removeComment(line));
        if (content.empty()) {
            continue;
        }
        if (content.front() == '[' && content.back() == ']') {
            const std::string section = content.substr(1, content.size() - 2);
            const std::string prefix = "tunnels.";
            if (section.compare(0, prefix.size(), prefix) != 0 || !isAlias(section.substr(prefix.size()))) {
                throw std::runtime_error("line " + std::to_string(line_number) + " must name a tunnel as [tunnels.<alias>]");
            }
            active_alias = section.substr(prefix.size());
            if (tunnel_fields.find(active_alias) != tunnel_fields.end()) {
                throw std::runtime_error("duplicate tunnel alias '" + active_alias + "'");
            }
            tunnel_fields.emplace(active_alias, TunnelFields {});
            continue;
        }

        const std::size_t separator = content.find('=');
        if (separator == std::string::npos) {
            throw std::runtime_error("line " + std::to_string(line_number) + " must be a key/value assignment");
        }
        const std::string key = trim(content.substr(0, separator));
        const std::string value = parseValue(trim(content.substr(separator + 1)), key);
        if (active_alias.empty()) {
            if (key != "version" || version != 0 || value != "1") {
                throw std::runtime_error("line " + std::to_string(line_number) + " must set version = 1 before tunnel sections");
            }
            version = 1;
        } else if (!tunnel_fields[active_alias].emplace(key, value).second) {
            throw std::runtime_error("tunnel '" + active_alias + "' defines '" + key + "' more than once");
        }
    }

    if (version != 1) {
        throw std::runtime_error("configuration must begin with version = 1");
    }

    TunnelConfiguration configuration;
    for (const auto& fields : tunnel_fields) {
        configuration.tunnels.push_back(makeTunnel(fields.first, fields.second));
    }
    return configuration;
}


TunnelConfiguration loadConfiguration(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open configuration file '" + path.string() + "'");
    }
    return parseConfiguration(input);
}


void writeSampleConfiguration(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) {
        throw std::runtime_error("configuration file already exists at '" + path.string() + "'");
    }
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("could not create configuration file '" + path.string() + "'");
    }
    output << R"(version = 1

# Replace these values with local interface names or IPv4 addresses.
[tunnels.example-client]
mode = "client"
udp_interface = "192.168.1.20"
udp_group = "239.1.2.3:5000"
tcp_server = "192.168.1.10:14052"

[tunnels.example-server]
mode = "server"
udp_interface = "192.168.1.10"
tcp_listen_port = 14052
udp_destination = "replicate_client"
)";
}


const NamedTunnel& findTunnel(const TunnelConfiguration& configuration, const std::string& alias) {
    const auto tunnel = std::find_if(configuration.tunnels.begin(), configuration.tunnels.end(), [&alias](const NamedTunnel& candidate) {
        return candidate.alias == alias;
    });
    if (tunnel == configuration.tunnels.end()) {
        throw std::runtime_error("no tunnel named '" + alias + "'");
    }
    return *tunnel;
}
