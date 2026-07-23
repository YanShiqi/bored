#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bored::protocol {

constexpr std::uint16_t k_magic = 0x424F; // ASCII "BO"
constexpr std::uint8_t k_version = 1;
// v1 固定包头为 10 字节；1200 字节上限给未来的 UDP/IP 头部和路径 MTU 留出余量。
constexpr std::size_t k_header_size = 10;
constexpr std::size_t k_max_datagram_size = 1200;

enum class MessageType : std::uint8_t {
    hello = 1,
    hello_ack = 2,
    ping = 3,
    pong = 4,
};

struct PacketHeader {
    std::uint16_t magic = k_magic;
    std::uint8_t version = k_version;
    MessageType message_type = MessageType::hello;
    std::uint32_t sequence = 0; // 发送方本地递增；v1 仅用于诊断，尚不提供可靠性保证。
    std::uint16_t payload_length = 0;
};

struct Packet {
    // decode_packet 成功返回时，header 的长度字段已与 payload 的实际长度一致。
    PacketHeader header;
    std::vector<std::uint8_t> payload;
};

// 编码和解码始终使用网络字节序，避免两端依赖 CPU 的本地字节序。
[[nodiscard]] std::vector<std::uint8_t> encode_packet(
    MessageType message_type,
    std::uint32_t sequence,
    std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<Packet> decode_packet(
    std::span<const std::uint8_t> datagram,
    std::string& error);

[[nodiscard]] std::vector<std::uint8_t> encode_u32_payload(std::uint32_t value);
[[nodiscard]] std::vector<std::uint8_t> encode_u64_payload(std::uint64_t value);
[[nodiscard]] std::vector<std::uint8_t> encode_hello_ack_payload(
    std::uint32_t client_nonce,
    std::uint32_t client_id);
[[nodiscard]] std::optional<std::uint32_t> decode_u32_payload(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<std::uint64_t> decode_u64_payload(std::span<const std::uint8_t> payload);

} // namespace bored::protocol
