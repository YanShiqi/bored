#include "bored/protocol.h"

#include <algorithm>
#include <stdexcept>

namespace bored::protocol {
namespace {

void append_u16(std::vector<std::uint8_t>& buffer, std::uint16_t value) {
    // 手动拆字节，避免主机字节序和平台网络转换函数渗入协议实现。
    buffer.push_back(static_cast<std::uint8_t>(value >> 8));
    buffer.push_back(static_cast<std::uint8_t>(value));
}

void append_u32(std::vector<std::uint8_t>& buffer, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        buffer.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void append_u64(std::vector<std::uint8_t>& buffer, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        buffer.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint16_t read_u16(std::span<const std::uint8_t> buffer, std::size_t offset) {
    return static_cast<std::uint16_t>(buffer[offset] << 8) |
           static_cast<std::uint16_t>(buffer[offset + 1]);
}

std::uint32_t read_u32(std::span<const std::uint8_t> buffer, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value = (value << 8) | buffer[offset + index];
    }
    return value;
}

std::uint64_t read_u64(std::span<const std::uint8_t> buffer, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8) | buffer[offset + index];
    }
    return value;
}

bool is_known_message_type(std::uint8_t value) {
    return value >= static_cast<std::uint8_t>(MessageType::hello) &&
           value <= static_cast<std::uint8_t>(MessageType::pong);
}

} // namespace

std::vector<std::uint8_t> encode_packet(
    MessageType message_type,
    std::uint32_t sequence,
    std::span<const std::uint8_t> payload) {
    if (payload.size() > k_max_datagram_size - k_header_size) {
        throw std::invalid_argument("payload exceeds the protocol datagram limit");
    }

    std::vector<std::uint8_t> packet;
    packet.reserve(k_header_size + payload.size());
    append_u16(packet, k_magic);
    packet.push_back(k_version);
    packet.push_back(static_cast<std::uint8_t>(message_type));
    append_u32(packet, sequence);
    append_u16(packet, static_cast<std::uint16_t>(payload.size()));
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

std::optional<Packet> decode_packet(std::span<const std::uint8_t> datagram, std::string& error) {
    if (datagram.size() < k_header_size) {
        error = "datagram is smaller than the protocol header";
        return std::nullopt;
    }

    if (datagram.size() > k_max_datagram_size) {
        error = "datagram exceeds the protocol limit";
        return std::nullopt;
    }

    const std::uint16_t magic = read_u16(datagram, 0);
    if (magic != k_magic) {
        error = "unexpected packet magic";
        return std::nullopt;
    }

    if (datagram[2] != k_version) {
        error = "unsupported protocol version";
        return std::nullopt;
    }

    if (!is_known_message_type(datagram[3])) {
        error = "unknown message type";
        return std::nullopt;
    }

    const std::uint16_t payload_length = read_u16(datagram, 8);
    if (datagram.size() != k_header_size + payload_length) {
        // 必须精确相等，拒绝截断包和藏在合法负载后的额外字节。
        error = "payload length does not match datagram size";
        return std::nullopt;
    }

    Packet packet;
    packet.header = {
        .magic = magic,
        .version = datagram[2],
        .message_type = static_cast<MessageType>(datagram[3]),
        .sequence = read_u32(datagram, 4),
        .payload_length = payload_length,
    };
    packet.payload.assign(datagram.begin() + static_cast<std::ptrdiff_t>(k_header_size), datagram.end());
    return packet;
}

std::vector<std::uint8_t> encode_u32_payload(std::uint32_t value) {
    std::vector<std::uint8_t> payload;
    payload.reserve(4);
    append_u32(payload, value);
    return payload;
}

std::vector<std::uint8_t> encode_u64_payload(std::uint64_t value) {
    std::vector<std::uint8_t> payload;
    payload.reserve(8);
    append_u64(payload, value);
    return payload;
}

std::vector<std::uint8_t> encode_hello_ack_payload(std::uint32_t client_nonce, std::uint32_t client_id) {
    std::vector<std::uint8_t> payload;
    payload.reserve(8);
    append_u32(payload, client_nonce);
    append_u32(payload, client_id);
    return payload;
}

std::optional<std::uint32_t> decode_u32_payload(std::span<const std::uint8_t> payload) {
    // Hello 的 nonce 必须恰好占 4 字节，不能接受带尾随数据的近似格式。
    if (payload.size() != 4) {
        return std::nullopt;
    }

    return read_u32(payload, 0);
}

std::optional<std::uint64_t> decode_u64_payload(std::span<const std::uint8_t> payload) {
    // Ping/Pong 只回显时间戳，固定长度让 RTT 计算没有歧义。
    if (payload.size() != 8) {
        return std::nullopt;
    }

    return read_u64(payload, 0);
}

} // namespace bored::protocol
