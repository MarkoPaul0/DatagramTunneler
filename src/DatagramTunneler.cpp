#include "DatagramTunneler.h"

#include <assert.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

#include "Log.h"
#include "Network.h"
#include "Protocol.h"

namespace {

constexpr int kNoFlags = 0;
constexpr int kHeartbeatPeriodSeconds = 5;
constexpr auto kLatencyReportInterval = std::chrono::seconds(5);
constexpr auto kTcpConnectTimeout = std::chrono::seconds(10);
constexpr auto kTcpConnectPollInterval = std::chrono::milliseconds(250);

uint64_t currentTimestampMicroseconds() {
    const auto elapsed = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
}

class LatencyStatistics {
public:
    void record(double milliseconds) {
        samples_.push_back(milliseconds);
    }

    void reportIfDue() {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_report_ < kLatencyReportInterval || samples_.empty()) {
            return;
        }
        if (compactOutputEnabled()) {
            samples_.clear();
            last_report_ = now;
            return;
        }
        std::sort(samples_.begin(), samples_.end());
        double sum = 0.0;
        for (const double sample : samples_) {
            sum += sample;
        }
        const auto percentile = [this](double percentile_value) {
            const std::size_t index = static_cast<std::size_t>(std::ceil(percentile_value *
                static_cast<double>(samples_.size()))) - 1;
            return samples_[index];
        };
        INFO("Tunnel latency over the last interval: avg %.3f ms, p50 %.3f ms, p99 %.3f ms, max %.3f ms (%zu datagram%s)",
             sum / static_cast<double>(samples_.size()), percentile(0.50), percentile(0.99), samples_.back(),
             samples_.size(), samples_.size() == 1 ? "" : "s");
        samples_.clear();
        last_report_ = now;
    }

private:
    std::vector<double> samples_;
    std::chrono::steady_clock::time_point last_report_ = std::chrono::steady_clock::now();
};

Socket createSocket(int type) {
    Socket socket_handle(socket(AF_INET, type, kNoFlags));
    if (!socket_handle.valid()) {
        throw std::runtime_error("Could not create socket! Error " + std::to_string(lastSocketError()));
    }
    return socket_handle;
}

[[noreturn]] void throwTunnelError(const char* format, ...) {
    char message[512] {};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    throw std::runtime_error(message);
}

} // namespace


// This method is local to this cpp file and sends the entire buffer over TCP.
static bool sendTCPData(SocketHandle tcp_socket, const void* data, size_t datalen, int flags) {
    size_t len_to_send = datalen;
    const char* p = reinterpret_cast<const char*>(data);
    do {
        SocketIoSize len_sent = 0;
        if((len_sent = send(tcp_socket, p, static_cast<SocketBufferLength>(len_to_send), flags)) < 0) {
            return false;
        }
        if (len_sent == 0) {
            throwTunnelError("Unable to send data via TCP. The server might not be able to keep up!");
        }
        assert(len_to_send >= static_cast<size_t>(len_sent));
        len_to_send -= static_cast<size_t>(len_sent);
        p += len_sent;
    } while (len_to_send != 0);
    return true;
}


bool DatagramTunneler::Config::isComplete() const {
    if (udp_iface_ip_.empty() ||
        //tcp_iface_ip_.empty() || //TODO
        tcp_srv_port_ == 0) {
        return false;
    }
    if (is_client_) {
        if (tcp_srv_ip_.empty() || udp_dst_ip_.empty() || udp_dst_port_ == 0) {
            return false;
        }
    } else if (!use_clt_grp_ && (udp_dst_ip_.empty() || udp_dst_port_ == 0)) {
        return false;
    }
    return true;
}


// ---------------------------- DatagramTunneler Implementation--------------------------------
DatagramTunneler::DatagramTunneler(Config cfg, RuntimeObserver observer)
    : cfg_(std::move(cfg)), observer_(std::move(observer)) {
    if (cfg_.udp_iface_reference_.empty()) {
        cfg_.udp_iface_reference_ = cfg_.udp_iface_ip_;
    }
    const auto interface_address = resolveInterfaceIpv4(cfg_.udp_iface_ip_);
    if (!interface_address.has_value()) {
        throw std::runtime_error("could not resolve UDP interface '" + cfg_.udp_iface_reference_ + "' to an IPv4 address");
    }
    cfg_.udp_iface_ip_ = *interface_address;
    if (cfg_.is_client_)
        setupClient(cfg_);
    else
        setupServer(cfg_);
}


