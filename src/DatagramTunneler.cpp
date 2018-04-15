#include "DatagramTunneler.h"
#include <sys/socket.h>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <cstdint>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
static const int NO_FLAGS = 0;

DatagramTunneler::DatagramTunneler(Config cfg) : cfg_(cfg) {
    if (cfg.is_client_) {
        setupClient(cfg);
    } else { 
        setupServer(cfg);
    }
}

DatagramTunneler::~DatagramTunneler() {
}

void DatagramTunneler::run() {
    if (cfg_.is_client_) {
        runClient();
    } else {
        runServer();
    }
}


//------------------------------------------------------------------------------------------------
// CLIENT SIDE METHODS
//------------------------------------------------------------------------------------------------
void DatagramTunneler::setupClient(const Config& cfg) {
    // Creating UDP socket
    udp_socket_ = socket(AF_INET, SOCK_DGRAM, NO_FLAGS);
    if (udp_socket_ < 0) {
        DEATH("Could not create UDP socket!");
    }

    // Binding UDP socket to configured port
    sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(cfg.udp_dst_port_);
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    if(bind(udp_socket_, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        DEATH("Could not bind UDP socket to port %u!", cfg.udp_dst_port_);
    }

    // Creating TCP socket
    tcp_socket_ = socket(AF_INET , SOCK_STREAM , NO_FLAGS);
    if (tcp_socket_ < 0) {
        DEATH("Could not create TCP socket!");
    }
}

void DatagramTunneler::runClient() {
    INFO("[DatagramTunneler][CLIENT-MODE] is now running...");
    // Connect to TCP server
    sockaddr_in server_addr;
    //TODO: set an tcp interface ip!
    server_addr.sin_addr.s_addr = inet_addr(cfg_.tcp_srv_ip_.c_str());
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(cfg_.tcp_srv_port_);
    if (connect(tcp_socket_, reinterpret_cast<struct sockaddr *>(&server_addr), sizeof(server_addr)) < 0) {
        DEATH("Unable to connect to server %s:%u. Error %d!", cfg_.tcp_srv_ip_.c_str(), cfg_.tcp_srv_port_, errno);
    }

    // Join multicast group
    ip_mreq udp_group;
    udp_group.imr_multiaddr.s_addr = inet_addr(cfg_.udp_dst_ip_.c_str());
    udp_group.imr_interface.s_addr = inet_addr(cfg_.udp_iface_ip_.c_str());
    if(setsockopt(udp_socket_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &udp_group, sizeof(udp_group)) < 0) {
        DEATH("Could not join multicast group %s:%u. Error %d", cfg_.udp_dst_ip_.c_str(), cfg_.udp_dst_port_, errno);
    }

    Datagram dgram;
    //TODO: pupulate UDP IP and port into datagram once and for all
    int len_read = 0;
    while (true) {
        if((len_read = read(udp_socket_, dgram.databuf_, MAX_DGRAM_LEN)) < 0) {
            //TODO: handle errors and edge cases such as jumbo frames
            DEATH("Unable to read data from UDP socket %d", errno);
        }
        assert(len_read <= UINT16_MAX);
        dgram.datalen_ = static_cast<uint16_t>(len_read);
        if (dgram.datalen_ > 0) {
            if(send(tcp_socket_, &dgram, dgram.size(), NO_FLAGS) < 0) { //TODO: review no flags
                DEATH("Unable to send data to server! Error %d", errno);
            }
            INFO("Tunneled a %u byte datagram to server.", dgram.datalen_);
        }
    }
}
//------------------------------------------------------------------------------------------------


//------------------------------------------------------------------------------------------------
// SERVER SIDE METHODS
//------------------------------------------------------------------------------------------------
void DatagramTunneler::setupServer(const Config& cfg) {
    // Creating UDP socket
    udp_socket_ = socket(AF_INET, SOCK_DGRAM, NO_FLAGS);
    if (udp_socket_ < 0) {
        DEATH("Could not create UDP socket! Error %d", errno);
    }
    
    // Setting up the UDP publishing interface
    in_addr iface;
    iface.s_addr = inet_addr(cfg_.udp_iface_ip_.c_str());
    if(setsockopt(udp_socket_, IPPROTO_IP, IP_MULTICAST_IF, &iface, sizeof(iface)) < 0) {
        DEATH("Could not set UDP publisher interface to %s! Error %d", cfg_.udp_iface_ip_.c_str(), errno);
    }
    
    // Creating TCP socket
    tcp_socket_ = socket(AF_INET , SOCK_STREAM , NO_FLAGS);
    if (tcp_socket_ < 0) {
        DEATH("Could not create TCP socket! Error %d", errno);
    }

    // Binding the TCP socket
    sockaddr_in tcp_iface;
    tcp_iface.sin_family = AF_INET;
    tcp_iface.sin_port = htons (cfg_.tcp_srv_port_);
    tcp_iface.sin_addr.s_addr = htonl(INADDR_ANY); //TODO: review that 
    if(bind(tcp_socket_, reinterpret_cast<sockaddr*>(&tcp_iface), sizeof(tcp_iface)) < 0) {
        DEATH("Could not bind TCP socket to port %u! Error %d", cfg.tcp_srv_port_, errno);
    }
}

void DatagramTunneler::runServer() {
    INFO("[DatagramTunneler][SERVER MODE] listening for client connection on port %u...", cfg_.tcp_srv_port_);
    listen(tcp_socket_, 1); //only accepts one connection
    sockaddr remote;
    socklen_t sosize  = sizeof(remote);
    int new_fd = accept(tcp_socket_, &remote, &sosize);
    if (new_fd < 0) {
        DEATH("Unable to accept incoming TCP connection, accept() error %d!", errno);
    }
    INFO("Accepted incoming connection from a remote host. Waiting for forwarded datagrams...");
    sockaddr_in pub_group;
    if (!cfg_.use_clt_grp_) { //if not reusing the multicast joined by the client then using the one set in the configuration to publish data
        memset(&pub_group, 0, sizeof(pub_group));
        pub_group.sin_family = AF_INET; 
        pub_group.sin_port = htons(cfg_.udp_dst_port_);
        pub_group.sin_addr.s_addr = inet_addr(cfg_.udp_dst_ip_.c_str());
    } else {
        DEATH("Publishing the data on the same group the client has joined is NOT supported yet!");
    }   
    //char data[MAX_DGRAM_LEN];
    Datagram dgram;
    while(true) {
        int len_read = recv(new_fd, &dgram, MAX_DGRAM_LEN, NO_FLAGS);
        if (len_read < 0) {
            DEATH("Unable to read data from TCP socket, recv() error %d!", errno);
        }
        if (len_read == 0) {
            //TODO: temporary, review this
            return;
        }
        if (cfg_.use_clt_grp_) {
            memset(&pub_group, 0, sizeof(pub_group));
            //TODO: use the udp group sent in the packet to publish the data
        }
    
        if(sendto(udp_socket_, dgram.databuf_, dgram.datalen_, NO_FLAGS, reinterpret_cast<struct sockaddr*>(&pub_group), sizeof(pub_group)) < 0) {
            DEATH("Unable to publish multicast data, sendto() error %d!", errno);
        }
        INFO("Published a %u byte datagram tunneled by client.", dgram.datalen_);
    }
}
