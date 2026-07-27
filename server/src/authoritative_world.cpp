#include "bored/authoritative_world.h"

#include <algorithm>
#include <stdexcept>

namespace bored {
namespace {

constexpr std::int32_t k_player_speed_mm_per_second = 3'000;
constexpr std::int32_t k_world_boundary_mm = 9'000;

bool is_valid_move_axis(std::int8_t value) {
    return value >= -1 && value <= 1;
}

} // namespace

AuthoritativeWorld::AuthoritativeWorld(std::uint32_t tick_rate) {
    if (tick_rate == 0 || tick_rate > static_cast<std::uint32_t>(k_player_speed_mm_per_second)) {
        throw std::invalid_argument("tick rate cannot represent the configured movement speed");
    }

    movement_per_tick_mm_ = k_player_speed_mm_per_second / static_cast<std::int32_t>(tick_rate);
}

void AuthoritativeWorld::add_player(std::uint32_t client_id) {
    players_.try_emplace(client_id);
}

bool AuthoritativeWorld::submit_input(std::uint32_t client_id, const protocol::InputCommand& command) {
    const auto player = players_.find(client_id);
    if (player == players_.end() || !is_valid_move_axis(command.move_x) || !is_valid_move_axis(command.move_y)) {
        return false;
    }

    if (command.input_sequence <= player->second.last_input_sequence) {
        return false;
    }

    // client_tick 先保留在协议中用于后续预测与延迟诊断；权威移动只按服务器 Tick 推进。
    player->second.move_x = command.move_x;
    player->second.move_y = command.move_y;
    player->second.last_input_sequence = command.input_sequence;
    return true;
}

void AuthoritativeWorld::simulate_tick() {
    for (auto& [client_id, player] : players_) {
        static_cast<void>(client_id);
        player.position_x_mm = std::clamp(
            player.position_x_mm + player.move_x * movement_per_tick_mm_,
            -k_world_boundary_mm,
            k_world_boundary_mm);
        player.position_y_mm = std::clamp(
            player.position_y_mm + player.move_y * movement_per_tick_mm_,
            -k_world_boundary_mm,
            k_world_boundary_mm);
    }
}

std::uint32_t AuthoritativeWorld::last_input_sequence(std::uint32_t client_id) const noexcept {
    const auto player = players_.find(client_id);
    return player == players_.end() ? 0 : player->second.last_input_sequence;
}

std::vector<protocol::SnapshotEntity> AuthoritativeWorld::snapshot_entities() const {
    std::vector<protocol::SnapshotEntity> entities;
    entities.reserve(players_.size());
    for (const auto& [client_id, player] : players_) {
        entities.push_back({
            .client_id = client_id,
            .position_x_mm = player.position_x_mm,
            .position_y_mm = player.position_y_mm,
        });
    }
    return entities;
}

std::size_t AuthoritativeWorld::player_count() const noexcept {
    return players_.size();
}

} // namespace bored
