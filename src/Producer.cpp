#include "Producer.h"

#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

#include "Log.h"
#include "Network.h"
#include "Protocol.h"

namespace {

Socket createDatagramSocket() {
    Socket socket_handle(socket(AF_INET, SOCK_DGRAM, 0));
    if (!socket_handle.valid()) {
        throw std::runtime_error("could not create UDP socket (error " + std::to_string(lastSocketError()) + ")");
    }
    return socket_handle;
}

} // namespace


DatagramProducer::DatagramProducer(const DatagramTunneler::Config& config, Options options)
    : config_(config), options_(std::move(options)) {
    if (!config_.is_client_) {
        throw std::runtime_error("producer requires a client tunnel");
    }
    if (options_.interval_ms == 0) {
        throw std::runtime_error("--interval-ms must be greater than zero");
    }
    if (options_.payload_prefix.empty()) {
        throw std::runtime_error("--payload-prefix must not be empty");
    }
}


void DatagramProducer::run() {
    Socket udp_socket = createDatagramSocket();

    in_addr interface_address {};
    if (inet_pton(AF_INET, config_.udp_iface_ip_.c_str(), &interface_address) != 1) {
        throw std::runtime_error("producer has an invalid UDP interface address");
    }
    if (setSocketOption(udp_socket.get(), IPPROTO_IP, IP_MULTICAST_IF, interface_address) < 0) {
        throw std::runtime_error("could not set multicast producer interface (error " + std::to_string(lastSocketError()) + ")");
    }

    const unsigned char enable_loopback = 1;
    if (setSocketOption(udp_socket.get(), IPPROTO_IP, IP_MULTICAST_LOOP, enable_loopback) < 0) {
        throw std::runtime_error("could not enable multicast loopback (error " + std::to_string(lastSocketError()) + ")");
    }

    sockaddr_in destination {};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(config_.udp_dst_port_);
    if (inet_pton(AF_INET, config_.udp_dst_ip_.c_str(), &destination.sin_addr) != 1) {
        throw std::runtime_error("producer has an invalid multicast group address");
    }

    INFO("Producer sending to multicast %s:%u via %s every %u ms%s",
         config_.udp_dst_ip_.c_str(), static_cast<unsigned int>(config_.udp_dst_port_),
         config_.udp_iface_ip_.c_str(), options_.interval_ms,
         options_.count == 0 ? " until interrupted" : "");

    for (std::size_t number = 1; options_.count == 0 || number <= options_.count; ++number) {
        const std::string payload = options_.payload_prefix + " #" + std::to_string(number);
        if (payload.size() > kMaxDatagramLength ||
            payload.size() > static_cast<std::size_t>(std::numeric_limits<SocketBufferLength>::max())) {
            throw std::runtime_error("producer payload exceeds the maximum datagram length");
        }

        const SocketIoSize bytes_sent = sendto(
            udp_socket.get(), payload.data(), static_cast<SocketBufferLength>(payload.size()), kNoSignalFlag,
            reinterpret_cast<const sockaddr*>(&destination), static_cast<SocketAddressLength>(sizeof(destination)));
        if (bytes_sent < 0 || static_cast<std::size_t>(bytes_sent) != payload.size()) {
            throw std::runtime_error("could not send dummy datagram (error " + std::to_string(lastSocketError()) + ")");
        }

        INFO("Sent %s", payload.c_str());
        if (options_.count == 0 || number < options_.count) {
            std::this_thread::sleep_for(std::chrono::milliseconds(options_.interval_ms));
        }
    }
}
