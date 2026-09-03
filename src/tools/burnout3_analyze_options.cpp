#include "tools/burnout3_analyze_options.h"

#include <charconv>
#include <utility>

namespace b3r::tools {
namespace {

Burnout3AnalyzeOptionsResult fail(Burnout3AnalyzeOptionError error, std::string message) {
    Burnout3AnalyzeOptionsResult result{};
    result.error = error;
    result.message = std::move(message);
    return result;
}

bool is_option(std::string_view value) noexcept {
    return value.starts_with("--");
}

} // namespace

Burnout3AnalyzeOptionsResult
parse_burnout3_analyze_options(std::span<const std::string_view> args) {
    Burnout3AnalyzeOptions options{};
    bool saw_elf = false;
    bool saw_output = false;
    bool saw_max_blocks = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto arg = args[i];
        if (arg == "--help") {
            options.show_help = true;
            continue;
        }

        auto require_value = [&](const char* option_name) -> std::optional<std::string_view> {
            if (i + 1u >= args.size() || is_option(args[i + 1u])) {
                return std::nullopt;
            }
            ++i;
            return args[i];
        };

        if (arg == "--elf") {
            if (saw_elf) {
                return fail(Burnout3AnalyzeOptionError::DuplicateOption,
                            "--elf may only be specified once");
            }
            const auto value = require_value("--elf");
            if (!value.has_value()) {
                return fail(Burnout3AnalyzeOptionError::MissingValue,
                            "--elf requires a file path");
            }
            options.elf_path.assign(value->begin(), value->end());
            saw_elf = true;
            continue;
        }

        if (arg == "--output") {
            if (saw_output) {
                return fail(Burnout3AnalyzeOptionError::DuplicateOption,
                            "--output may only be specified once");
            }
            const auto value = require_value("--output");
            if (!value.has_value()) {
                return fail(Burnout3AnalyzeOptionError::MissingValue,
                            "--output requires a file path");
            }
            options.output_path = std::string(value->begin(), value->end());
            saw_output = true;
            continue;
        }

        if (arg == "--max-blocks") {
            if (saw_max_blocks) {
                return fail(Burnout3AnalyzeOptionError::DuplicateOption,
                            "--max-blocks may only be specified once");
            }
            const auto value = require_value("--max-blocks");
            if (!value.has_value()) {
                return fail(Burnout3AnalyzeOptionError::MissingValue,
                            "--max-blocks requires a positive integer");
            }

            std::size_t parsed = 0;
            const auto begin = value->data();
            const auto end = begin + value->size();
            const auto conversion = std::from_chars(begin, end, parsed, 10);
            if (conversion.ec != std::errc{} || conversion.ptr != end || parsed == 0u) {
                return fail(Burnout3AnalyzeOptionError::InvalidMaxBlocks,
                            "--max-blocks must be a positive integer");
            }
            options.max_blocks = parsed;
            saw_max_blocks = true;
            continue;
        }

        return fail(Burnout3AnalyzeOptionError::UnknownOption,
                    "unknown Burnout3Analyze option: " + std::string(arg));
    }

    if (!options.show_help && !saw_elf) {
        return fail(Burnout3AnalyzeOptionError::MissingElfPath,
                    "--elf <path> is required");
    }

    Burnout3AnalyzeOptionsResult result{};
    result.options = std::move(options);
    return result;
}

const char* burnout3_analyze_usage() noexcept {
    return
        "Usage: Burnout3Analyze --elf <path> [--output <path>] [--max-blocks <count>]\n"
        "       Burnout3Analyze --help\n\n"
        "Analyzes an externally supplied PS2 ELF without executing guest code.\n"
        "If --output is omitted, the deterministic analysis report is written to stdout.\n"
        "Default --max-blocks: 4096.\n";
}

} // namespace b3r::tools
