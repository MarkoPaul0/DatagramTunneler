#include "CommandLine.h"

#include <cassert>
#include <cstdint>
#include <getopt.h>
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
    static option long_options[] = {
        {"server",   no_argument,       0, 's'},
        {"client",   no_argument,       0, 'c'},
        {"udpiface", required_argument, 0, 'i'},
        {"tcpiface", required_argument, 0, 'j'},
        {"udpgroup", required_argument, 0, 'u'},
        {"tcpsrv",   required_argument, 0, 't'}
    };

    int c = 0;
    bool side_selected = false;
    cfg->use_clt_grp_ = true;
    while (c >= 0) {
        int opt_index = 0;
        if ((c = getopt_long(argc, argv, "sci:j:u:t:", long_options, &opt_index)) < 0) {
            break;
        }
        switch (c) {
        case 's': {
            INFO("Mode:                        server");
            cfg->is_client_ = false;
            side_selected = true;
            break;
        }
        case 'c': {
            INFO("Mode:                        client");
            cfg->is_client_ = true;
            side_selected = true;
            break;
        }
        case 'i': {
            INFO("UDP interface:               %s", optarg);
            cfg->udp_iface_ip_ = optarg;
            break;
        }
        case 'j': {
            INFO("TCP interface:               %s", optarg);
            WARN("TCP interface selection is not supported yet");
            cfg->tcp_iface_ip_ = optarg;
            break;
        }
        case 'u': {
            INFO("UDP destination IP and port: %s", optarg);
            cfg->use_clt_grp_ = false;
            const std::string ip_port_arg(optarg);
            getIpAndPort(ip_port_arg, &cfg->udp_dst_ip_, &cfg->udp_dst_port_);
            break;
        }
        case 't': {
            assert(side_selected);
            const std::string ip_port_arg(optarg);
            if (cfg->is_client_) {
                INFO("TCP server IP and port:      %s", optarg);
                getIpAndPort(ip_port_arg, &cfg->tcp_srv_ip_, &cfg->tcp_srv_port_);
            } else {
                INFO("TCP server listen port:      %s", optarg);
                cfg->tcp_srv_port_ = getPort(ip_port_arg);
            }
            break;
        }
        default:
            DEATH("Invalid option %d", c);
        }
        if (!side_selected) {
            DEATH("Firt argument needs to be selection of server or client side! (--server or --client)");
        }
    }

    if (optind < argc) {
        while (optind < argc) {
            ERROR("Unkown argument %s", argv[optind++]);
        }
        exit(1);
    }
    if (!cfg->isComplete()) {
        return false;
    }
    if (!cfg->is_client_ && cfg->use_clt_grp_) {
        WARN("The server is set to publish tunneled packets on the same multicast group joined by the client."
             "This is dangerous if both client and server are on the same subnet.\nPress Ctrl-C to quit or any key to continue");
        getchar();
    }
    printf("\n");
    return true;
}
