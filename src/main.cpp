#include "DatagramTunneler.h"

int main(int argc, char* argv[]) {
    INFO("Starting main");
    DatagramTunneler::Config cfg;
    cfg.is_client_ = true;
    DatagramTunneler tunneler(cfg);

    INFO("\n\nNow testing server");
    cfg.is_client_ = false;
    DatagramTunneler tunneler2(cfg);
    tunneler2.run();
    tunneler.run();
    INFO("Exiting main");
}
