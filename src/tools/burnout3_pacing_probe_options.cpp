#include "tools/burnout3_pacing_probe_options.h"

#include <charconv>
#include <utility>

namespace b3r::tools {
namespace {

Burnout3PacingProbeOptionsResult fail(Burnout3PacingProbeOptionError error,
                                      std::string message) {
    Burnout3PacingProbeOptionsResult result{};
    result.error = error;
    result.message = std::move(message);
    return result;
}

bool is_option(std::string_view value) noexcept {
    return value.starts_with("--");
}

} // namespace

Burnout3PacingProbeOptionsResult
parse_burnout3_pacing_probe_options(std::span<const std::string_view> args) {
    Burnout3PacingProbeOptions options{};
    bool saw_seconds = false;
    bool saw_output = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto arg = args[i];
        if (arg == "--help") {
            options.show_help = true;
            continue;
        }

        auto require_value = [&]() -> std::optional<std::string_view> {
            if (i + 1u >= args.size() || is_option(args[i + 1u])) {
                return std::nullopt;
            }
            ++i;
            return args[i];
        };

        if (arg == "--seconds") {
            if (saw_seconds) {
                return fail(Burnout3PacingProbeOptionError::DuplicateOption,
                            "--seconds may only be specified once");
            }
            const auto value = require_value();
            if (!value.has_value()) {
                return fail(Burnout3PacingProbeOptionError::MissingValue,
                            "--seconds requires a positive integer");
            }

            std::size_t parsed = 0;
            const auto begin = value->data();
            const auto end = begin + value->size();
            const auto conversion = std::from_chars(begin, end, parsed, 10);
            if (conversion.ec != std::errc{} || conversion.ptr != end || parsed == 0u) {
                return fail(Burnout3PacingProbeOptionError::InvalidSeconds,
                            "--seconds must be a positive integer");
            }
            options.seconds = parsed;
            saw_seconds = true;
            continue;
        }

        if (arg == "--output") {
            if (saw_output) {
                return fail(Burnout3PacingProbeOptionError::DuplicateOption,
                            "--output may only be specified once");
            }
            const auto value = require_value();
            if (!value.has_value()) {
                return fail(Burnout3PacingProbeOptionError::MissingValue,
                            "--output requires a file path");
            }
            options.output_path = std::string(value->begin(), value->end());
            saw_output = true;
            continue;
        }

        return fail(Burnout3PacingProbeOptionError::UnknownOption,
                    "unknown Burnout3PacingProbe option: " + std::string(arg));
    }

    Burnout3PacingProbeOptionsResult result{};
    result.options = std::move(options);
    return result;
}

const char* burnout3_pacing_probe_usage() noexcept {
    return
        "Usage: Burnout3PacingProbe [--seconds <count>] [--output <path>]\n"
        "       Burnout3PacingProbe --help\n\n"
        "Captures Windows frame-pacing telemetry at a fixed 120 Hz target.\n"
        "Default --seconds: 60. Without --output, the report is written to stdout.\n"
        "The probe reports measurements only; it does not classify desktop performance as pass/fail.\n";
}

} // namespace b3r::tools
