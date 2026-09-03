#include "core/frame_pacing_telemetry.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <vector>

namespace b3r::core {
namespace {

constexpr double kMillisecondsPerSecond = 1000.0;
constexpr double kTarget120HzMs = kMillisecondsPerSecond / 120.0;

[[nodiscard]] double nearest_rank_percentile(const std::vector<double>& sorted_ms,
                                             double percentile) {
    if (sorted_ms.empty()) {
        return 0.0;
    }

    const double rank = std::ceil(percentile * static_cast<double>(sorted_ms.size()));
    const std::size_t one_based_rank = static_cast<std::size_t>(std::max(1.0, rank));
    const std::size_t index = std::min(one_based_rank - 1, sorted_ms.size() - 1);
    return sorted_ms[index];
}

} // namespace

FramePacingTelemetrySummary summarize_frame_pacing(std::span<const double> frame_seconds) {
    FramePacingTelemetrySummary summary{};
    if (frame_seconds.empty()) {
        return summary;
    }

    std::vector<double> samples_ms;
    samples_ms.reserve(frame_seconds.size());
    for (const double seconds : frame_seconds) {
        samples_ms.push_back(seconds * kMillisecondsPerSecond);
    }

    summary.sample_count = samples_ms.size();
    summary.min_ms = *std::min_element(samples_ms.begin(), samples_ms.end());
    summary.max_ms = *std::max_element(samples_ms.begin(), samples_ms.end());

    const double total_ms = std::accumulate(samples_ms.begin(), samples_ms.end(), 0.0);
    summary.mean_ms = total_ms / static_cast<double>(samples_ms.size());

    double squared_deviation_total = 0.0;
    for (const double sample_ms : samples_ms) {
        const double deviation = sample_ms - summary.mean_ms;
        squared_deviation_total += deviation * deviation;

        if (sample_ms > kTarget120HzMs) {
            ++summary.over_8_333ms;
        }
        if (sample_ms > 9.0) {
            ++summary.over_9ms;
        }
        if (sample_ms > 10.0) {
            ++summary.over_10ms;
        }
        if (sample_ms > 12.0) {
            ++summary.over_12ms;
        }
    }

    summary.stddev_ms = std::sqrt(
        squared_deviation_total / static_cast<double>(samples_ms.size()));

    std::sort(samples_ms.begin(), samples_ms.end());
    summary.p50_ms = nearest_rank_percentile(samples_ms, 0.50);
    summary.p95_ms = nearest_rank_percentile(samples_ms, 0.95);
    summary.p99_ms = nearest_rank_percentile(samples_ms, 0.99);

    return summary;
}

std::string render_frame_pacing_report(const FramePacingTelemetrySummary& summary) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "B3R_FRAME_PACING_TELEMETRY 1\n";
    out << "SAMPLES " << summary.sample_count << '\n';
    out << "MEAN_MS " << summary.mean_ms << '\n';
    out << "MIN_MS " << summary.min_ms << '\n';
    out << "MAX_MS " << summary.max_ms << '\n';
    out << "STDDEV_MS " << summary.stddev_ms << '\n';
    out << "P50_MS " << summary.p50_ms << '\n';
    out << "P95_MS " << summary.p95_ms << '\n';
    out << "P99_MS " << summary.p99_ms << '\n';
    out << "OVER_8_333MS " << summary.over_8_333ms << '\n';
    out << "OVER_9MS " << summary.over_9ms << '\n';
    out << "OVER_10MS " << summary.over_10ms << '\n';
    out << "OVER_12MS " << summary.over_12ms << '\n';
    return out.str();
}

} // namespace b3r::core
