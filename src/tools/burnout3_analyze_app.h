#pragma once

#include "tools/burnout3_analyze_options.h"

#include <ostream>
#include <string>

namespace b3r::tools {

enum class Burnout3AnalyzeRunError {
    None = 0,
    InputOpenFailed,
    InputReadFailed,
    AnalysisFailed,
    OutputOpenFailed,
    OutputWriteFailed,
};

struct Burnout3AnalyzeRunResult {
    Burnout3AnalyzeRunError error{Burnout3AnalyzeRunError::None};
    std::string message{};

    [[nodiscard]] bool ok() const noexcept {
        return error == Burnout3AnalyzeRunError::None;
    }
};

[[nodiscard]] Burnout3AnalyzeRunResult
run_burnout3_analyze(const Burnout3AnalyzeOptions& options,
                     std::ostream& standard_output);

} // namespace b3r::tools
