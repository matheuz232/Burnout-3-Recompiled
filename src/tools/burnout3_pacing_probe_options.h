#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace b3r::tools {

enum class Burnout3PacingProbeOptionError {
    None = 0,
    MissingValue,
    DuplicateOption,
    UnknownOption,
    InvalidSeconds,
};

struct Burnout3PacingProbeOptions {
    std::size_t seconds{60};
    std::optional<std::string> output_path{};
    bool show_help{false};
};

struct Burnout3PacingProbeOptionsResult {
    Burnout3PacingProbeOptionError error{Burnout3PacingProbeOptionError::None};
    std::string message{};
    std::optional<Burnout3PacingProbeOptions> options{};

    [[nodiscard]] bool ok() const noexcept {
        return error == Burnout3PacingProbeOptionError::None && options.has_value();
    }
};

[[nodiscard]] Burnout3PacingProbeOptionsResult
parse_burnout3_pacing_probe_options(std::span<const std::string_view> args);

[[nodiscard]] const char* burnout3_pacing_probe_usage() noexcept;

} // namespace b3r::tools
