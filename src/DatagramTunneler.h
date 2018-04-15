#pragma once
#include <cstdio>
#include <cstdint>
#include <assert.h>
#include <netinet/in.h>
#include <string>

#define INFO(_format_,...)  printf("INFO      "); printf((_format_),##__VA_ARGS__); printf("\n");
#define WARN(_format_,...)  printf("WARNING   "); printf((_format_),##__VA_ARGS__); printf("\n");
#define ERROR(_format_,...) printf("ERROR     "); printf((_format_),##__VA_ARGS__); printf("\n");
#define DEATH(_format_,...) printf("DEATH     "); printf((_format_),##__VA_ARGS__); printf("\n"); exit(1);

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
        bool            use_clt_multicast_; //if true, the server will publish multicast data on the same group the client is listening
    };

    //Constructor
    DatagramTunneler(Config cfg);
    //Destructor 
    ~DatagramTunneler();
    
    void run();
private:
static const size_t MAX_DGRAM_LEN = 1472; //jumbo frames are not supported
#pragma pack(push,1)
    struct Datagram {
        uint32_t udp_dst_ip_;               // UDP destination address 
        uint16_t udp_dst_port_;             // UDP destination port
        uint16_t datalen_;                  // Datagram length
        char     databuf_[MAX_DGRAM_LEN];   // Datagram buffer
        size_t size() const { //TODO: move to cpp
            return static_cast<size_t>(datalen_ + 8);
        }
    };
    static_assert(sizeof(Datagram) == 1480, "The Datagram struct should be 1480 bytes long!");
#pragma pack(pop)

    //Client side methods
    void setupClient(const Config& cfg);
    void runClient();
    void getNextDatagram(Datagram* const dgram);
    void sendDatagramToServer(const Datagram* dgram);

    //Server side methods
    void setupServer(const Config& cfg);
    void runServer();

    Config          cfg_;
    bool            is_client_;
    int             udp_socket_;    // Used by the client to read udp data or by the server to publish data
    int             tcp_socket_;    // used by the client to connect to the server, by the server to listen for client connections
    sockaddr_in     pub_group_;     // publisher group, used by server to publish data
};
