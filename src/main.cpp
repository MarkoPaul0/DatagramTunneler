#include "DatagramTunneler.h"

int main(int argc, char* argv[]) {
    INFO("Starting main");
    DatagramTunneler::Config cfg;
    cfg.is_client_ = true;
    DatagramTunneler tunneler(cfg);
    tunneler.run();
    INFO("Exiting main");
}
