#pragma once

#include "tools/burnout3_pacing_probe_options.h"

#include <ostream>
#include <string>

namespace b3r::tools {

enum class Burnout3PacingProbeRunError {
    None = 0,
    InvalidDuration,
    CaptureFailed,
    OutputOpenFailed,
    OutputWriteFailed,
};

struct Burnout3PacingProbeRunResult {
    Burnout3PacingProbeRunError error{Burnout3PacingProbeRunError::None};
    std::string message{};

    [[nodiscard]] bool ok() const noexcept {
        return error == Burnout3PacingProbeRunError::None;
    }
};

[[nodiscard]] Burnout3PacingProbeRunResult
run_burnout3_pacing_probe(const Burnout3PacingProbeOptions& options,
                          std::ostream& standard_output);

} // namespace b3r::tools
