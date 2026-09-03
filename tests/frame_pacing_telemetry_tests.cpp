#include "core/frame_pacing_telemetry.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "frame_pacing_telemetry_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

void expect_near(double actual, double expected, double tolerance, const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        fail(message + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
    }
}

void test_summary_metrics_and_thresholds() {
    const std::vector<double> samples{
        0.008,
        0.009,
        0.010,
        0.012,
        0.013,
    };

    const auto summary = b3r::core::summarize_frame_pacing(samples);

    expect(summary.sample_count == 5, "sample count mismatch");
    expect_near(summary.mean_ms, 10.4, 1e-9, "mean mismatch");
    expect_near(summary.min_ms, 8.0, 1e-9, "minimum mismatch");
    expect_near(summary.max_ms, 13.0, 1e-9, "maximum mismatch");
    expect_near(summary.stddev_ms, std::sqrt(3.44), 1e-9, "population standard deviation mismatch");
    expect_near(summary.p50_ms, 10.0, 1e-9, "P50 mismatch");
    expect_near(summary.p95_ms, 13.0, 1e-9, "P95 mismatch");
    expect_near(summary.p99_ms, 13.0, 1e-9, "P99 mismatch");
    expect(summary.over_8_333ms == 4, ">8.333ms count mismatch");
    expect(summary.over_9ms == 3, ">9ms count mismatch");
    expect(summary.over_10ms == 2, ">10ms count mismatch");
    expect(summary.over_12ms == 1, ">12ms count mismatch");
}

void test_empty_summary_is_zeroed() {
    const std::vector<double> samples;
    const auto summary = b3r::core::summarize_frame_pacing(samples);

    expect(summary.sample_count == 0, "empty sample count mismatch");
    expect(summary.mean_ms == 0.0, "empty mean should be zero");
    expect(summary.p99_ms == 0.0, "empty P99 should be zero");
    expect(summary.over_8_333ms == 0, "empty threshold count should be zero");
}

void test_report_is_deterministic() {
    const std::vector<double> samples{
        0.008,
        0.009,
        0.010,
        0.012,
        0.013,
    };

    const auto summary = b3r::core::summarize_frame_pacing(samples);
    const std::string report = b3r::core::render_frame_pacing_report(summary);

    const std::string expected =
        "B3R_FRAME_PACING_TELEMETRY 1\n"
        "SAMPLES 5\n"
        "MEAN_MS 10.400\n"
        "MIN_MS 8.000\n"
        "MAX_MS 13.000\n"
        "STDDEV_MS 1.855\n"
        "P50_MS 10.000\n"
        "P95_MS 13.000\n"
        "P99_MS 13.000\n"
        "OVER_8_333MS 4\n"
        "OVER_9MS 3\n"
        "OVER_10MS 2\n"
        "OVER_12MS 1\n";

    expect(report == expected, "deterministic report mismatch\n" + report);
}

} // namespace

int main() {
    test_summary_metrics_and_thresholds();
    test_empty_summary_is_zeroed();
    test_report_is_deterministic();
    std::cout << "frame_pacing_telemetry_tests: PASS\n";
    return EXIT_SUCCESS;
}
