#include "DatagramTunneler.h"

int main(int argc, char* argv[]) {
    INFO("Starting main %d", argc);
    if (argc == 1) {
        DatagramTunneler::Config cfg;
        cfg.is_client_ = true;
        DatagramTunneler tunneler(cfg);
        tunneler.run();
    } else{
        DatagramTunneler::Config cfg;
        cfg.is_client_ = true;
        INFO("\n\nNow testing server");
        cfg.is_client_ = false;
        DatagramTunneler tunneler2(cfg);
        tunneler2.run();
        INFO("Exiting main");
    }
}
