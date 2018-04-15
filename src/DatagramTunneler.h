#pragma once
#include <cstdint>
#include <assert.h>
#include <string>
#include "log.h"

class DatagramTunneler {
public:
    struct Config {
        bool            is_client_;
        std::string     udp_iface_ip_;
        std::string     tcp_iface_ip_;
        uint16_t        tcp_srv_port_;
        std::string     udp_dst_ip_;    //Client uses this to join. Server uses this to publish if use_clt_multicast_ is false 
        uint16_t        udp_dst_port_;
         
        // Client specific
        std::string     tcp_srv_ip_;

        // Sevver specific
        bool            use_clt_grp_; //if true, the server will publish multicast data on the same group the client is listening
    };

    DatagramTunneler(Config cfg);   // Constructor
    ~DatagramTunneler();            // Destructor
    void run();

private:
    // Client side methods
    void setupClient(const Config& cfg);
    void runClient();
    // Server side methods
    void setupServer(const Config& cfg);
    void runServer();

    // Member variables
    Config          cfg_;
    int             udp_socket_;    // Used by the client to read udp data or by the server to publish data
    int             tcp_socket_;    // Used by the client to connect to server, by the server to listen for a client
};