void DatagramTunneler::run(std::stop_token stop_token) {
    if (cfg_.is_client_)
        runClient(stop_token);
    else
        runServer(stop_token);
}


//------------------------------------------------------------------------------------------------
// CLIENT SIDE METHODS
//------------------------------------------------------------------------------------------------
void DatagramTunneler::setupClient(const Config& cfg) {
    // Creating UDP socket
    udp_socket_ = createSocket(SOCK_DGRAM);

    // Setting timeout on UDP socket so as to send data to server at least every HEARTBEAT_PERIOD_SEC seconds
    if (!setSocketReceiveTimeout(udp_socket_.get(), kHeartbeatPeriodSeconds)) {
        throwTunnelError("Could not set receive timeout on UDP socket! Error %d", lastSocketError());
    }

    // Binding UDP socket to configured port
    sockaddr_in bind_addr {};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(cfg.udp_dst_port_);
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    if(bind(udp_socket_.get(), reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        throwTunnelError("Could not bind UDP socket to port %u! Error %d", cfg.udp_dst_port_, lastSocketError());
    }

    // Creating TCP socket
    tcp_socket_ = createSocket(SOCK_STREAM);

    //OSX only
#ifdef __APPLE__
    // If the TCP server crashes, the client will just exit abruptly because the client socket sends
    // a sigpipe signal when invoking send() instead of returning an error
    // on other distributions, we use MSG_NOSIGNAL when invoking send()
    int set = 1;
    if (setSocketOption(tcp_socket_.get(), SOL_SOCKET, SO_NOSIGPIPE, set) < 0) {
        throwTunnelError("Could not prevent TCP socket to send SIGPIPE on disconnect! Error %d", lastSocketError());
    }
#endif
}


void DatagramTunneler::runClient(std::stop_token stop_token) {
    // Connect to TCP server
    sockaddr_in server_addr {};
    //TODO: set a tcp interface ip!
    server_addr.sin_addr.s_addr = inet_addr(cfg_.tcp_srv_ip_.c_str());
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(cfg_.tcp_srv_port_);
    if (!connectClientWithTimeout(server_addr, stop_token)) {
        return;
    }
    if (observer_.on_client_connection_state) {
        observer_.on_client_connection_state(ClientConnectionState::Connected);
    }
    if (compactOutputEnabled()) {
        logCompactMessage(LogLevel::Info, "connected to TCP server");
    } else {
        INFO("[DatagramTunneler][CLIENT-MODE] connected to TCP remote %s:%u and listening to multicast %s:%u",
             cfg_.tcp_srv_ip_.c_str(), cfg_.tcp_srv_port_, cfg_.udp_dst_ip_.c_str(), cfg_.udp_dst_port_);
    }

    // Join multicast group
    ip_mreq udp_group {};
    udp_group.imr_multiaddr.s_addr = inet_addr(cfg_.udp_dst_ip_.c_str());
    udp_group.imr_interface.s_addr = inet_addr(cfg_.udp_iface_ip_.c_str());
    if(setSocketOption(udp_socket_.get(), IPPROTO_IP, IP_ADD_MEMBERSHIP, udp_group) < 0) {
        throwTunnelError("Could not join multicast group %s:%u. Error %d", cfg_.udp_dst_ip_.c_str(), cfg_.udp_dst_port_, lastSocketError());
    }
    if (compactOutputEnabled()) {
        logCompactMessage(LogLevel::Info, "joined multicast group");
    } else {
        INFO("Joined multicast group %s:%u.", cfg_.udp_dst_ip_.c_str(), cfg_.udp_dst_port_);
    }

    // Setting up the DTEP header in the TunnelPacket struct
    TunnelPacket tunnel_pkt {};
    tunnel_pkt.protocol_version_ = kDtepProtocolVersion;
    inet_pton(AF_INET, cfg_.udp_dst_ip_.c_str(), &tunnel_pkt.udp_dst_ip_);
    tunnel_pkt.udp_dst_port_ = cfg_.udp_dst_port_;
    SocketIoSize len_read = 0;
    // Running loop
    while (!stop_token.stop_requested()) {
        // Read datagram from udp socket
        if((len_read = recv(udp_socket_.get(), reinterpret_cast<char*>(tunnel_pkt.databuf_.data()), static_cast<SocketBufferLength>(kMaxDatagramLength), kReceiveTruncationFlag)) < 0) {
            //TODO: handle errors and edge cases such as jumbo frames
            const int error_code = lastSocketError();
            if (isDatagramTooLargeError(error_code)) {
                WARN("Discarding jumbo datagram.");
                continue;
            }
            if (!isReceiveTimeout(error_code)) {
                throwTunnelError("Unable to read data from UDP socket %d", error_code);
            }
            tunnel_pkt.type_ = TunnelPacketType::Heartbeat;
            if (compactOutputEnabled()) {
                logCompactMessage(LogLevel::Info, "heartbeat -> TCP");
            } else {
                INFO("Sending a heartbeat to server.");
            }
        } else {
            assert(len_read <= static_cast<SocketIoSize>(kMaxDatagramLength));
            if (len_read > static_cast<SocketIoSize>(kMaxDatagramLength)) { //this is possible because we are using MSG_TRUNC flag
                WARN("Discarding jumbo datagram of %zu bytes!", static_cast<size_t>(len_read));
                continue;
            }
            tunnel_pkt.type_ = TunnelPacketType::Datagram;
            if (compactOutputEnabled()) {
                logCompactMessage(LogLevel::Info, "forwarded %zu B", static_cast<size_t>(len_read));
            } else {
                INFO("Tunneling a %zu byte datagram to server.", static_cast<size_t>(len_read));
            }
            tunnel_pkt.datalen_ = static_cast<uint16_t>(len_read);
            tunnel_pkt.client_timestamp_us_ = currentTimestampMicroseconds();
        }

        // Send the encapsulated datagram to the server over the TCP connection
        if(!sendTCPData(tcp_socket_.get(), &tunnel_pkt, tunnel_pkt.size(), kNoSignalFlag)) {
            throwTunnelError("Unable to send data to server! Error %d", lastSocketError());
        }
        if (tunnel_pkt.type_ == TunnelPacketType::Datagram) {
            recordDatagram(tunnel_pkt.datalen_);
            if (observer_.on_datagram) {
                observer_.on_datagram({tunnel_pkt.datalen_, std::nullopt});
            }
        }
    }
}

bool DatagramTunneler::connectClientWithTimeout(const sockaddr_in& server_addr, std::stop_token stop_token) {
    int nonblocking_error = 0;
    if (!setSocketBlocking(tcp_socket_.get(), false, &nonblocking_error)) {
        throwTunnelError("Could not set TCP socket to non-blocking mode. Error %d", nonblocking_error);
    }

    const int connect_result = connect(tcp_socket_.get(), reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr));
    if (connect_result == 0) {
        if (!setSocketBlocking(tcp_socket_.get(), true, &nonblocking_error)) {
            throwTunnelError("Could not restore TCP socket blocking mode. Error %d", nonblocking_error);
        }
        return true;
    }

    const int connect_error = lastSocketError();
    if (!isConnectInProgress(connect_error)) {
        throwTunnelError("Unable to connect to server %s:%u. Error %d!", cfg_.tcp_srv_ip_.c_str(), cfg_.tcp_srv_port_, connect_error);
    }

    const auto deadline = std::chrono::steady_clock::now() + kTcpConnectTimeout;
    while (!stop_token.stop_requested() && std::chrono::steady_clock::now() < deadline) {
        fd_set writable;
        fd_set failed;
        FD_ZERO(&writable);
        FD_ZERO(&failed);
        FD_SET(tcp_socket_.get(), &writable);
        FD_SET(tcp_socket_.get(), &failed);
        const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(deadline - std::chrono::steady_clock::now());
        const auto wait = std::min(remaining, std::chrono::duration_cast<std::chrono::microseconds>(kTcpConnectPollInterval));
        timeval timeout {};
        timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(wait.count() / 1000000);
        timeout.tv_usec = static_cast<decltype(timeout.tv_usec)>(wait.count() % 1000000);
#ifdef _WIN32
        const int selected = select(0, nullptr, &writable, &failed, &timeout);
#else
        const int selected = select(tcp_socket_.get() + 1, nullptr, &writable, &failed, &timeout);
#endif
        if (selected == 0) {
            continue;
        }
        if (selected < 0) {
            throwTunnelError("Unable to wait for TCP connection to server %s:%u. Error %d!", cfg_.tcp_srv_ip_.c_str(),
                             cfg_.tcp_srv_port_, lastSocketError());
        }

        int socket_error = 0;
        SocketAddressLength socket_error_length = sizeof(socket_error);
#ifdef _WIN32
        if (getsockopt(tcp_socket_.get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error),
                       &socket_error_length) < 0) {
#else
        if (getsockopt(tcp_socket_.get(), SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_length) < 0) {
#endif
            throwTunnelError("Unable to inspect TCP connection to server %s:%u. Error %d!", cfg_.tcp_srv_ip_.c_str(),
                             cfg_.tcp_srv_port_, lastSocketError());
        }
        if (socket_error != 0) {
            throwTunnelError("Unable to connect to server %s:%u. Error %d!", cfg_.tcp_srv_ip_.c_str(), cfg_.tcp_srv_port_, socket_error);
        }
        if (!setSocketBlocking(tcp_socket_.get(), true, &nonblocking_error)) {
            throwTunnelError("Could not restore TCP socket blocking mode. Error %d", nonblocking_error);
        }
        return true;
    }

    if (stop_token.stop_requested()) {
        return false;
    }
    throwTunnelError("Timed out connecting to server %s:%u after %lld seconds.", cfg_.tcp_srv_ip_.c_str(), cfg_.tcp_srv_port_,
                     static_cast<long long>(kTcpConnectTimeout.count()));
}

