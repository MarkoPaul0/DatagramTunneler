#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "CommandLine.h"
#include "Configuration.h"
#include "Log.h"
#include "LiveOutput.h"
#include "Network.h"
#include "Producer.h"
#include "control/ControlService.h"

#ifndef DGRAMTUNNELER_VERSION
#define DGRAMTUNNELER_VERSION "development"
#endif


static void printUsage(const char* binary_name) {
    printf("Usage:\n");
    printf("Named tunnels:\n");
    printf("    %s config init [--config <path>]\n", binary_name);
    printf("    %s config path [--config <path>]\n", binary_name);
    printf("    %s config edit [--config <path>]\n", binary_name);
    printf("    %s tunnel list [--config <path>]\n", binary_name);
    printf("    %s tunnel show <alias> [--config <path>]\n", binary_name);
    printf("    %s tunnel validate [alias] [--config <path>]\n", binary_name);
    printf("    %s tunnel run <alias> [--compact] [--config <path>]\n", binary_name);
    printf("    %s producer <client-alias> [--compact] [--config <path>] [--interval-ms <ms>] [--count <n>] [--payload-prefix <text>]\n", binary_name);
    printf("\nDirect invocation:\n");
    printf("Server mode:\n");
    printf("    %s --server -i <udp_iface_ip> -t <tcp_listen_port> [-u <udp_dst_ip>:<port>] [--compact]\n", binary_name);
    printf("Client mode:\n");
    printf("    %s --client -i <udp_iface_ip> -t <tcp_srv_ip>:<tcp_srv_port> -u <udp_dst_ip>:<port> [--compact]\n", binary_name);
}


static void printVersion() {
    printf("dgramtunneler %s\n", DGRAMTUNNELER_VERSION);
}

namespace {

struct CommandArguments {
    std::filesystem::path config_path;
    std::vector<std::string> positional;
    bool compact_output = false;
};

CommandArguments parseCommandArguments(int argc, char* argv[], int start_index, bool allow_compact = false) {
    CommandArguments arguments;
    for (int index = start_index; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--config") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--config requires a file path");
            }
            arguments.config_path = argv[++index];
        } else if (argument == "--compact") {
            if (!allow_compact) {
                throw std::runtime_error("--compact is only supported by tunnel run");
            }
            arguments.compact_output = true;
        } else {
            arguments.positional.push_back(argument);
        }
    }
    if (arguments.config_path.empty()) {
        arguments.config_path = defaultConfigurationPath();
    }
    return arguments;
}

std::size_t parsePositiveSize(const std::string& value, const char* option) {
    try {
        std::size_t parsed_length = 0;
        const unsigned long long parsed = std::stoull(value, &parsed_length);
        if (parsed_length != value.size() || parsed == 0 ||
            parsed > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
            throw std::out_of_range("value");
        }
        return static_cast<std::size_t>(parsed);
    } catch (const std::exception&) {
        throw std::runtime_error(std::string(option) + " must be a positive integer");
    }
}

struct ProducerCommand {
    std::filesystem::path config_path;
    std::string alias;
    DatagramProducer::Options options;
    bool compact_output = false;
};

ProducerCommand parseProducerCommand(int argc, char* argv[]) {
    ProducerCommand command;
    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--config") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--config requires a file path");
            }
            command.config_path = argv[++index];
        } else if (argument == "--compact") {
            command.compact_output = true;
        } else if (argument == "--interval-ms") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--interval-ms requires a value");
            }
            const std::size_t interval = parsePositiveSize(argv[++index], "--interval-ms");
            if (interval > std::numeric_limits<unsigned int>::max()) {
                throw std::runtime_error("--interval-ms is too large");
            }
            command.options.interval_ms = static_cast<unsigned int>(interval);
        } else if (argument == "--count") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--count requires a value");
            }
            command.options.count = parsePositiveSize(argv[++index], "--count");
        } else if (argument == "--payload-prefix") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--payload-prefix requires a value");
            }
            command.options.payload_prefix = argv[++index];
        } else if (!argument.empty() && argument.front() == '-') {
            throw std::runtime_error("unknown producer option '" + argument + "'");
        } else if (command.alias.empty()) {
            command.alias = argument;
        } else {
            throw std::runtime_error("producer requires exactly one client alias");
        }
    }
    if (command.alias.empty()) {
        throw std::runtime_error("producer requires exactly one client alias");
    }
    if (command.config_path.empty()) {
        command.config_path = defaultConfigurationPath();
    }
    return command;
}

