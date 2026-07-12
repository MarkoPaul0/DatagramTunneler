#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

constexpr std::size_t kMaxDatagramLength = 1472;
constexpr std::size_t kTunnelPacketHeaderLength = 9;

enum class TunnelPacketType : uint8_t {
    Heartbeat = 0,
    Datagram = 1
};

#pragma pack(push, 1)
struct TunnelPacket {
    TunnelPacketType type_;
    uint32_t udp_dst_ip_;
    uint16_t udp_dst_port_;
    uint16_t datalen_;
    std::array<std::byte, kMaxDatagramLength> databuf_ {};

    std::size_t size() const {
        if (type_ == TunnelPacketType::Heartbeat) {
            return sizeof(type_);
        }
        return static_cast<std::size_t>(datalen_) + kTunnelPacketHeaderLength;
    }
};
#pragma pack(pop)

static_assert(sizeof(TunnelPacket) == 1481, "The TunnelPacket struct should be 1481 bytes long!");
