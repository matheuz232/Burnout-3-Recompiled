#pragma once

#include "tools/burnout3_pacing_probe_options.h"

#include <ostream>
#include <string>

namespace b3r::platform::windows {

struct Burnout3PacingProbeRunResult {
    bool success{false};
    std::string message{};

    [[nodiscard]] bool ok() const noexcept {
        return success;
    }
};

[[nodiscard]] Burnout3PacingProbeRunResult run_burnout3_pacing_probe(
    const b3r::tools::Burnout3PacingProbeOptions& options,
    std::ostream& output);

} // namespace b3r::platform::windows
