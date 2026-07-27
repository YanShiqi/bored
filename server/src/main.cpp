#include "bored/authoritative_world.h"
#include "bored/fixed_tick_clock.h"
#include "bored/protocol.h"
#include "bored/udp_socket.h"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

struct Options {
    std::uint32_t tick_rate = 30;
    std::uint32_t snapshot_rate = 15;
    std::uint64_t tick_limit = 0;
    std::uint16_t port = 39000;
};

struct ClientSession {
    std::uint32_t id = 0;
    bored::UdpEndpoint endpoint;
};

struct ClientAssignment {
    std::uint32_t id = 0;
    bool is_new = false;
};

class ClientRegistry {
public:
    [[nodiscard]] std::optional<ClientAssignment> assign(const bored::UdpEndpoint& endpoint) {
        const auto existing = clients_.find(endpoint.label);
        if (existing != clients_.end()) {
            existing->second.endpoint = endpoint;
            return ClientAssignment{.id = existing->second.id, .is_new = false};
        }

        if (clients_.size() >= bored::protocol::k_max_snapshot_entities) {
            return std::nullopt;
        }

        const std::uint32_t client_id = next_client_id_++;
        clients_.emplace(endpoint.label, ClientSession{.id = client_id, .endpoint = endpoint});
        return ClientAssignment{.id = client_id, .is_new = true};
    }

    [[nodiscard]] const ClientSession* find(const std::string& endpoint_label) const {
        const auto session = clients_.find(endpoint_label);
        return session == clients_.end() ? nullptr : &session->second;
    }

    [[nodiscard]] const std::unordered_map<std::string, ClientSession>& sessions() const noexcept {
        return clients_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return clients_.size();
    }

private:
    // v2 用 IP:端口作为临时会话键；重连和 NAT 变化会获得新 ID，尚无账号或迁移逻辑。
    std::unordered_map<std::string, ClientSession> clients_;
    std::uint32_t next_client_id_ = 1;
};

class SnapshotScheduler {
public:
    SnapshotScheduler(std::uint32_t simulation_rate, std::uint32_t snapshot_rate)
        : simulation_rate_(simulation_rate), snapshot_rate_(snapshot_rate) {}

    [[nodiscard]] bool advance() {
        // 累积器支持不整除的频率组合，例如 64 Hz 模拟与 20 Hz 快照。
        accumulated_rate_ += snapshot_rate_;
        if (accumulated_rate_ < simulation_rate_) {
            return false;
        }

        accumulated_rate_ -= simulation_rate_;
        return true;
    }

private:
    std::uint32_t simulation_rate_ = 0; // 每秒执行的权威模拟 Tick 数，作为累积器的分母。
    std::uint32_t snapshot_rate_ = 0;   // 每秒期望发送的快照数，每个模拟 Tick 累加一次。
    std::uint32_t accumulated_rate_ = 0; // 尚未换算为一次快照发送机会的频率余量。
};

struct NetworkStatistics {
    std::uint64_t accepted_input_packets = 0; // 当前统计周期内被权威世界接受的输入包数量。
    std::uint64_t dropped_input_packets = 0;  // 当前统计周期内因会话、格式或序号无效而丢弃的输入包数量。
    std::uint64_t snapshot_packets = 0;       // 当前统计周期内成功发出的世界快照包数量。
    std::uint64_t snapshot_bytes = 0;         // 当前统计周期内快照包的总字节数，包含协议包头。

    void reset() noexcept {
        *this = {};
    }
};

std::optional<std::uint64_t> parse_positive_integer(std::string_view value) {
    // from_chars 不分配内存，也不会受区域设置影响，适合服务端启动参数解析。
    std::uint64_t parsed_value = 0;
    const auto [parse_end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed_value);

    if (error != std::errc{} || parse_end != value.data() + value.size() || parsed_value == 0) {
        return std::nullopt;
    }

    return parsed_value;
}

void print_usage() {
    std::cout << "Usage: bored_server [--port <port>] [--tick-rate <hz>] [--snapshot-rate <hz>] [--ticks <count>]\n";
}

