#include "platform/windows/burnout3_pacing_probe_app.h"

#include "core/frame_pacing_telemetry.h"
#include "platform/windows/qpc_clock.h"
#include "platform/windows/windows_frame_pacer.h"

#include <limits>
#include <vector>

namespace b3r::platform::windows {

Burnout3PacingProbeRunResult run_burnout3_pacing_probe(
    const b3r::tools::Burnout3PacingProbeOptions& options,
    std::ostream& output) {
    constexpr std::size_t kTargetHz = 120u;
    if (options.seconds > (std::numeric_limits<std::size_t>::max() / kTargetHz)) {
        return Burnout3PacingProbeRunResult{false, "requested duration is too large"};
    }

    const std::size_t frame_count = options.seconds * kTargetHz;
    WindowsFramePacer pacer{120.0};

    std::vector<double> frame_durations;
    frame_durations.reserve(frame_count);

    double previous = QpcClock::now_seconds();
    for (std::size_t i = 0; i < frame_count; ++i) {
        const double now = pacer.wait_for_next_frame();
        frame_durations.push_back(now - previous);
        previous = now;
    }

    const auto telemetry = b3r::core::summarize_frame_pacing(frame_durations);

    output << "B3R_PACING_PROBE 1\n"
           << "TARGET_HZ 120\n"
           << "REQUESTED_SECONDS " << options.seconds << '\n'
           << "FRAMES " << frame_count << '\n'
           << b3r::core::render_frame_pacing_report(telemetry)
           << "HIGH_RESOLUTION_TIMER "
           << (pacer.using_high_resolution_timer() ? "YES" : "NO") << '\n';

    return Burnout3PacingProbeRunResult{true, {}};
}

} // namespace b3r::platform::windows
