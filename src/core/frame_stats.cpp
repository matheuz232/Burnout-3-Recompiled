#include "core/frame_stats.h"

namespace b3r::core {

void FrameStats::add_frame(double frame_seconds,
                           double simulation_seconds,
                           double render_seconds,
                           std::uint32_t simulation_steps) noexcept {
    samples_[next_index_] = Sample{
        frame_seconds,
        simulation_seconds,
        render_seconds,
        simulation_steps,
    };

    next_index_ = (next_index_ + 1) % kWindowSize;
    if (count_ < kWindowSize) {
        ++count_;
    }
}

FrameStatsSnapshot FrameStats::snapshot() const noexcept {
    FrameStatsSnapshot result{};
    if (count_ == 0) {
        return result;
    }

    double frame_total = 0.0;
    double simulation_total = 0.0;
    double render_total = 0.0;

    for (std::size_t i = 0; i < count_; ++i) {
        frame_total += samples_[i].frame_seconds;
        simulation_total += samples_[i].simulation_seconds;
        render_total += samples_[i].render_seconds;
    }

    const double count = static_cast<double>(count_);
    const double average_frame_seconds = frame_total / count;

    result.fps = average_frame_seconds > 0.0 ? 1.0 / average_frame_seconds : 0.0;
    result.frame_time_ms = average_frame_seconds * 1000.0;
    result.simulation_time_ms = (simulation_total / count) * 1000.0;
    result.render_time_ms = (render_total / count) * 1000.0;

    const std::size_t latest_index = (next_index_ + kWindowSize - 1) % kWindowSize;
    result.simulation_steps = samples_[latest_index].simulation_steps;
    result.sample_count = static_cast<unsigned>(count_);
    return result;
}

void FrameStats::reset() noexcept {
    samples_ = {};
    next_index_ = 0;
    count_ = 0;
}

} // namespace b3r::core
