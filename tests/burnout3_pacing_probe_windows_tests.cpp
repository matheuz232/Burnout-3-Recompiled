#include "tools/burnout3_pacing_probe_app.h"
#include "tools/burnout3_pacing_probe_options.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "burnout3_pacing_probe_windows_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

} // namespace

int main() {
    using namespace b3r::tools;

    Burnout3PacingProbeOptions options{};
    options.seconds = 1u;

    std::ostringstream output;
    const auto result = run_burnout3_pacing_probe(options, output);
    expect(result.ok(), "one-second pacing probe must run successfully");

    const std::string report = output.str();
    expect(report.find("B3R_PACING_PROBE 1\n") != std::string::npos,
           "report must include the probe format marker");
    expect(report.find("TARGET_HZ 120\n") != std::string::npos,
           "report must preserve the fixed 120 Hz target");
    expect(report.find("REQUESTED_SECONDS 1\n") != std::string::npos,
           "report must include requested duration");
    expect(report.find("FRAMES 120\n") != std::string::npos,
           "one second must capture exactly 120 frame intervals");
    expect(report.find("HIGH_RESOLUTION_TIMER ") != std::string::npos,
           "report must expose high-resolution timer availability");
    expect(report.find("B3R_FRAME_PACING_TELEMETRY 1\n") != std::string::npos,
           "report must embed the existing telemetry format");
    expect(report.find("SAMPLES 120\n") != std::string::npos,
           "telemetry sample count must match the fixed 120 Hz frame count");
    expect(report.find("PERFORMANCE_PASS") == std::string::npos &&
               report.find("PERFORMANCE_FAIL") == std::string::npos,
           "probe must not classify desktop performance as pass/fail");

    Burnout3PacingProbeOptions overflow{};
    overflow.seconds = std::numeric_limits<std::size_t>::max();
    std::ostringstream ignored;
    const auto overflow_result = run_burnout3_pacing_probe(overflow, ignored);
    expect(!overflow_result.ok(), "unrepresentable frame count must fail before capture");
    expect(overflow_result.error == Burnout3PacingProbeRunError::InvalidDuration,
           "duration overflow must have a specific error");

    std::cout << "burnout3_pacing_probe_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
