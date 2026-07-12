#include <cstddef>
#include <cstdio>
#include <cstring>

#include "CommandLine.h"
#include "Protocol.h"

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "Test failure: %s\n", message);
        return false;
    }
    return true;
}

bool testProtocolFraming() {
    TunnelPacket packet{};
    packet.type_ = TunnelPacketType::Heartbeat;
    if (!expect(packet.size() == 1, "heartbeat frame must be one byte")) {
        return false;
    }

    packet.type_ = TunnelPacketType::Datagram;
    packet.datalen_ = 42;
    return expect(packet.size() == kTunnelPacketHeaderLength + 42, "datagram frame length must include its header") &&
           expect(offsetof(TunnelPacket, udp_dst_ip_) == 1, "destination address must follow packet type") &&
           expect(offsetof(TunnelPacket, udp_dst_port_) == 5, "destination port offset must be stable") &&
           expect(offsetof(TunnelPacket, datalen_) == 7, "datagram length offset must be stable") &&
           expect(offsetof(TunnelPacket, databuf_) == kTunnelPacketHeaderLength, "payload must follow the header");
}

bool testClientCommandLineParsing() {
    char binary[] = "dgramtunneler";
    char client[] = "--client";
    char udp_interface[] = "-i";
    char interface_address[] = "127.0.0.1";
    char tcp_server[] = "-t";
    char tcp_address[] = "127.0.0.1:14052";
    char udp_group[] = "-u";
    char multicast_address[] = "239.1.2.3:5000";
    char* argv[] = {binary, client, udp_interface, interface_address, tcp_server, tcp_address, udp_group, multicast_address, nullptr};

    DatagramTunneler::Config config;
    if (!expect(parseCommandLineConfig(8, argv, &config), "valid client command line must parse")) {
        return false;
    }

    return expect(config.is_client_, "client mode must be selected") &&
           expect(config.udp_iface_ip_ == "127.0.0.1", "UDP interface must be parsed") &&
           expect(config.tcp_srv_ip_ == "127.0.0.1", "TCP server address must be parsed") &&
           expect(config.tcp_srv_port_ == 14052, "TCP server port must be parsed") &&
           expect(config.udp_dst_ip_ == "239.1.2.3", "multicast address must be parsed") &&
           expect(config.udp_dst_port_ == 5000, "multicast port must be parsed");
}

} // namespace


int main() {
    return testProtocolFraming() && testClientCommandLineParsing() ? 0 : 1;
}
