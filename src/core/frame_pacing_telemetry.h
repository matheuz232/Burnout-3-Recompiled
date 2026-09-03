#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace b3r::core {

struct FramePacingTelemetrySummary {
    std::size_t sample_count{};
    double mean_ms{};
    double min_ms{};
    double max_ms{};
    double stddev_ms{};
    double p50_ms{};
    double p95_ms{};
    double p99_ms{};
    std::size_t over_8_333ms{};
    std::size_t over_9ms{};
    std::size_t over_10ms{};
    std::size_t over_12ms{};
};

[[nodiscard]] FramePacingTelemetrySummary summarize_frame_pacing(
    std::span<const double> frame_seconds);

[[nodiscard]] std::string render_frame_pacing_report(
    const FramePacingTelemetrySummary& summary);

} // namespace b3r::core
