#include "DatagramTunneler.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <cerrno>
#include <unistd.h>

#include <sys/types.h>
//for inet_addr();
#include <arpa/inet.h>

DatagramTunneler::DatagramTunneler(Config cfg) : cfg_(cfg) {
    INFO("DatagramTunneler construction");
    if (cfg.is_client_) {
        udp_socket_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_socket_ < 0) {
            DEATH("Could not create UDP socket!");
        }
        
        sockaddr_in bind_addr;
        memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = htons(7437);
        bind_addr.sin_addr.s_addr = INADDR_ANY;
        if(bind(udp_socket_, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
            DEATH("Could not bind UDP socket!");
        }

        ip_mreq udp_group;
        udp_group.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
        udp_group.imr_interface.s_addr = inet_addr("192.168.0.104");
        if(setsockopt(udp_socket_, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<char*>(&udp_group), sizeof(udp_group)) < 0) {
            DEATH("Could not join multicast group %d", errno);
        }

        char data[2048];
        int len_read = 0;
        if((len_read = read(udp_socket_, data, 2048)) < 0) {
            DEATH("Unable to read data from UDP socket %d", errno);
        }
        INFO("Read udp packet of %d bytes", len_read);


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
