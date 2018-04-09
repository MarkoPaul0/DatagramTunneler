#include "DatagramTunneler.h"


DatagramTunneler::DatagramTunneler(Config cfg) {
    INFO("DatagramTunneler construction");
}

DatagramTunneler::~DatagramTunneler() {
    INFO("DatagramTunneler destruction");
}

int main(int argc, char* argv[]) {
    INFO("Starting main");
    DatagramTunneler::Config cfg;
    DatagramTunneler tunneler(cfg);
    INFO("Exiting main");
}

