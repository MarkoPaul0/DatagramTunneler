#include "DatagramTunneler.h"
//#include <sys/types.h>
#include <sys/socket.h>
//#include <arpa/inet.h>
//#include <netinet/in.h>


DatagramTunneler::DatagramTunneler(Config cfg) : cfg_(cfg) {
    INFO("DatagramTunneler construction");
    if (cfg.is_client_) {
        udp_socket_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_socket_ < 0) {
            DEATH("Could not create UDP socket!");
        }
    } else {
        //TODO: 
    }
}

DatagramTunneler::~DatagramTunneler() {
    INFO("DatagramTunneler destruction");
}

void DatagramTunneler::run() {
    INFO("DatagramTunneler run()");
    if (cfg_.is_client_) {
        runClient();
    } else {
        runServer();
    }
}

void DatagramTunneler::runClient() {
    INFO("DatagramTunneler is now running as a client...");
}

void DatagramTunneler::runServer() {
    INFO("DatagramTunneler is now running as a server...");
}