//------------------------------------------------------------------------------------------------
// SERVER SIDE METHODS
//------------------------------------------------------------------------------------------------
void DatagramTunneler::setupServer(const Config& cfg) {
    // Creating UDP socket
    udp_socket_ = createSocket(SOCK_DGRAM);

    // Setting up the UDP publishing interface
    in_addr iface;
    iface.s_addr = inet_addr(cfg_.udp_iface_ip_.c_str());
    if(setSocketOption(udp_socket_.get(), IPPROTO_IP, IP_MULTICAST_IF, iface) < 0) {
        throwTunnelError("Could not set UDP publisher interface to %s! Error %d", cfg_.udp_iface_ip_.c_str(), lastSocketError());
    }
    if (cfg_.use_clt_grp_) {
        const unsigned char disable_loopback = 0;
        if (setSocketOption(udp_socket_.get(), IPPROTO_IP, IP_MULTICAST_LOOP, disable_loopback) < 0) {
            throwTunnelError("Could not disable multicast loopback for client-group replication! Error %d", lastSocketError());
        }
    }

    // Creating TCP socket
    tcp_socket_ = createSocket(SOCK_STREAM);

    // Binding the TCP socket
    sockaddr_in tcp_iface {};
    tcp_iface.sin_family = AF_INET;
    tcp_iface.sin_port = htons (cfg_.tcp_srv_port_);
    tcp_iface.sin_addr.s_addr = htonl(INADDR_ANY); //TODO: review that
    if(bind(tcp_socket_.get(), reinterpret_cast<sockaddr*>(&tcp_iface), sizeof(tcp_iface)) < 0) {
        throwTunnelError("Could not bind TCP socket to port %u! Error %d", cfg.tcp_srv_port_, lastSocketError());
    }
}


