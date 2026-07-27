#include "bored/protocol.h"

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

void append_i32(std::vector<std::uint8_t>& buffer, std::int32_t value) {
    append_u32(buffer, static_cast<std::uint32_t>(value));
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

std::int32_t read_i32(std::span<const std::uint8_t> buffer, std::size_t offset) {
    const std::uint32_t encoded_value = read_u32(buffer, offset);
    std::int64_t signed_value = encoded_value;
    if ((encoded_value & 0x80000000U) != 0) {
        signed_value -= (std::int64_t{1} << 32);
    }
    return static_cast<std::int32_t>(signed_value);
}

bool is_valid_move_axis(std::int8_t value) {
    return value >= -1 && value <= 1;
}

std::optional<std::int8_t> decode_move_axis(std::uint8_t value) {
    if (value == 0xFFU) {
        return std::int8_t{-1};
    }
    if (value <= 1U) {
        return static_cast<std::int8_t>(value);
    }
    return std::nullopt;
}

bool is_known_message_type(std::uint8_t value) {
    return value >= static_cast<std::uint8_t>(MessageType::hello) &&
           value <= static_cast<std::uint8_t>(MessageType::world_snapshot);
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

std::vector<std::uint8_t> encode_input_command_payload(const InputCommand& command) {
    if (!is_valid_move_axis(command.move_x) || !is_valid_move_axis(command.move_y)) {
        throw std::invalid_argument("input movement axes must be between -1 and 1");
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(k_input_command_size);
    append_u32(payload, command.client_tick);
    append_u32(payload, command.input_sequence);
    payload.push_back(static_cast<std::uint8_t>(command.move_x));
    payload.push_back(static_cast<std::uint8_t>(command.move_y));
    return payload;
}

std::vector<std::uint8_t> encode_world_snapshot_payload(const WorldSnapshot& snapshot) {
    if (snapshot.entities.size() > k_max_snapshot_entities) {
        throw std::invalid_argument("world snapshot exceeds the entity limit");
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(k_world_snapshot_prefix_size + snapshot.entities.size() * k_snapshot_entity_size);
    append_u32(payload, snapshot.server_tick);
    append_u32(payload, snapshot.acknowledged_input_sequence);
    append_u16(payload, static_cast<std::uint16_t>(snapshot.entities.size()));
    for (const SnapshotEntity& entity : snapshot.entities) {
        append_u32(payload, entity.client_id);
        append_i32(payload, entity.position_x_mm);
        append_i32(payload, entity.position_y_mm);
    }
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

std::optional<InputCommand> decode_input_command_payload(std::span<const std::uint8_t> payload) {
    if (payload.size() != k_input_command_size) {
        return std::nullopt;
    }

    InputCommand command;
    command.client_tick = read_u32(payload, 0);
    command.input_sequence = read_u32(payload, 4);
    const auto move_x = decode_move_axis(payload[8]);
    const auto move_y = decode_move_axis(payload[9]);
    if (!move_x.has_value() || !move_y.has_value()) {
        return std::nullopt;
    }
    command.move_x = *move_x;
    command.move_y = *move_y;
    return command;
}

std::optional<WorldSnapshot> decode_world_snapshot_payload(std::span<const std::uint8_t> payload) {
    if (payload.size() < k_world_snapshot_prefix_size) {
        return std::nullopt;
    }

    const std::uint16_t entity_count = read_u16(payload, 8);
    if (entity_count > k_max_snapshot_entities ||
        payload.size() != k_world_snapshot_prefix_size + entity_count * k_snapshot_entity_size) {
        return std::nullopt;
    }

    WorldSnapshot snapshot;
    snapshot.server_tick = read_u32(payload, 0);
    snapshot.acknowledged_input_sequence = read_u32(payload, 4);
    snapshot.entities.reserve(entity_count);
    for (std::size_t index = 0; index < entity_count; ++index) {
        const std::size_t offset = k_world_snapshot_prefix_size + index * k_snapshot_entity_size;
        snapshot.entities.push_back({
            .client_id = read_u32(payload, offset),
            .position_x_mm = read_i32(payload, offset + 4),
            .position_y_mm = read_i32(payload, offset + 8),
        });
    }
    return snapshot;
}

} // namespace bored::protocol
