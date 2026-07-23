#include "bored/fixed_tick_clock.h"

#include <stdexcept>

namespace bored {

FixedTickClock::FixedTickClock(std::uint32_t tick_rate) {
    if (tick_rate == 0) {
        throw std::invalid_argument("tick rate must be greater than zero");
    }

    const auto interval_in_nanoseconds = std::chrono::nanoseconds{1'000'000'000} / tick_rate;
    interval_ = std::chrono::duration_cast<Duration>(interval_in_nanoseconds);
    if (interval_ <= Duration::zero()) {
        throw std::invalid_argument("tick rate is too high for the selected clock");
    }
}

void FixedTickClock::start(TimePoint start_time) {
    next_tick_time_ = start_time;
    tick_index_ = 0;
    started_ = true;
}

bool FixedTickClock::started() const noexcept {
    return started_;
}

bool FixedTickClock::is_tick_due(TimePoint now) const noexcept {
    return started_ && now >= next_tick_time_;
}

FixedTickClock::Duration FixedTickClock::interval() const noexcept {
    return interval_;
}

std::uint64_t FixedTickClock::tick_index() const noexcept {
    return tick_index_;
}

FixedTickClock::TimePoint FixedTickClock::next_tick_time() const {
    if (!started_) {
        throw std::logic_error("fixed tick clock has not been started");
    }

    return next_tick_time_;
}

FixedTickClock::Duration FixedTickClock::lateness(TimePoint now) const noexcept {
    if (!started_ || now <= next_tick_time_) {
        return Duration::zero();
    }

    return now - next_tick_time_;
}

void FixedTickClock::advance() {
    if (!started_) {
        throw std::logic_error("fixed tick clock has not been started");
    }

    next_tick_time_ += interval_;
    ++tick_index_;
}

} // namespace bored