Options parse_options(int argc, char* argv[]) {
    Options options;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];

        if (argument == "--help") {
            print_usage();
            std::exit(0);
        }

        if (argument != "--port" && argument != "--tick-rate" && argument != "--snapshot-rate" &&
            argument != "--ticks") {
            throw std::invalid_argument("unknown argument: " + std::string{argument});
        }

        if (++index >= argc) {
            throw std::invalid_argument("missing value for " + std::string{argument});
        }

        const auto parsed_value = parse_positive_integer(argv[index]);
        if (!parsed_value.has_value()) {
            throw std::invalid_argument("expected a positive integer for " + std::string{argument});
        }

        if (argument == "--port") {
            if (*parsed_value > UINT16_MAX) {
                throw std::invalid_argument("port is too large");
            }
            options.port = static_cast<std::uint16_t>(*parsed_value);
        } else if (argument == "--tick-rate") {
            if (*parsed_value > UINT32_MAX) {
                throw std::invalid_argument("tick rate is too large");
            }
            options.tick_rate = static_cast<std::uint32_t>(*parsed_value);
        } else if (argument == "--snapshot-rate") {
            if (*parsed_value > UINT32_MAX) {
                throw std::invalid_argument("snapshot rate is too large");
            }
            options.snapshot_rate = static_cast<std::uint32_t>(*parsed_value);
        } else {
            options.tick_limit = *parsed_value;
        }
    }

    if (options.snapshot_rate > options.tick_rate) {
        throw std::invalid_argument("snapshot rate cannot exceed tick rate");
    }

    return options;
}

bool send_packet(
    bored::UdpSocket& socket,
    const bored::UdpEndpoint& endpoint,
    bored::protocol::MessageType message_type,
    std::uint32_t& server_sequence,
    std::span<const std::uint8_t> payload) {
    // 服务端序号独立于客户端序号，便于以后分别分析上下行丢包和乱序。
    const auto response = bored::protocol::encode_packet(message_type, server_sequence++, payload);
    std::string error;
    if (!socket.send_to(endpoint, response, error)) {
        std::cerr << "bored_server send error to " << endpoint.label << ": " << error << "\n";
        return false;
    }
    return true;
}

void process_network(
    bored::UdpSocket& socket,
    ClientRegistry& clients,
    bored::AuthoritativeWorld& world,
    std::uint32_t& server_sequence,
    NetworkStatistics& statistics) {
    // 单 Tick 限额防止网络突发独占模拟预算；剩余数据报留给下一 Tick 处理。
    constexpr int max_datagrams_per_tick = 32;

    for (int index = 0; index < max_datagrams_per_tick; ++index) {
        const auto datagram = socket.receive();
        if (!datagram.has_value()) {
            return;
        }

        std::string decode_error;
        const auto packet = bored::protocol::decode_packet(datagram->bytes, decode_error);
        if (!packet.has_value()) {
            // 无效 UDP 数据报只丢弃，不能影响权威模拟循环。
            continue;
        }

        switch (packet->header.message_type) {
        case bored::protocol::MessageType::hello: {
            const auto nonce = bored::protocol::decode_u32_payload(packet->payload);
            if (!nonce.has_value()) {
                continue;
            }

            const auto assignment = clients.assign(datagram->sender);
            if (!assignment.has_value()) {
                continue;
            }

            if (assignment->is_new) {
                world.add_player(assignment->id);
            }
            // 回显 nonce 让客户端确认该响应属于本次握手，而非旧 UDP 包。
            const auto response_payload = bored::protocol::encode_hello_ack_payload(*nonce, assignment->id);
            send_packet(
                socket,
                datagram->sender,
                bored::protocol::MessageType::hello_ack,
                server_sequence,
                response_payload);
            if (assignment->is_new) {
                std::cout << "client=" << assignment->id << " hello from " << datagram->sender.label << "\n";
            }
            break;
        }
        case bored::protocol::MessageType::input_command: {
            const ClientSession* session = clients.find(datagram->sender.label);
            const auto input = bored::protocol::decode_input_command_payload(packet->payload);
            if (session == nullptr || !input.has_value() || !world.submit_input(session->id, *input)) {
                ++statistics.dropped_input_packets;
                continue;
            }
            ++statistics.accepted_input_packets;
            break;
        }
        case bored::protocol::MessageType::ping:
            if (bored::protocol::decode_u64_payload(packet->payload).has_value()) {
                send_packet(
                    socket,
                    datagram->sender,
                    bored::protocol::MessageType::pong,
                    server_sequence,
                    packet->payload);
            }
            break;
        case bored::protocol::MessageType::hello_ack:
        case bored::protocol::MessageType::pong:
        case bored::protocol::MessageType::world_snapshot:
            // 客户端专用消息到达服务端时忽略，避免形成意外的响应回路。
            break;
        }
    }
}

