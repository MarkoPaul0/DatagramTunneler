#include "DatagramTunneler.h"
#include <getopt.h>
#include <string>

//Parses the command line arguments to populate the provided DatagramTunneler::Config
static void getCommandLineConfig(int argc, char* argv[], DatagramTunneler::Config* const cfg) {
    static struct option long_options[] = {  //TODO: static?
        {"server",   no_argument,       0, 's'},
        {"client",   no_argument,       0, 'c'},
        {"udpiface", required_argument, 0, 'i'},
        {"tcpiface", required_argument, 0, 'j'},
        {"udpdst",   required_argument, 0, 'u'},
        {"udpport",  required_argument, 0, 'p'},
        {"tcpip",    required_argument, 0, 't'},
        {"tcpport",  required_argument, 0, 'l'}
    };

    int c = 0;
    while (c >= 0) {
        int opt_index = 0;
        if ((c = getopt_long (argc, argv, "sci:j:u:p:t:l:", long_options, &opt_index)) < 0) {
            break;//end of options;
        }    
        switch (c)
        {
        case 's': {
            INFO("Mode:                 server");
            cfg->is_client_ = false;
            break;
        }
        case 'c': {
            INFO("Mode:                 client");
            cfg->is_client_ = true;
            break;
        }
        case 'i': {
            INFO("UDP interface:        %s", optarg) ;
            cfg->udp_iface_ip_ = optarg;
            break;
        }
        case 'j': {
            INFO("TCP interface:        %s", optarg);
            WARN("TCP interface selection is not supported yet"); //TODO: support interface seleciton
            cfg->tcp_iface_ip_ = optarg;
            break;
        }
        case 'u': {
            INFO("UDP destination IP:   %s", optarg);
            cfg->udp_dst_ip_ = optarg;
            break;
        }
        case 'p': {
            INFO("UDP destination port: %s", optarg);
            cfg->udp_dst_port_ = std::stoi(optarg);
            break;
        }
        case 't': {
            INFO("TCP server IP:        %s", optarg);
            cfg->tcp_srv_ip_ = optarg;
            break;
        }
        case 'l': {
            INFO("TCP server port:      %s", optarg);
            cfg->tcp_srv_port_ = std::stoi(optarg);
            break;
        }
        default:
            DEATH("Unexpected option %d", c);
        }
    }

    //If there are unkown options
    if (optind < argc) {
        while (optind < argc) {
            ERROR("Unkown argument %s", argv[optind++]);
        }
        exit(-1);
    }
    printf("\n");
}

int main(int argc, char* argv[]) {        
    //Parse command line config
    DatagramTunneler::Config cfg;
    getCommandLineConfig(argc, argv, &cfg);
    
    //Create and run the datagram tunneler with the parsed config
    DatagramTunneler tunneler(cfg);
    tunneler.run();

    INFO("Exiting program");
    return 0;
}
