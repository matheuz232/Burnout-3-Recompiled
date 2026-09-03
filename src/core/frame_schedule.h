#pragma once

namespace b3r::core {

class FrameSchedule {
public:
    FrameSchedule(double rate_hz, double start_time_seconds);

    [[nodiscard]] double period_seconds() const noexcept;
    [[nodiscard]] double deadline_seconds() const noexcept;

    void reset(double start_time_seconds) noexcept;
    void advance_after_frame(double now_seconds) noexcept;

private:
    double period_seconds_{};
    double deadline_seconds_{};
};

} // namespace b3r::core
