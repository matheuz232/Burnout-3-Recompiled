#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace b3r::core {

struct FrameStatsSnapshot {
    double fps{};
    double frame_time_ms{};
    double simulation_time_ms{};
    double render_time_ms{};
    std::uint32_t simulation_steps{};
    unsigned sample_count{};
};

class FrameStats {
public:
    static constexpr std::size_t kWindowSize = 120;

    void add_frame(double frame_seconds,
                   double simulation_seconds,
                   double render_seconds,
                   std::uint32_t simulation_steps) noexcept;

    [[nodiscard]] FrameStatsSnapshot snapshot() const noexcept;
    void reset() noexcept;

private:
    struct Sample {
        double frame_seconds{};
        double simulation_seconds{};
        double render_seconds{};
        std::uint32_t simulation_steps{};
    };

    std::array<Sample, kWindowSize> samples_{};
    std::size_t next_index_{};
    std::size_t count_{};
};

} // namespace b3r::core