std::vector<std::string> splitEditorCommand(const std::string& command) {
    std::vector<std::string> arguments;
    std::string argument;
    char quote = '\0';
    bool escaped = false;
    for (const char character : command) {
        if (escaped) {
            argument += character;
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (quote != '\0') {
            if (character == quote) {
                quote = '\0';
            } else {
                argument += character;
            }
        } else if (character == '\'' || character == '"') {
            quote = character;
        } else if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            if (!argument.empty()) {
                arguments.push_back(std::move(argument));
                argument.clear();
            }
        } else {
            argument += character;
        }
    }
    if (escaped || quote != '\0') {
        throw std::runtime_error("VISUAL or EDITOR contains an incomplete escape or quote");
    }
    if (!argument.empty()) {
        arguments.push_back(std::move(argument));
    }
    if (arguments.empty()) {
        throw std::runtime_error("VISUAL or EDITOR must name an editor executable");
    }
    return arguments;
}

std::vector<std::string> editorCommand() {
    const char* const visual = std::getenv("VISUAL");
    if (visual != nullptr && *visual != '\0') {
        return splitEditorCommand(visual);
    }
    const char* const editor = std::getenv("EDITOR");
    if (editor != nullptr && *editor != '\0') {
        return splitEditorCommand(editor);
    }
#ifdef _WIN32
    return {"notepad.exe"};
#elif defined(__APPLE__)
    return {"open", "-W", "-e"};
#else
    return {"vi"};
#endif
}

void editConfiguration(const std::filesystem::path& path) {
    std::vector<std::string> command = editorCommand();
    command.push_back(path.string());
    std::vector<char*> arguments;
    arguments.reserve(command.size() + 1);
    for (std::string& argument : command) {
        arguments.push_back(argument.data());
    }
    arguments.push_back(nullptr);

#ifdef _WIN32
    const int exit_code = _spawnvp(_P_WAIT, arguments.front(), arguments.data());
    if (exit_code == -1) {
        throw std::runtime_error("could not launch the configured editor");
    }
    if (exit_code != 0) {
        throw std::runtime_error("editor exited with status " + std::to_string(exit_code));
    }
#else
    const pid_t child = fork();
    if (child == -1) {
        throw std::runtime_error("could not launch the configured editor");
    }
    if (child == 0) {
        execvp(arguments.front(), arguments.data());
        _Exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) == -1) {
        if (errno != EINTR) {
            throw std::runtime_error("could not wait for the configured editor");
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error("editor exited unsuccessfully");
    }
#endif
}

const char* modeName(const NamedTunnel& tunnel) {
    return tunnel.config.is_client_ ? "client" : "server";
}

std::string endpoint(const std::string& address, uint16_t port) {
    return address + ":" + std::to_string(port);
}

std::string compactTunnelContext(const DatagramTunneler::Config& config) {
    if (config.is_client_) {
        return "CLIENT " + endpoint(config.udp_dst_ip_, config.udp_dst_port_) + " -> TCP " +
               endpoint(config.tcp_srv_ip_, config.tcp_srv_port_);
    }
    const std::string destination = config.use_clt_grp_ ? "client group" : endpoint(config.udp_dst_ip_, config.udp_dst_port_);
    return "SERVER TCP :" + std::to_string(config.tcp_srv_port_) + " -> " + destination;
}

