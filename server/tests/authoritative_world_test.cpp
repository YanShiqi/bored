#include "bored/authoritative_world.h"

#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

void test_applies_server_authoritative_movement() {
    bored::AuthoritativeWorld world{30};
    world.add_player(1);
    const bored::protocol::InputCommand move_right = {
        .client_tick = 1,
        .input_sequence = 1,
        .move_x = 1,
        .move_y = 0,
    };

    expect(world.submit_input(1, move_right), "new input should be accepted");
    world.simulate_tick();

    const auto entities = world.snapshot_entities();
    expect(entities.size() == 1, "snapshot should include the connected player");
    expect(entities[0].position_x_mm == 100 && entities[0].position_y_mm == 0,
           "30 Hz simulation should move at the configured authoritative speed");
}

void test_rejects_unknown_and_stale_input() {
    bored::AuthoritativeWorld world{30};
    const bored::protocol::InputCommand command = {
        .client_tick = 1,
        .input_sequence = 1,
        .move_x = 0,
        .move_y = 1,
    };

    expect(!world.submit_input(99, command), "unknown player must not control the world");
    world.add_player(1);
    expect(world.submit_input(1, command), "first input should be accepted");
    expect(!world.submit_input(1, command), "duplicate input sequence must be rejected");
}

void test_preserves_snapshot_order() {
    bored::AuthoritativeWorld world{30};
    world.add_player(7);
    world.add_player(2);

    const auto entities = world.snapshot_entities();
    expect(entities.size() == 2, "snapshot should contain both players");
    expect(entities[0].client_id == 2 && entities[1].client_id == 7,
           "snapshot entity order should be stable by client id");
}

struct TestCase {
    std::string_view name;
    std::function<void()> run;
};

} // namespace

int main() {
    const TestCase test_cases[] = {
        {"applies server authoritative movement", test_applies_server_authoritative_movement},
        {"rejects unknown and stale input", test_rejects_unknown_and_stale_input},
        {"preserves snapshot order", test_preserves_snapshot_order},
    };

    for (const TestCase& test_case : test_cases) {
        try {
            test_case.run();
            std::cout << "PASS " << test_case.name << "\n";
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << test_case.name << ": " << error.what() << "\n";
            return 1;
        }
    }

    return 0;
}
