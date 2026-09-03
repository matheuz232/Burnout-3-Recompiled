#pragma once

#include "core/frame_schedule.h"

#include <windows.h>

namespace b3r::platform::windows {

class WindowsFramePacer {
public:
    explicit WindowsFramePacer(double target_hz = 120.0);
    ~WindowsFramePacer();

    WindowsFramePacer(const WindowsFramePacer&) = delete;
    WindowsFramePacer& operator=(const WindowsFramePacer&) = delete;

    [[nodiscard]] double wait_for_next_frame();
    [[nodiscard]] double target_period_seconds() const noexcept;
    [[nodiscard]] bool using_high_resolution_timer() const noexcept;

private:
    HANDLE timer_{};
    bool high_resolution_timer_{};
    bool fallback_timer_resolution_{};
    double spin_threshold_seconds_{0.00075};
    b3r::core::FrameSchedule schedule_;
};

} // namespace b3r::platform::windows
