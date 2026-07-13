#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "CommandLine.h"
#include "Configuration.h"
#include "Network.h"
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

bool testNamedTunnelConfiguration() {
    std::istringstream input(R"(version = 1

[tunnels.office-client]
mode = "client"
udp_interface = "127.0.0.1"
udp_group = "239.1.2.3:5000"
tcp_server = "127.0.0.1:14052"

[tunnels.office-server]
mode = "server"
udp_interface = "127.0.0.1"
tcp_listen_port = 14052
udp_destination = "239.1.2.4:5000"

[tunnels.replica-server]
mode = "server"
udp_interface = "127.0.0.1"
tcp_listen_port = 14053
udp_destination = "replicate_client"

[tunnels.legacy-server]
mode = "server"
udp_interface = "127.0.0.1"
tcp_listen_port = 14054
)");

    const TunnelConfiguration configuration = parseConfiguration(input);
    if (!expect(configuration.tunnels.size() == 4, "all named tunnels must be loaded")) {
        return false;
    }

    const NamedTunnel& client = findTunnel(configuration, "office-client");
    const NamedTunnel& server = findTunnel(configuration, "office-server");
    const NamedTunnel& replica = findTunnel(configuration, "replica-server");
    const NamedTunnel& legacy = findTunnel(configuration, "legacy-server");
    return expect(client.config.is_client_, "client tunnel mode must be selected") &&
           expect(client.config.tcp_srv_port_ == 14052, "client server port must be parsed") &&
           expect(!server.config.is_client_, "server tunnel mode must be selected") &&
           expect(server.config.udp_dst_port_ == 5000, "server destination port must be parsed") &&
           expect(replica.config.use_clt_grp_, "explicit replica server must use the client group") &&
           expect(legacy.config.use_clt_grp_, "server without a destination must retain legacy client-group behavior");
}

bool testInvalidNamedTunnelConfiguration() {
    std::istringstream input(R"(version = 1

[tunnels.bad-client]
mode = "client"
udp_interface = "127.0.0.1"
udp_group = "239.1.2.3:5000"
tcp_server = "127.0.0.1:14052"
unknown = "value"
)");
    try {
        static_cast<void>(parseConfiguration(input));
    } catch (const std::runtime_error&) {
        return true;
    }
    return expect(false, "unknown named-tunnel fields must be rejected");
}

} // namespace


int main() {
    int network_error = 0;
    if (!initializeNetwork(&network_error)) {
        std::fprintf(stderr, "Test failure: network initialization failed (%d)\n", network_error);
        return 1;
    }
    return testProtocolFraming() && testClientCommandLineParsing() && testNamedTunnelConfiguration() &&
                   testInvalidNamedTunnelConfiguration()
               ? 0
               : 1;
}
