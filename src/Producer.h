#pragma once

#include <cstddef>
#include <string>

#include "DatagramTunneler.h"

class DatagramProducer {
public:
    struct Options {
        unsigned int interval_ms = 1000;
        std::size_t count = 0; // Zero means produce until interrupted.
        std::string payload_prefix = "Dummy datagram";
    };

    DatagramProducer(const DatagramTunneler::Config& config, Options options);

    void run();

private:
    DatagramTunneler::Config config_;
    Options options_;
};
