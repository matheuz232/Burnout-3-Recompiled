#include "core/frame_schedule.h"

#include <stdexcept>

namespace b3r::core {

FrameSchedule::FrameSchedule(double rate_hz, double start_time_seconds) {
    if (rate_hz <= 0.0) {
        throw std::invalid_argument("FrameSchedule rate_hz must be > 0");
    }

    period_seconds_ = 1.0 / rate_hz;
    reset(start_time_seconds);
}

double FrameSchedule::period_seconds() const noexcept {
    return period_seconds_;
}

double FrameSchedule::deadline_seconds() const noexcept {
    return deadline_seconds_;
}

void FrameSchedule::reset(double start_time_seconds) noexcept {
    deadline_seconds_ = start_time_seconds + period_seconds_;
}

void FrameSchedule::advance_after_frame(double now_seconds) noexcept {
    do {
        deadline_seconds_ += period_seconds_;
    } while (deadline_seconds_ <= now_seconds);
}

} // namespace b3r::core
