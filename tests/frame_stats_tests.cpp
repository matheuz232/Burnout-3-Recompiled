#include "core/frame_stats.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void expect_near(double actual, double expected, double epsilon, const char* label) {
    if (std::abs(actual - expected) > epsilon) {
        std::cerr << label << ": expected " << expected << ", got " << actual << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void expect_equal(unsigned actual, unsigned expected, const char* label) {
    if (actual != expected) {
        std::cerr << label << ": expected " << expected << ", got " << actual << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using b3r::core::FrameStats;

    FrameStats stats;
    stats.add_frame(1.0 / 120.0, 0.002, 0.001, 1);
    stats.add_frame(1.0 / 120.0, 0.004, 0.003, 2);

    const auto snapshot = stats.snapshot();
    expect_near(snapshot.fps, 120.0, 1e-9, "fps");
    expect_near(snapshot.frame_time_ms, 1000.0 / 120.0, 1e-9, "frame time");
    expect_near(snapshot.simulation_time_ms, 3.0, 1e-9, "simulation average");
    expect_near(snapshot.render_time_ms, 2.0, 1e-9, "render average");
    expect_equal(snapshot.simulation_steps, 2, "latest simulation steps");
    expect_equal(snapshot.sample_count, 2, "sample count");

    std::cout << "frame_stats_tests: PASS\n";
    return EXIT_SUCCESS;
}
