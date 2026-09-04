#include "tools/burnout3_pacing_probe_app.h"

#include "core/frame_pacing_telemetry.h"
#include "platform/windows/qpc_clock.h"
#include "platform/windows/windows_frame_pacer.h"

#include <cstddef>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace b3r::tools {
namespace {

constexpr std::size_t kTargetHz = 120u;

Burnout3PacingProbeRunResult fail(Burnout3PacingProbeRunError error,
                                  std::string message) {
    Burnout3PacingProbeRunResult result{};
    result.error = error;
    result.message = std::move(message);
    return result;
}

} // namespace

Burnout3PacingProbeRunResult
run_burnout3_pacing_probe(const Burnout3PacingProbeOptions& options,
                          std::ostream& standard_output) {
    if (options.seconds > std::numeric_limits<std::size_t>::max() / kTargetHz) {
        return fail(Burnout3PacingProbeRunError::InvalidDuration,
                    "requested duration is too large to represent at 120 Hz");
    }

    const std::size_t frame_count = options.seconds * kTargetHz;
    std::vector<double> frame_durations;
    frame_durations.reserve(frame_count);

    bool high_resolution_timer = false;
    try {
        b3r::platform::windows::WindowsFramePacer pacer{static_cast<double>(kTargetHz)};
        high_resolution_timer = pacer.using_high_resolution_timer();

        double previous = b3r::platform::windows::QpcClock::now_seconds();
        for (std::size_t frame = 0; frame < frame_count; ++frame) {
            const double now = pacer.wait_for_next_frame();
            frame_durations.push_back(now - previous);
            previous = now;
        }
    } catch (const std::exception& error) {
        return fail(Burnout3PacingProbeRunError::CaptureFailed,
                    std::string("frame-pacing capture failed: ") + error.what());
    }

    const auto telemetry = b3r::core::summarize_frame_pacing(frame_durations);

    std::ostringstream report;
    report << "B3R_PACING_PROBE 1\n";
    report << "TARGET_HZ " << kTargetHz << '\n';
    report << "REQUESTED_SECONDS " << options.seconds << '\n';
    report << "FRAMES " << frame_count << '\n';
    report << "HIGH_RESOLUTION_TIMER "
           << (high_resolution_timer ? "YES" : "NO") << '\n';
    report << b3r::core::render_frame_pacing_report(telemetry);

    const std::string report_text = report.str();
    if (options.output_path.has_value()) {
        std::ofstream output(*options.output_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return fail(Burnout3PacingProbeRunError::OutputOpenFailed,
                        "could not open pacing report output: " + *options.output_path);
        }
        output.write(report_text.data(), static_cast<std::streamsize>(report_text.size()));
        if (!output) {
            return fail(Burnout3PacingProbeRunError::OutputWriteFailed,
                        "could not write complete pacing report: " + *options.output_path);
        }
        return {};
    }

    standard_output.write(report_text.data(),
                          static_cast<std::streamsize>(report_text.size()));
    if (!standard_output) {
        return fail(Burnout3PacingProbeRunError::OutputWriteFailed,
                    "could not write pacing report to stdout");
    }

    return {};
}

} // namespace b3r::tools
