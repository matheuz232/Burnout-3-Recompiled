#include "tools/burnout3_analyze_options.h"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "burnout3_analyze_options_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

b3r::tools::Burnout3AnalyzeOptionsResult parse(std::initializer_list<std::string_view> args) {
    const std::vector<std::string_view> values(args);
    return b3r::tools::parse_burnout3_analyze_options(values);
}

} // namespace

int main() {
    using namespace b3r::tools;

    {
        const auto result = parse({"--elf", "SLUS_210.50", "--output", "analysis.txt", "--max-blocks", "8192"});
        expect(result.ok(), "complete CLI options must parse");
        expect(result.options->elf_path == "SLUS_210.50", "ELF path must be retained");
        expect(result.options->output_path.has_value() && *result.options->output_path == "analysis.txt",
               "output path must be retained");
        expect(result.options->max_blocks == 8192u, "max block limit must parse as a positive integer");
        expect(!result.options->show_help, "normal invocation must not request help");
    }

    {
        const auto result = parse({"--elf", "game.elf"});
        expect(result.ok(), "ELF-only invocation must use defaults");
        expect(result.options->max_blocks == 4096u, "default max block limit must remain bounded");
        expect(!result.options->output_path.has_value(), "output must default to stdout");
    }

    {
        const auto result = parse({"--help"});
        expect(result.ok(), "help must not require an ELF path");
        expect(result.options->show_help, "help flag must be preserved");
    }

    {
        const auto result = parse({"--output", "analysis.txt"});
        expect(!result.ok(), "normal invocation without --elf must fail");
        expect(result.error == Burnout3AnalyzeOptionError::MissingElfPath,
               "missing ELF path must have a specific error");
    }

    {
        const auto result = parse({"--elf"});
        expect(!result.ok(), "option without a value must fail");
        expect(result.error == Burnout3AnalyzeOptionError::MissingValue,
               "missing option values must have a specific error");
    }

    {
        const auto result = parse({"--elf", "game.elf", "--max-blocks", "0"});
        expect(!result.ok(), "zero block limit must fail");
        expect(result.error == Burnout3AnalyzeOptionError::InvalidMaxBlocks,
               "invalid block limit must have a specific error");
    }

    {
        const auto result = parse({"--elf", "game.elf", "--wat"});
        expect(!result.ok(), "unknown options must fail");
        expect(result.error == Burnout3AnalyzeOptionError::UnknownOption,
               "unknown option must have a specific error");
    }

    std::cout << "burnout3_analyze_options_tests: PASS\n";
    return EXIT_SUCCESS;
}
