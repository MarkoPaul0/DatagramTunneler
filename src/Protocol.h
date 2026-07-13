#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

constexpr std::size_t kMaxDatagramLength = 1472;
constexpr std::size_t kTunnelPacketPreambleLength = 2;
constexpr std::size_t kTunnelPacketHeaderLength = 18;
constexpr uint8_t kDtepProtocolVersion = 2;

enum class TunnelPacketType : uint8_t {
    Heartbeat = 0,
    Datagram = 1,
};

#pragma pack(push, 1)
struct TunnelPacket {
    TunnelPacketType type_;
    uint8_t protocol_version_;
    uint32_t udp_dst_ip_;
    uint16_t udp_dst_port_;
    uint16_t datalen_;
    uint64_t client_timestamp_us_;
    std::array<std::byte, kMaxDatagramLength> databuf_ {};

    std::size_t size() const {
        if (type_ == TunnelPacketType::Heartbeat) {
            return kTunnelPacketPreambleLength;
        }
        return static_cast<std::size_t>(datalen_) + kTunnelPacketHeaderLength;
    }
};
#pragma pack(pop)

static_assert(sizeof(TunnelPacket) == 1490, "The TunnelPacket struct should be 1490 bytes long!");
