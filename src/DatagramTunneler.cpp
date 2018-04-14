#include "DatagramTunneler.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <cstdint>

#include <sys/types.h>
//for inet_addr();
#include <arpa/inet.h>
static const int NO_FLAGS = 0;

DatagramTunneler::DatagramTunneler(Config cfg) : cfg_(cfg) {
    INFO("DatagramTunneler construction");
    if (cfg.is_client_) {
        INFO("CLIENT SETUP!");
        // UDP SOCKET SETUP
        udp_socket_ = socket(AF_INET, SOCK_DGRAM, NO_FLAGS);
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

        //TCP SOCKET SETUP
        tcp_socket_ = socket(AF_INET , SOCK_STREAM , NO_FLAGS);
		if (tcp_socket_ < 0) {
            DEATH("Could not create TCP socket!");
        }
        sockaddr_in server_addr;
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        server_addr.sin_family = AF_INET;
  	    server_addr.sin_port = htons(24052);
        //TODO: move the connection to run the function maybe?
        if (connect(tcp_socket_, reinterpret_cast<struct sockaddr *>(&server_addr), sizeof(server_addr)) < 0) {
            DEATH("Unable to connect to server %d", errno);
        }

    } else { //SERVER SETUP
        INFO("SERVER SETUP!");
        // UDP SOCKET SETUP
        udp_socket_ = socket(AF_INET, SOCK_DGRAM, NO_FLAGS);
        if (udp_socket_ < 0) {
            DEATH("Could not create UDP socket!");
        }
        
        in_addr iface;
        iface.s_addr = inet_addr("192.168.0.104");
        if(setsockopt(udp_socket_, IPPROTO_IP, IP_MULTICAST_IF, &iface, sizeof(iface)) < 0) {
            DEATH("Could not set UDP publisher interface");
        }
        

        //TCP SOCKET SETUP
        tcp_socket_ = socket(AF_INET , SOCK_STREAM , NO_FLAGS);
		if (tcp_socket_ < 0) {
            DEATH("Could not create TCP socket!");
        }

        sockaddr_in tcp_iface;
        tcp_iface.sin_family = AF_INET;
        tcp_iface.sin_port = htons (24052);
        tcp_iface.sin_addr.s_addr = htonl (INADDR_ANY);
        if(bind(tcp_socket_, reinterpret_cast<sockaddr*>(&tcp_iface), sizeof(tcp_iface)) < 0) {
            DEATH("Could not bind TCP socket!");
        }

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

void DatagramTunneler::getNextDatagram(Datagram* const dgram) {
    int len_read = 0;
    if((len_read = read(udp_socket_, dgram->databuf_, MAX_DGRAM_LEN)) < 0) {
        //TODO: handle errors and edge cases such as jumbo frames
        DEATH("Unable to read data from UDP socket %d", errno);
    }
    assert(len_read <= UINT16_MAX);
    //tODO: handle case where len_read = 0
    INFO("Read udp packet of %d bytes", len_read);
    dgram->datalen_ = static_cast<uint16_t>(len_read);
}

void DatagramTunneler::sendDatagramToServer(const Datagram* dgram) {
    INFO("Sending datagram to server via TCP");
    if(send(tcp_socket_, dgram, dgram->size(), NO_FLAGS) < 0) { //TODO: review no flags
        DEATH("Unable to send data to server!");
    }
}

void DatagramTunneler::runClient() {
    INFO("DatagramTunneler is now running as a client...");

    ip_mreq udp_group;
    udp_group.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
    udp_group.imr_interface.s_addr = inet_addr("192.168.0.104");
    if(setsockopt(udp_socket_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &udp_group, sizeof(udp_group)) < 0) {
        DEATH("Could not join multicast group %d", errno);
    }

    Datagram dgram;
    //TODO: pupulate UDP IP and port into datagram once and for all
    while (true) {
        getNextDatagram(&dgram);
        sendDatagramToServer(&dgram);
    }
}

void DatagramTunneler::runServer() {
    INFO("DatagramTunneler is now running as a server...");
    INFO("Listening on port 24052..");
    sockaddr_in group_addr;
    memset(&group_addr, 0, sizeof(group_addr));
    group_addr.sin_family = AF_INET; 
    group_addr.sin_port = htons(1234);
    group_addr.sin_addr.s_addr = inet_addr("228.14.28.52");
    listen(tcp_socket_,1);
    while(true) {
        sockaddr remote;
        socklen_t sosize  = sizeof(remote);
        int new_fd = accept(tcp_socket_, &remote, &sosize);
        if (new_fd < 0) {
            DEATH("Accept error!");
        }
        INFO("Now receiving data from the connection");
        char data[2048];
        while(true) {
            int len_read = recv(new_fd, data, 2048, NO_FLAGS);
            if (len_read < 0) {
                DEATH("ERROR While reading");
            }
            INFO("Received %d bytes", len_read);
        
            Datagram dgram;
            if(sendto(udp_socket_, dgram.databuf_, dgram.datalen_, NO_FLAGS, reinterpret_cast<struct sockaddr*>(&group_addr), sizeof(group_addr)) < 0) {
                DEATH("Unable to publish UDP data!");
            }
            INFO("Sent data to multicast group 228.14.28.52!");
        }
    }
}







