#pragma once

#include <cstdint>
#include <functional>
#include <stop_token>
#include <string>

#include "Network.h"

class DatagramTunneler {
public:
    struct RuntimeObserver {
        std::function<void(std::size_t)> on_datagram;
        std::function<void(double)> on_latency;
    };

    struct Config {
        bool            is_client_ = false;
        std::string     udp_iface_ip_;
        std::string     tcp_iface_ip_;
        uint16_t        tcp_srv_port_ = 0;
        std::string     udp_dst_ip_;    // Client uses this to join. Server uses this to publish if use_clt_grp_ is false
        uint16_t        udp_dst_port_ = 0;

        // Client specific
        std::string     tcp_srv_ip_;

        // Server specific
        bool            use_clt_grp_ = false; // If true, the server will publish multicast data on the same group the client is listening

        // Display compact live statistics and recent events when stdout is an interactive terminal.
        bool            compact_output_ = false;

        // Returns true if the config is complete, false otherwise.
        // Note that being complete doesn't mean being valid.
        bool isComplete() const;
    };

    // Constructor
    explicit DatagramTunneler(Config cfg, RuntimeObserver observer = {});

    // Run method, to be called after instantiation
    void run(std::stop_token stop_token = {});

private:
    // Client side methods
    void setupClient(const Config& cfg);
    void runClient(std::stop_token stop_token);
    // Server side methods
    void setupServer(const Config& cfg);
    void runServer(std::stop_token stop_token);

    // Member variables
    Config          cfg_;
    RuntimeObserver observer_;
    Socket          udp_socket_;    // Used by the client to read udp data or by the server to publish data
    Socket          tcp_socket_;    // Used by the client to connect to server, by the server to listen for a client
};
