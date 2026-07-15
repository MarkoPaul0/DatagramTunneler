#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "CommandLine.h"
#include "Configuration.h"
#include "Network.h"
#include "Protocol.h"
#include "control/ControlService.h"
#include "control/ControlApi.h"
#include "control/ManagedTunnelRuntime.h"

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
    packet.protocol_version_ = kDtepProtocolVersion;
    if (!expect(packet.size() == kTunnelPacketPreambleLength, "heartbeat frame must include type and version")) {
        return false;
    }

    packet.type_ = TunnelPacketType::Datagram;
    packet.datalen_ = 42;
    return expect(packet.size() == kTunnelPacketHeaderLength + 42, "datagram frame length must include its header") &&
           expect(kDtepProtocolVersion == 2, "timestamp protocol must use version 2") &&
           expect(offsetof(TunnelPacket, protocol_version_) == 1, "protocol version must follow packet type") &&
           expect(offsetof(TunnelPacket, udp_dst_ip_) == 2, "destination address must follow packet version") &&
           expect(offsetof(TunnelPacket, udp_dst_port_) == 6, "destination port offset must be stable") &&
           expect(offsetof(TunnelPacket, datalen_) == 8, "datagram length offset must be stable") &&
           expect(offsetof(TunnelPacket, client_timestamp_us_) == 10, "timestamp must follow the datagram length") &&
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

bool testCompactCommandLineParsing() {
    char binary[] = "dgramtunneler";
    char client[] = "--client";
    char udp_interface[] = "-i";
    char interface_address[] = "127.0.0.1";
    char tcp_server[] = "-t";
    char tcp_address[] = "127.0.0.1:14052";
    char udp_group[] = "-u";
    char multicast_address[] = "239.1.2.3:5000";
    char compact[] = "--compact";
    char* argv[] = {binary, client, udp_interface, interface_address, tcp_server, tcp_address,
                    udp_group, multicast_address, compact, nullptr};

    DatagramTunneler::Config config;
    return expect(parseCommandLineConfig(9, argv, &config), "compact client command line must parse") &&
           expect(config.compact_output_, "--compact must enable compact output");
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

bool testControlServiceCatalog() {
    const std::filesystem::path configuration_path =
        std::filesystem::path(__FILE__).parent_path() / "named_tunnels.toml";
    const control::ControlService service(configuration_path);
    const std::vector<control::TunnelSummary> tunnels = service.listTunnels();
    if (!expect(tunnels.size() == 3, "control service must expose each named tunnel")) {
        return false;
    }

    const NamedTunnel client = service.tunnel("example-client");
    const NamedTunnel replica = service.tunnel("replica-server");
    return expect(client.config.is_client_, "control service must return client configuration") &&
           expect(replica.config.use_clt_grp_, "control service must preserve replicate-client configuration") &&
           expect(tunnels[2].udp_destination == "replicate_client", "control service must describe replicated destinations") &&
           expect(tunnels[0].equivalent_direct_command.find("dgramtunneler --client") == 0,
                  "control service must expose a direct command equivalent");
}

class RecordingEventSink final : public control::EventSink {
public:
    void publish(const control::ControlEvent& event) override {
        const std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(event);
    }

    std::size_t size() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return events_.size();
    }

private:
    mutable std::mutex mutex_;
    std::vector<control::ControlEvent> events_;
};

bool testManagedProducerRuntime() {
    const std::filesystem::path configuration_path =
        std::filesystem::path(__FILE__).parent_path() / "named_tunnels.toml";
    const control::ControlService service(configuration_path);
    RecordingEventSink event_sink;
    control::ManagedTunnelRuntime runtime(service, &event_sink);
    DatagramProducer::Options options;
    options.count = 1;
    options.interval_ms = 1;
    runtime.startProducer("example-client", options);

    for (int attempt = 0; attempt < 100; ++attempt) {
        const std::vector<control::TunnelSnapshot> snapshots = runtime.snapshots();
        if (snapshots.size() == 1 && snapshots.front().state == control::TunnelState::Stopped) {
            return expect(snapshots.front().kind == control::RuntimeKind::Producer,
                          "managed producer must be identified as a producer") &&
                   expect(event_sink.size() >= 3, "managed producer must publish lifecycle events");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return expect(false, "managed producer must reach a stopped state");
}

bool testControlApiContract() {
    const control::api::ProducerStartRequest defaults;
    return expect(control::api::kVersion == "v1", "control API must expose its version") &&
           expect(control::api::tunnelActionPath("example-client", "start") == "/api/v1/tunnels/example-client/start",
                  "control API must define tunnel lifecycle paths") &&
           expect(control::api::producerActionPath("example-client", "start") ==
                      "/api/v1/tunnels/example-client/producer/start",
                  "control API must define producer lifecycle paths") &&
           expect(defaults.interval_milliseconds == 1000 && !defaults.count.has_value(),
                  "control API producer defaults must be safe");
}

} // namespace


int main() {
    int network_error = 0;
    if (!initializeNetwork(&network_error)) {
        std::fprintf(stderr, "Test failure: network initialization failed (%d)\n", network_error);
        return 1;
    }
    return testProtocolFraming() && testClientCommandLineParsing() && testCompactCommandLineParsing() && testNamedTunnelConfiguration() &&
                   testInvalidNamedTunnelConfiguration() && testControlServiceCatalog() && testManagedProducerRuntime()
                   && testControlApiContract()
               ? 0
               : 1;
}
