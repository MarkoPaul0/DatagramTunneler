#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <filesystem>
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
#include "Network.h"

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
    printf("    %s tunnel run <alias> [--config <path>]\n", binary_name);
    printf("\nDirect invocation:\n");
    printf("Server mode:\n");
    printf("    %s --server -i <udp_iface_ip> -t <tcp_listen_port> [-u <udp_dst_ip>:<port>]\n", binary_name);
    printf("Client mode:\n");
    printf("    %s --client -i <udp_iface_ip> -t <tcp_srv_ip>:<tcp_srv_port> -u <udp_dst_ip>:<port>\n", binary_name);
}


static void printVersion() {
    printf("dgramtunneler %s\n", DGRAMTUNNELER_VERSION);
}

namespace {

struct CommandArguments {
    std::filesystem::path config_path;
    std::vector<std::string> positional;
};

CommandArguments parseCommandArguments(int argc, char* argv[], int start_index) {
    CommandArguments arguments;
    for (int index = start_index; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--config") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--config requires a file path");
            }
            arguments.config_path = argv[++index];
        } else {
            arguments.positional.push_back(argument);
        }
    }
    if (arguments.config_path.empty()) {
        arguments.config_path = defaultConfigurationPath();
    }
    return arguments;
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

void printTunnel(const NamedTunnel& tunnel) {
    printf("%s (%s)\n", tunnel.alias.c_str(), modeName(tunnel));
    printf("  udp_interface = %s\n", tunnel.config.udp_iface_ip_.c_str());
    if (tunnel.config.is_client_) {
        printf("  udp_group = %s:%u\n", tunnel.config.udp_dst_ip_.c_str(), static_cast<unsigned int>(tunnel.config.udp_dst_port_));
        printf("  tcp_server = %s:%u\n", tunnel.config.tcp_srv_ip_.c_str(), static_cast<unsigned int>(tunnel.config.tcp_srv_port_));
    } else {
        printf("  tcp_listen_port = %u\n", static_cast<unsigned int>(tunnel.config.tcp_srv_port_));
        if (!tunnel.config.use_clt_grp_) {
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
    const CommandArguments arguments = parseCommandArguments(argc, argv, 2);
    if (arguments.positional.empty()) {
        throw std::runtime_error("tunnel requires a subcommand: list, show, validate, or run");
    }

    const std::string& subcommand = arguments.positional.front();
    if (subcommand == "list") {
        if (arguments.positional.size() != 1) {
            throw std::runtime_error("tunnel list does not accept an alias");
        }
        const TunnelConfiguration configuration = loadConfiguration(arguments.config_path);
        for (const NamedTunnel& tunnel : configuration.tunnels) {
            printf("%s\t%s\n", tunnel.alias.c_str(), modeName(tunnel));
        }
        return 0;
    }

    if (subcommand == "validate") {
        if (arguments.positional.size() > 2) {
            throw std::runtime_error("tunnel validate accepts at most one alias");
        }
        const TunnelConfiguration configuration = loadConfiguration(arguments.config_path);
        if (arguments.positional.size() == 2) {
            const NamedTunnel& tunnel = findTunnel(configuration, arguments.positional[1]);
            INFO("Tunnel '%s' is valid (%s)", tunnel.alias.c_str(), modeName(tunnel));
        } else {
            INFO("Configuration is valid (%zu tunnel%s)", configuration.tunnels.size(),
                 configuration.tunnels.size() == 1 ? "" : "s");
        }
        return 0;
    }

    if (arguments.positional.size() != 2) {
        throw std::runtime_error("tunnel " + subcommand + " requires exactly one alias");
    }
    const TunnelConfiguration configuration = loadConfiguration(arguments.config_path);
    const NamedTunnel& tunnel = findTunnel(configuration, arguments.positional[1]);
    if (subcommand == "show") {
        printTunnel(tunnel);
        return 0;
    }
    if (subcommand == "run") {
        DatagramTunneler tunneler(tunnel.config);
        tunneler.run();
        return 0;
    }
    throw std::runtime_error("unknown tunnel subcommand '" + subcommand + "'");
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
    } catch (const std::exception& error) {
        ERROR("%s", error.what());
        return 1;
    }

    // Parse command line config
    DatagramTunneler::Config cfg;
    if (!parseCommandLineConfig(argc, argv, &cfg)) {
        printUsage(argv[0]);
        return 1;
    }

    // Create and run the datagram tunneler with the parsed config
    DatagramTunneler tunneler(std::move(cfg));
    tunneler.run();

    INFO("Exiting program");
    return 0;
}
