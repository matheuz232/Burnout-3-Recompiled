#include "core/frame_pacing_telemetry.h"
#include "platform/windows/qpc_clock.h"
#include "platform/windows/windows_frame_pacer.h"

#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
    using namespace b3r::platform::windows;

    WindowsFramePacer pacer{120.0};
    constexpr int kFrames = 240;

    std::vector<double> frame_durations;
    frame_durations.reserve(kFrames);

    double previous = QpcClock::now_seconds();
    for (int i = 0; i < kFrames; ++i) {
        const double now = pacer.wait_for_next_frame();
        frame_durations.push_back(now - previous);
        previous = now;
    }

    const auto telemetry = b3r::core::summarize_frame_pacing(frame_durations);
    const std::string report = b3r::core::render_frame_pacing_report(telemetry);

    if (telemetry.sample_count != static_cast<std::size_t>(kFrames)) {
        std::cerr << "unexpected telemetry sample count: " << telemetry.sample_count << '\n';
        return EXIT_FAILURE;
    }

    // Hosted CI is not a real-time environment. Keep this envelope deliberately broad:
    // the test catches gross pacing failures while the report preserves the actual jitter data.
    if (telemetry.mean_ms < 6.0 || telemetry.mean_ms > 12.5) {
        std::cerr << "unexpected pacing mean: " << telemetry.mean_ms << " ms\n";
        std::cerr << report;
        return EXIT_FAILURE;
    }

    if (telemetry.p50_ms < 4.0 || telemetry.p50_ms > 15.0) {
        std::cerr << "unexpected pacing P50: " << telemetry.p50_ms << " ms\n";
        std::cerr << report;
        return EXIT_FAILURE;
    }

    if (telemetry.max_ms > 100.0) {
        std::cerr << "gross pacing stall detected: " << telemetry.max_ms << " ms\n";
        std::cerr << report;
        return EXIT_FAILURE;
    }

    std::cout << report;
    std::cout << "HIGH_RESOLUTION_TIMER "
              << (pacer.using_high_resolution_timer() ? "YES" : "NO") << '\n';
    std::cout << "frame_pacer_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