void DatagramTunneler::runServer(std::stop_token stop_token) {
    if (compactOutputEnabled()) {
        logCompactMessage(LogLevel::Info, "listening for client");
    } else {
        INFO("[DatagramTunneler][SERVER MODE] listening for client connection on port %u...", cfg_.tcp_srv_port_);
    }
    if (cfg_.use_clt_grp_) {
        WARN("Client-group replication must use different source and destination subnets to avoid multicast feedback loops.");
    }
    //Listening for client connection and accepting connection
    if (listen(tcp_socket_.get(), 1) < 0) {
        throwTunnelError("Unable to listen on TCP port %u, error %d!", cfg_.tcp_srv_port_, lastSocketError());
    }
    if (!setSocketReceiveTimeout(tcp_socket_.get(), 1)) {
        throwTunnelError("Could not set TCP listen timeout! Error %d", lastSocketError());
    }
    Socket client_socket;
    while (!stop_token.stop_requested() && !client_socket.valid()) {
        sockaddr remote {};
        SocketAddressLength sosize = sizeof(remote);
        Socket accepted_socket(accept(tcp_socket_.get(), &remote, &sosize));
        if (!accepted_socket.valid()) {
            const int error_code = lastSocketError();
            if (isReceiveTimeout(error_code)) {
                continue;
            }
            throwTunnelError("Unable to accept incoming TCP connection, accept() error %d!", error_code);
        }
        client_socket = std::move(accepted_socket);
    }
    if (!client_socket.valid()) {
        return;
    }
    if (compactOutputEnabled()) {
        logCompactMessage(LogLevel::Info, "client connected");
    } else {
        INFO("Accepted incoming connection from a remote host. Waiting for forwarded datagrams...");
    }

    // Setting up TCP timeout HEARTBEAT_PERIOD_SEC + 1 second
    if (!setSocketReceiveTimeout(client_socket.get(), kHeartbeatPeriodSeconds + 1)) {
        throwTunnelError("Could not set receive timeout on TCP socket! Error %d", lastSocketError());
    }

    sockaddr_in pub_group {};
    if (!cfg_.use_clt_grp_) { //if not reusing the multicast joined by the client then using the one set in the configuration to publish data
        pub_group.sin_family = AF_INET;
        pub_group.sin_port = htons(cfg_.udp_dst_port_);
        pub_group.sin_addr.s_addr = inet_addr(cfg_.udp_dst_ip_.c_str());
    }

    TunnelPacket tunnel_pkt {};
    char* p = reinterpret_cast<char*>(&tunnel_pkt); //a write pointer
    SocketIoSize len_read = 0;
    size_t len_to_read = kTunnelPacketPreambleLength;
    LatencyStatistics latency_statistics;
    while (!stop_token.stop_requested()) {
        assert(len_to_read != 0);
        // Reading data sent from client
        if ((len_read = recv(client_socket.get(), p, static_cast<SocketBufferLength>(len_to_read), kNoSignalFlag)) < 0) {
            if (isReceiveTimeout(lastSocketError())) {
                INFO("Client has been silent for too long. Terminating");
                return;
            } else {
                throwTunnelError("Unable to read data from TCP socket, recv() error %d!", lastSocketError());
            }
        }
        if (len_read == 0) {
            INFO("Client terminated!"); //TODO: review conditions under which this could happen
            return;
        }
        p += len_read;
        assert(p > reinterpret_cast<char*>(&tunnel_pkt));
        const size_t data_len = static_cast<size_t>(p - reinterpret_cast<char*>(&tunnel_pkt));
        if (data_len < kTunnelPacketPreambleLength) {
            len_to_read = kTunnelPacketPreambleLength - data_len;
            continue;
        }
        if (tunnel_pkt.protocol_version_ != kDtepProtocolVersion) {
            throwTunnelError("Unsupported DTEP protocol version %u (this server requires version %u)",
                  static_cast<unsigned int>(tunnel_pkt.protocol_version_), static_cast<unsigned int>(kDtepProtocolVersion));
        }
        if (tunnel_pkt.type_ == TunnelPacketType::Heartbeat) {
            p = reinterpret_cast<char*>(&tunnel_pkt);
            len_to_read = kTunnelPacketPreambleLength;
            if (compactOutputEnabled()) {
                logCompactMessage(LogLevel::Info, "heartbeat <- client");
            } else {
                INFO("Received heartbeat from client.");
            }
            latency_statistics.reportIfDue();
            continue;
        }
        if (tunnel_pkt.type_ != TunnelPacketType::Datagram) {
            throwTunnelError("Unsupported DTEP packet type %u", static_cast<unsigned int>(tunnel_pkt.type_));
        }
        if (data_len < kTunnelPacketHeaderLength) {
            len_to_read = kTunnelPacketHeaderLength - data_len;
            continue; //read enough bytes to get the whole DTEP header
        }
        if (data_len < tunnel_pkt.size()) {
            len_to_read = tunnel_pkt.size() - data_len;
            continue; //we need the whole DTEP packet before publishing it
        }
        assert(data_len == tunnel_pkt.size()); //we only read enough bytes for one complete DTEP frame
        p = reinterpret_cast<char*>(&tunnel_pkt); //we have read a full packet, we now reset the write pointer to the beginning of tunnel_pkt
        len_to_read = kTunnelPacketPreambleLength;

        if (cfg_.use_clt_grp_) { //potential for feedbackloop if both client and server are in the same subnet
//however if that were the case, there would be no need to forward the datagrams
            pub_group = {};
            pub_group.sin_family = AF_INET;
            pub_group.sin_port = htons(tunnel_pkt.udp_dst_port_);
            pub_group.sin_addr.s_addr = tunnel_pkt.udp_dst_ip_;
        }

        // Multicasting the datagrams received from the client
        if(sendto(udp_socket_.get(), reinterpret_cast<const char*>(tunnel_pkt.databuf_.data()), tunnel_pkt.datalen_, kNoSignalFlag, reinterpret_cast<struct sockaddr*>(&pub_group), sizeof(pub_group)) < 0) {
            throwTunnelError("Unable to publish multicast data, sendto() error %d!", lastSocketError());
        }
        recordDatagram(tunnel_pkt.datalen_);

        const uint64_t server_timestamp_us = currentTimestampMicroseconds();
        double latency_ms = 0.0;
        bool latency_available = false;
        if (server_timestamp_us < tunnel_pkt.client_timestamp_us_) {
            WARN("Client timestamp is ahead of the server clock; latency is unavailable for this datagram.");
        } else {
            latency_ms = static_cast<double>(server_timestamp_us - tunnel_pkt.client_timestamp_us_) / 1000.0;
            latency_available = true;
            recordLatency(latency_ms);
            latency_statistics.record(latency_ms);
            latency_statistics.reportIfDue();
        }
        if (observer_.on_datagram) {
            observer_.on_datagram({tunnel_pkt.datalen_, latency_available ? std::optional<double>(latency_ms) : std::nullopt});
        }

        //Getting group on which the datagram was published on client side
        char clt_grp_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &tunnel_pkt.udp_dst_ip_, clt_grp_ip, INET_ADDRSTRLEN);

        //Getting group on which the server is publishing the forwared datagrams
        char pub_grp_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &pub_group.sin_addr, pub_grp_ip, INET_ADDRSTRLEN);
        if (latency_available) {
            if (compactOutputEnabled()) {
                logCompactMessage(LogLevel::Info, "published %u B | %.3f ms", tunnel_pkt.datalen_, latency_ms);
            } else {
                INFO("Published to %s:%u a %u byte datagram tunneled by client. Client side group was %s:%u. Latency: %.3f ms",
                     pub_grp_ip, ntohs(pub_group.sin_port), tunnel_pkt.datalen_, clt_grp_ip, tunnel_pkt.udp_dst_port_, latency_ms);
            }
        } else {
            if (compactOutputEnabled()) {
                logCompactMessage(LogLevel::Warning, "published %u B | latency unavailable (client clock ahead)", tunnel_pkt.datalen_);
            } else {
                INFO("Published to %s:%u a %u byte datagram tunneled by client. Client side group was %s:%u. Latency: unavailable (client clock ahead)",
                     pub_grp_ip, ntohs(pub_group.sin_port), tunnel_pkt.datalen_, clt_grp_ip, tunnel_pkt.udp_dst_port_);
            }
        }
    }
}
