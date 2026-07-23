#include "bored/fixed_tick_clock.h"

#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

using bored::FixedTickClock;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{message.data()};
    }
}

void test_rejects_zero_tick_rate() {
    bool threw = false;

    try {
        static_cast<void>(FixedTickClock{0});
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    expect(threw, "zero tick rate should be rejected");
}

void test_first_tick_is_due_at_start() {
    FixedTickClock clock{30};
    const FixedTickClock::TimePoint start_time{};

    clock.start(start_time);

    expect(clock.started(), "clock should be started");
    expect(clock.tick_index() == 0, "tick index should start at zero");
    expect(clock.is_tick_due(start_time), "first tick should be due at start");
}

void test_clock_preserves_fixed_cadence() {
    FixedTickClock clock{30};
    const FixedTickClock::TimePoint start_time{};

    clock.start(start_time);
    const auto interval = clock.interval();

    clock.advance();
    expect(clock.next_tick_time() == start_time + interval, "first advance should use one interval");
    expect(!clock.is_tick_due(start_time + interval - std::chrono::nanoseconds{1}),
           "tick should not be due before its deadline");

    const auto delayed_time = start_time + (interval * 3) + std::chrono::milliseconds{5};
    expect(clock.is_tick_due(delayed_time), "late tick should be due");

    clock.advance();
    expect(clock.next_tick_time() == start_time + (interval * 2),
           "late ticks must preserve their original cadence");
    expect(clock.lateness(delayed_time) == delayed_time - (start_time + (interval * 2)),
           "lateness should be measured against the scheduled deadline");
}

void test_advance_requires_start() {
    FixedTickClock clock{30};
    bool threw = false;

    try {
        clock.advance();
    } catch (const std::logic_error&) {
        threw = true;
    }

    expect(threw, "advance should require an explicit start");
}

struct TestCase {
    std::string_view name;
    std::function<void()> run;
};

} // namespace

int main() {
    const TestCase test_cases[] = {
        {"rejects zero tick rate", test_rejects_zero_tick_rate},
        {"first tick is due at start", test_first_tick_is_due_at_start},
        {"clock preserves fixed cadence", test_clock_preserves_fixed_cadence},
        {"advance requires start", test_advance_requires_start},
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
