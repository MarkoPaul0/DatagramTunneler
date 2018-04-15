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
#ifndef MSG_NOSIGNAL
//MSG_NOSIGNAL is posix but somehow not portable (undefined on OSX) 
#define MSG_NOSIGNAL NO_FLAGS
#endif

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

    //OSX only
#ifdef __APPLE__
    // If the TCP server crashes, the client will just exit abruptly because the client socket sends
    // a sigpipe signal when invoking send() instead of returning -1 and setting the errno
    // on other distributions, we use MSG_NOSIGNAL when invoking send()
    int set = 1;
    if (setsockopt(tcp_socket_, SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(set)) < 0) {
        DEATH("Could not prevent TCP socket to send SIGPIPE on disconnect! Error %d", errno);
    }
#endif
}

void DatagramTunneler::runClient() {
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
    INFO("[DatagramTunneler][CLIENT-MODE] connected to TCP remote %s:%u and listening to multicast %s:%u", 
    cfg_.tcp_srv_ip_.c_str(), cfg_.tcp_srv_port_, cfg_.udp_dst_ip_.c_str(), cfg_.udp_dst_port_);

    Datagram dgram;
    inet_pton(AF_INET, cfg_.udp_dst_ip_.c_str(), &dgram.udp_dst_ip_);
    dgram.udp_dst_port_ = cfg_.udp_dst_port_;
    int len_read = 0;
    while (true) {
        if((len_read = read(udp_socket_, dgram.databuf_, MAX_DGRAM_LEN)) < 0) {
            //TODO: handle errors and edge cases such as jumbo frames
            DEATH("Unable to read data from UDP socket %d", errno);
        }
        assert(len_read <= UINT16_MAX);
        dgram.datalen_ = static_cast<uint16_t>(len_read);
        if (dgram.datalen_ > 0) {
            if(send(tcp_socket_, &dgram, dgram.size(), NO_FLAGS) < 0) { 
                DEATH("Unable to send data to server! Error %d", errno);
            }
            //TODO: look at the number of bytes actually written to socket
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
    }

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
        
        if (cfg_.use_clt_grp_) { //potential for feedbackloop if both client and server are in the same subnet
//however if that were the case, there would be no need to forward the datagrams
            memset(&pub_group, 0, sizeof(pub_group));
            pub_group.sin_family = AF_INET; 
            pub_group.sin_port = htons(dgram.udp_dst_port_);
            pub_group.sin_addr.s_addr = dgram.udp_dst_ip_;
        }
    
        if(sendto(udp_socket_, dgram.databuf_, dgram.datalen_, MSG_NOSIGNAL, reinterpret_cast<struct sockaddr*>(&pub_group), sizeof(pub_group)) < 0) {
            DEATH("Unable to publish multicast data, sendto() error %d!", errno);
        }

        //Getting group on which the datagram was published on client side
        char clt_grp_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &dgram.udp_dst_ip_, clt_grp_ip, INET_ADDRSTRLEN);

        //Getting group on which the server is publishing the forwared datagrams
        char pub_grp_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &pub_group.sin_addr, pub_grp_ip, INET_ADDRSTRLEN);
        INFO("Published to %s:%u a %u byte datagram tunneled by client. Client side group was %s:%u",
        pub_grp_ip, ntohs(pub_group.sin_port), dgram.datalen_, clt_grp_ip, dgram.udp_dst_port_);
    }
}
