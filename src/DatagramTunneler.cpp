#include "DatagramTunneler.h"


DatagramTunneler::DatagramTunneler(Config cfg) : cfg_(cfg) {
    INFO("DatagramTunneler construction");
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

int main(int argc, char* argv[]) {
    INFO("Starting main");
    DatagramTunneler::Config cfg;
    cfg.is_client_ = true;
    DatagramTunneler tunneler(cfg);
    tunneler.run();
    INFO("Exiting main");
}