void broadcast_snapshot(
    bored::UdpSocket& socket,
    const ClientRegistry& clients,
    const bored::AuthoritativeWorld& world,
    std::uint32_t server_tick,
    std::uint32_t& server_sequence,
    NetworkStatistics& statistics) {
    const auto entities = world.snapshot_entities();
    for (const auto& client_entry : clients.sessions()) {
        const ClientSession& client = client_entry.second;
        const bored::protocol::WorldSnapshot snapshot = {
            .server_tick = server_tick,
            .acknowledged_input_sequence = world.last_input_sequence(client.id),
            .entities = entities,
        };
        const auto payload = bored::protocol::encode_world_snapshot_payload(snapshot);
        if (send_packet(
                socket,
                client.endpoint,
                bored::protocol::MessageType::world_snapshot,
                server_sequence,
                payload)) {
            ++statistics.snapshot_packets;
            statistics.snapshot_bytes += bored::protocol::k_header_size + payload.size();
        }
    }
}

long long to_microseconds(bored::FixedTickClock::Duration duration) {
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const Options options = parse_options(argc, argv);
        bored::FixedTickClock tick_clock{options.tick_rate};
        bored::AuthoritativeWorld world{options.tick_rate};
        SnapshotScheduler snapshot_scheduler{options.tick_rate, options.snapshot_rate};
        bored::UdpSocket socket;
        std::string socket_error;
        if (!socket.open(options.port, socket_error)) {
            throw std::runtime_error("could not bind UDP port " + std::to_string(options.port) + ": " + socket_error);
        }

        ClientRegistry clients;
        NetworkStatistics statistics;
        // 发送序号按方向独立递增，v2 不要求与收到包的序号对应。
        std::uint32_t server_sequence = 1;
        tick_clock.start(bored::FixedTickClock::Clock::now());

        std::cout << "bored_server listening on UDP " << options.port << " at " << options.tick_rate
                  << " Hz with " << options.snapshot_rate << " Hz snapshots";
        if (options.tick_limit != 0) {
            std::cout << " for " << options.tick_limit << " ticks";
        }
        std::cout << "\n";

        while (options.tick_limit == 0 || tick_clock.tick_index() < options.tick_limit) {
            const auto now = bored::FixedTickClock::Clock::now();
            if (!tick_clock.is_tick_due(now)) {
                // 没到计划时间时休眠，避免空转占满一个 CPU 核心。
                std::this_thread::sleep_until(tick_clock.next_tick_time());
                continue;
            }

            const auto tick_started_at = now;
            const auto lateness = tick_clock.lateness(tick_started_at);

            // 收包有上限且 socket 非阻塞，网络突发不能拖慢后续 Tick。
            process_network(socket, clients, world, server_sequence, statistics);
            world.simulate_tick();

            const auto tick_duration = bored::FixedTickClock::Clock::now() - tick_started_at;
            // advance 按原计划时间推进，不能以 now 重置，否则迟到会累积成节奏漂移。
            tick_clock.advance();
            if (snapshot_scheduler.advance()) {
                broadcast_snapshot(
                    socket,
                    clients,
                    world,
                    static_cast<std::uint32_t>(tick_clock.tick_index()),
                    server_sequence,
                    statistics);
            }

            // 每秒输出一次，既便于观察调度，又不会让日志量随 Tick 频率线性增长。
            const bool completed_requested_run =
                options.tick_limit != 0 && tick_clock.tick_index() == options.tick_limit;
            const bool completed_second = tick_clock.tick_index() % options.tick_rate == 0;
            if (completed_requested_run || completed_second) {
                std::cout << "tick=" << tick_clock.tick_index()
                          << " duration_us=" << to_microseconds(tick_duration)
                          << " lateness_us=" << to_microseconds(lateness)
                          << " clients=" << clients.size()
                          << " players=" << world.player_count()
                          << " inputs=" << statistics.accepted_input_packets
                          << " dropped_inputs=" << statistics.dropped_input_packets
                          << " snapshots=" << statistics.snapshot_packets
                          << " snapshot_bytes=" << statistics.snapshot_bytes << "\n";
                statistics.reset();
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "bored_server error: " << error.what() << "\n";
        print_usage();
        return 1;
    }

    return 0;
}
