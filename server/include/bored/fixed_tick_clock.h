#pragma once

#include <chrono>
#include <cstdint>

namespace bored {

// 固定 Tick 调度器：始终以初始时间轴推进，不因某一帧迟到而重置节奏。
class FixedTickClock {
public:
    using Clock = std::chrono::steady_clock;
    using Duration = Clock::duration;
    using TimePoint = Clock::time_point;

    explicit FixedTickClock(std::uint32_t tick_rate);

    // 显式设定第一个 Tick 的计划时间；开始前不能查询下一次 Tick 或推进时钟。
    void start(TimePoint start_time);

    [[nodiscard]] bool started() const noexcept;
    // 当当前时间到达或超过计划时间时，调用方应执行一次模拟并随后调用 advance()。
    [[nodiscard]] bool is_tick_due(TimePoint now) const noexcept;
    [[nodiscard]] Duration interval() const noexcept;
    [[nodiscard]] std::uint64_t tick_index() const noexcept;
    // 返回下一次模拟的计划时间，而不是根据当前时间重新计算的时间。
    [[nodiscard]] TimePoint next_tick_time() const;
    // 返回当前时间相对计划 Tick 的迟到量；未迟到时为零。
    [[nodiscard]] Duration lateness(TimePoint now) const noexcept;

    // 推进一个固定周期，保留原始节奏以便统计和处理积压 Tick。
    void advance();

private:
    Duration interval_;
    TimePoint next_tick_time_{};
    std::uint64_t tick_index_ = 0;
    bool started_ = false;
};

} // namespace bored
