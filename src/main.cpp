#include <string>

#include "CommandLine.h"
#include "Log.h"

#ifndef DGRAMTUNNELER_VERSION
#define DGRAMTUNNELER_VERSION "development"
#endif


static void printUsage(const char* binary_name) {
    printf("Usage:\n");
    printf("Server mode:\n");
    printf("    %s --server -i <udp_iface_ip> -t <tcp_listen_port> [-u <udp_dst_ip>:<port>]\n", binary_name);
    printf("Client mode:\n");
    printf("    %s --client -i <udp_iface_ip> -t <tcp_srv_ip>:<tcp_srv_port> -u <udp_dst_ip>:<port>\n", binary_name);
}


static void printVersion() {
    printf("dgramtunneler %s\n", DGRAMTUNNELER_VERSION);
}


int main(int argc, char* argv[]) {
    if (argc == 2 && (std::string(argv[1]) == "--version" || std::string(argv[1]) == "-V")) {
        printVersion();
        return 0;
    }

    // Parse command line config
    DatagramTunneler::Config cfg;
    if (!parseCommandLineConfig(argc, argv, &cfg)) {
        printUsage(argv[0]);
        return 1;
    }

    // Create and run the datagram tunneler with the parsed config
    DatagramTunneler tunneler(cfg);
    tunneler.run();

    INFO("Exiting program");
    return 0;
}
