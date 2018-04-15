#include "DatagramTunneler.h"

int main(int argc, char* argv[]) {
    INFO("Starting main %d", argc);
    
    DatagramTunneler::Config cfg;
    cfg.udp_iface_ip_ = "192.168.0.104";
    cfg.tcp_iface_ip_ = "";
    cfg.tcp_srv_ip_ = "127.0.0.1";
    cfg.tcp_srv_port_ = 28014;
    if (argc == 1) {
        cfg.is_client_ = true;
        cfg.udp_dst_ip_ = "224.0.0.251";
        cfg.udp_dst_port_ = 7437;
        DatagramTunneler tunneler(cfg);
        tunneler.run();
    } else{
        INFO("\n\nNow testing server");
        cfg.is_client_ = false;
        cfg.udp_dst_ip_ = "228.14.28.52";
        cfg.udp_dst_port_ = 1234;
//        cfg.use_clt_grp_ = true;
        DatagramTunneler tunneler2(cfg);
        tunneler2.run();
        INFO("Exiting main");
    }
}
