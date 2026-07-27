#pragma once

#include "bored/protocol.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace bored {

class AuthoritativeWorld {
public:
    explicit AuthoritativeWorld(std::uint32_t tick_rate);

    void add_player(std::uint32_t client_id);
    [[nodiscard]] bool submit_input(std::uint32_t client_id, const protocol::InputCommand& command);
    void simulate_tick();

    [[nodiscard]] std::uint32_t last_input_sequence(std::uint32_t client_id) const noexcept;
    [[nodiscard]] std::vector<protocol::SnapshotEntity> snapshot_entities() const;
    [[nodiscard]] std::size_t player_count() const noexcept;

private:
    struct PlayerState {
        std::int32_t position_x_mm = 0;
        std::int32_t position_y_mm = 0;
        std::int8_t move_x = 0;
        std::int8_t move_y = 0;
        std::uint32_t last_input_sequence = 0;
    };

    // map 让每份快照按 client_id 稳定排序，便于跨端测试和后续差量同步。
    std::map<std::uint32_t, PlayerState> players_;
    std::int32_t movement_per_tick_mm_ = 0;
};

} // namespace bored