std::string compactProducerContext(const DatagramTunneler::Config& config, const DatagramProducer::Options& options) {
    return "PRODUCER -> " + endpoint(config.udp_dst_ip_, config.udp_dst_port_) + " @ " +
           std::to_string(options.interval_ms) + " ms";
}

void printTunnelList(const std::vector<control::TunnelSummary>& rows) {
    std::size_t alias_width = std::string("Alias").size();
    std::size_t mode_width = std::string("Mode").size();
    std::size_t destination_width = std::string("UDP group / destination").size();
    for (const control::TunnelSummary& row : rows) {
        alias_width = std::max(alias_width, row.alias.size());
        mode_width = std::max(mode_width, row.mode.size());
        destination_width = std::max(destination_width, row.udp_destination.size());
    }

    const int alias_padding = static_cast<int>(alias_width);
    const int mode_padding = static_cast<int>(mode_width);
    const int destination_padding = static_cast<int>(destination_width);
    printf("%-*s  %-*s  %-*s  %s\n", alias_padding, "Alias", mode_padding, "Mode",
           destination_padding, "UDP group / destination", "Equivalent direct command");
    printf("%-*s  %-*s  %-*s  %s\n", alias_padding, "-----", mode_padding, "----",
           destination_padding, "-----------------------", "-------------------------");
    for (const control::TunnelSummary& row : rows) {
        printf("%-*s  %-*s  %-*s  %s\n", alias_padding, row.alias.c_str(), mode_padding,
               row.mode.c_str(), destination_padding, row.udp_destination.c_str(), row.equivalent_direct_command.c_str());
    }
}

void printTunnel(const NamedTunnel& tunnel) {
    printf("%s (%s)\n", tunnel.alias.c_str(), modeName(tunnel));
    printf("  udp_interface = %s\n", tunnel.config.udp_iface_ip_.c_str());
    if (tunnel.config.is_client_) {
        printf("  udp_group = %s:%u\n", tunnel.config.udp_dst_ip_.c_str(), static_cast<unsigned int>(tunnel.config.udp_dst_port_));
        printf("  tcp_server = %s:%u\n", tunnel.config.tcp_srv_ip_.c_str(), static_cast<unsigned int>(tunnel.config.tcp_srv_port_));
    } else {
        printf("  tcp_listen_port = %u\n", static_cast<unsigned int>(tunnel.config.tcp_srv_port_));
        if (tunnel.config.use_clt_grp_) {
            printf("  udp_destination = %s\n", kReplicateClientDestination.data());
        } else {
            printf("  udp_destination = %s:%u\n", tunnel.config.udp_dst_ip_.c_str(), static_cast<unsigned int>(tunnel.config.udp_dst_port_));
        }
    }
}

int runConfigCommand(int argc, char* argv[]) {
    const CommandArguments arguments = parseCommandArguments(argc, argv, 2);
    if (arguments.positional.size() != 1) {
        throw std::runtime_error("config requires exactly one subcommand: init, path, or edit");
    }

    const std::string& subcommand = arguments.positional.front();
    if (subcommand == "path") {
        printf("%s\n", arguments.config_path.string().c_str());
        return 0;
    }
    if (subcommand == "init") {
        writeSampleConfiguration(arguments.config_path);
        INFO("Created sample configuration at %s", arguments.config_path.string().c_str());
        return 0;
    }
    if (subcommand == "edit") {
        if (!std::filesystem::exists(arguments.config_path)) {
            writeSampleConfiguration(arguments.config_path);
            INFO("Created sample configuration at %s", arguments.config_path.string().c_str());
        }
        editConfiguration(arguments.config_path);
        return 0;
    }
    throw std::runtime_error("unknown config subcommand '" + subcommand + "'");
}

