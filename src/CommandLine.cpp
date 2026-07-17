#include "CommandLine.h"

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "Log.h"

namespace {

uint16_t getPort(const std::string& port_arg) {
    try {
        const int port_int = std::stoi(port_arg); //TODO: handle exception
        if (port_int < 0 || port_int > UINT16_MAX) {
            DEATH("Invalid port %d!", port_int);
        }
        return static_cast<uint16_t>(port_int);
    } catch (std::invalid_argument& ex) {
        DEATH("Invalid port %s!", port_arg.c_str());
    }
}


void getIpAndPort(const std::string& ip_port_arg, std::string* ip_out, uint16_t* port_out) {
    const std::size_t pos = ip_port_arg.find(':');

    // Case where no port is provided
    if (pos == std::string::npos || pos == ip_port_arg.size() - 1) {
        DEATH("Invalid ip-port argument '%s'.", ip_port_arg.c_str());
    }

    // Case where ip and port are provided
    *ip_out = ip_port_arg.substr(0, pos);
    *port_out = getPort(ip_port_arg.substr(pos + 1));
}

} // namespace


bool parseCommandLineConfig(int argc, char* argv[], DatagramTunneler::Config* const cfg) {
    bool side_selected = false;
    cfg->use_clt_grp_ = true;
    for (int index = 1; index < argc; ++index) {
        const std::string option(argv[index]);
        if (option == "--server" || option == "-s") {
            INFO("Mode:                        server");
            cfg->is_client_ = false;
            side_selected = true;
        } else if (option == "--client" || option == "-c") {
            INFO("Mode:                        client");
            cfg->is_client_ = true;
            side_selected = true;
        } else if (option == "--verbose") {
            cfg->verbose_output_ = true;
        } else {
            if (!side_selected) {
                DEATH("First argument needs to select server or client mode! (--server or --client)");
            }
            if (index + 1 >= argc) {
                DEATH("Option %s requires an argument.", option.c_str());
            }
            const char* const argument = argv[++index];
            if (option == "--udpiface" || option == "-i") {
                INFO("UDP interface:               %s", argument);
                cfg->udp_iface_ip_ = argument;
                cfg->udp_iface_reference_ = argument;
            } else if (option == "--tcpiface" || option == "-j") {
                INFO("TCP interface:               %s", argument);
                cfg->tcp_iface_ip_ = argument;
            WARN("TCP interface selection is not supported yet");
            } else if (option == "--udpgroup" || option == "-u") {
                INFO("UDP destination IP and port: %s", argument);
                cfg->use_clt_grp_ = false;
                const std::string ip_port_arg(argument);
                getIpAndPort(ip_port_arg, &cfg->udp_dst_ip_, &cfg->udp_dst_port_);
            } else if (option == "--tcpsrv" || option == "-t") {
                const std::string ip_port_arg(argument);
                if (cfg->is_client_) {
                    INFO("TCP server IP and port:      %s", argument);
                    getIpAndPort(ip_port_arg, &cfg->tcp_srv_ip_, &cfg->tcp_srv_port_);
                } else {
                    INFO("TCP server listen port:      %s", argument);
                    cfg->tcp_srv_port_ = getPort(ip_port_arg);
                }
            } else {
                DEATH("Invalid option %s", option.c_str());
            }
        }
    }

    if (!cfg->isComplete()) {
        return false;
    }
    if (!cfg->is_client_ && cfg->use_clt_grp_) {
        WARN("The server is set to publish tunneled packets on the same multicast group joined by the client."
             "This is dangerous if both client and server are on the same subnet.\nPress Ctrl-C to quit or any key to continue");
        getchar();
    }
    return true;
}
