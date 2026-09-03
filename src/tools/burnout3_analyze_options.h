#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace b3r::tools {

enum class Burnout3AnalyzeOptionError {
    None = 0,
    MissingElfPath,
    MissingValue,
    DuplicateOption,
    UnknownOption,
    InvalidMaxBlocks,
};

struct Burnout3AnalyzeOptions {
    std::string elf_path{};
    std::optional<std::string> output_path{};
    std::size_t max_blocks{4096};
    bool follow_direct_calls{false};
    bool show_help{false};
};

struct Burnout3AnalyzeOptionsResult {
    Burnout3AnalyzeOptionError error{Burnout3AnalyzeOptionError::None};
    std::string message{};
    std::optional<Burnout3AnalyzeOptions> options{};

    [[nodiscard]] bool ok() const noexcept {
        return error == Burnout3AnalyzeOptionError::None && options.has_value();
    }
};

[[nodiscard]] Burnout3AnalyzeOptionsResult
parse_burnout3_analyze_options(std::span<const std::string_view> args);

[[nodiscard]] const char* burnout3_analyze_usage() noexcept;

} // namespace b3r::tools