int runTunnelCommand(int argc, char* argv[]) {
    const CommandArguments arguments = parseCommandArguments(argc, argv, 2, true);
    if (arguments.positional.empty()) {
        throw std::runtime_error("tunnel requires a subcommand: list, show, validate, or run");
    }

    const std::string& subcommand = arguments.positional.front();
    if (arguments.compact_output && subcommand != "run") {
        throw std::runtime_error("--compact is only supported by tunnel run");
    }
    if (subcommand == "list") {
        if (arguments.positional.size() != 1) {
            throw std::runtime_error("tunnel list does not accept an alias");
        }
        const control::ControlService control_service(arguments.config_path);
        printTunnelList(control_service.listTunnels());
        return 0;
    }

    if (subcommand == "validate") {
        if (arguments.positional.size() > 2) {
            throw std::runtime_error("tunnel validate accepts at most one alias");
        }
        const control::ControlService control_service(arguments.config_path);
        if (arguments.positional.size() == 2) {
            const NamedTunnel tunnel = control_service.tunnel(arguments.positional[1]);
            INFO("Tunnel '%s' is valid (%s)", tunnel.alias.c_str(), modeName(tunnel));
        } else {
            const std::vector<control::TunnelSummary> tunnels = control_service.listTunnels();
            INFO("Configuration is valid (%zu tunnel%s)", tunnels.size(),
                 tunnels.size() == 1 ? "" : "s");
        }
        return 0;
    }

    if (arguments.positional.size() != 2) {
        throw std::runtime_error("tunnel " + subcommand + " requires exactly one alias");
    }
    const control::ControlService control_service(arguments.config_path);
    const NamedTunnel tunnel = control_service.tunnel(arguments.positional[1]);
    if (subcommand == "show") {
        printTunnel(tunnel);
        return 0;
    }
    if (subcommand == "run") {
        DatagramTunneler::Config config = tunnel.config;
        config.compact_output_ = arguments.compact_output;
        configureCompactOutput(config.compact_output_, compactTunnelContext(config));
        DatagramTunneler tunneler(std::move(config));
        tunneler.run();
        return 0;
    }
    throw std::runtime_error("unknown tunnel subcommand '" + subcommand + "'");
}

int runProducerCommand(int argc, char* argv[]) {
    const ProducerCommand command = parseProducerCommand(argc, argv);
    const control::ControlService control_service(command.config_path);
    const NamedTunnel tunnel = control_service.tunnel(command.alias);
    if (!tunnel.config.is_client_) {
        throw std::runtime_error("producer alias '" + tunnel.alias + "' must name a client tunnel");
    }
    configureCompactOutput(command.compact_output, compactProducerContext(tunnel.config, command.options));
    DatagramProducer producer(tunnel.config, command.options);
    producer.run();
    return 0;
}

} // namespace


int main(int argc, char* argv[]) {
    if (argc == 2 && (std::string(argv[1]) == "--version" || std::string(argv[1]) == "-V")) {
        printVersion();
        return 0;
    }

    int network_error = 0;
    if (!initializeNetwork(&network_error)) {
        ERROR("Could not initialize network support. Error %d", network_error);
        return 1;
    }

    try {
        if (argc >= 2 && std::string(argv[1]) == "config") {
            return runConfigCommand(argc, argv);
        }
        if (argc >= 2 && std::string(argv[1]) == "tunnel") {
            return runTunnelCommand(argc, argv);
        }
        if (argc >= 2 && std::string(argv[1]) == "producer") {
            return runProducerCommand(argc, argv);
        }
    } catch (const std::exception& error) {
        ERROR("%s", error.what());
        return 1;
    }

    // Parse command line config
    DatagramTunneler::Config cfg;
    for (int index = 1; index < argc; ++index) {
        if (std::string(argv[index]) == "--compact") {
            configureCompactOutput(true);
            break;
        }
    }
    if (!parseCommandLineConfig(argc, argv, &cfg)) {
        printUsage(argv[0]);
        return 1;
    }

    // Create and run the datagram tunneler with the parsed config
    configureCompactOutput(cfg.compact_output_, compactTunnelContext(cfg));
    DatagramTunneler tunneler(std::move(cfg));
    tunneler.run();

    INFO("Exiting program");
    return 0;
}
