#pragma once
#include <cstdio>
#include <cstdint>

#define INFO(_format_,...) printf("INFO      "); printf((_format_),##__VA_ARGS__); printf("\n");
#define WARN(_format_,...) printf("WARNING   "); printf((_format_),##__VA_ARGS__); printf("\n");
#define ERROR(_format_,...) printf("ERROR     "); printf((_format_),##__VA_ARGS__); printf("\n");

class DatagramTunneler {
public:
    struct ClientCfg {
        uint32_t udp_dst_ip_;   // UDP destination IP (multicast group ip)
        uint16_t udp_port_;     // UDP destination port //TODO: 0?
        uint32_t udp_iface_ip_; // Interface used to listen to multicast data
        uint32_t srv_ip_;       // TCP IP of DatagramTunneler server    
        uint16_t srv_port_;     // TCP port of DatagramTunneler server
        uint32_t srv_iface_ip_; // Interface used to connect to server
    };

    struct ServerCfg {   
        uint32_t listen_iface_; // Interface used to listen for client connection
        uint16_t listen_port_;  // Port used to listen for client connection
        uint32_t udp_iface_;    // Interface used to publish the datagrams received from the client
        uint32_t udp_dst_ip_;   //TODO: optional
        uint32_t udp_dst_port_; //TODO: optional
        
    };

    struct Config {
        bool            is_client_;
        union {
            ClientCfg   client_cfg_;
            ServerCfg   server_cfg_;
        };
    };

    //Constructor
    DatagramTunneler(Config cfg); 
    ~DatagramTunneler();
private:
    struct Datagram {
        uint32_t udp_dst_ip_;   // UDP destination address 
        uint16_t udp_dst_port_; // UDP destination port
        uint16_t datalen_;      // Datagram length
        char*    databuf_;      // Datagram buffer
    };
};
