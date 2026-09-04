#include "tools/burnout3_pacing_probe_options.h"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "burnout3_pacing_probe_options_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

b3r::tools::Burnout3PacingProbeOptionsResult parse(
    std::initializer_list<std::string_view> args) {
    const std::vector<std::string_view> values(args);
    return b3r::tools::parse_burnout3_pacing_probe_options(values);
}

} // namespace

int main() {
    using namespace b3r::tools;

    {
        const auto result = parse({});
        expect(result.ok(), "empty invocation must use the 60-second default");
        expect(result.options->seconds == 60u, "default capture duration must be 60 seconds");
        expect(!result.options->output_path.has_value(), "output must default to stdout");
        expect(!result.options->show_help, "default invocation must not request help");
    }

    {
        const auto result = parse({"--seconds", "1", "--output", "pacing.txt"});
        expect(result.ok(), "explicit duration and output must parse");
        expect(result.options->seconds == 1u, "explicit seconds must be retained");
        expect(result.options->output_path.has_value() && *result.options->output_path == "pacing.txt",
               "output path must be retained");
    }

    {
        const auto result = parse({"--help"});
        expect(result.ok(), "help must parse without other options");
        expect(result.options->show_help, "help flag must be retained");
    }

    {
        const auto result = parse({"--seconds", "0"});
        expect(!result.ok(), "zero-second captures must fail");
        expect(result.error == Burnout3PacingProbeOptionError::InvalidSeconds,
               "zero seconds must report InvalidSeconds");
    }

    {
        const auto result = parse({"--seconds"});
        expect(!result.ok(), "missing seconds value must fail");
        expect(result.error == Burnout3PacingProbeOptionError::MissingValue,
               "missing option values must report MissingValue");
    }

    {
        const auto result = parse({"--seconds", "1", "--seconds", "2"});
        expect(!result.ok(), "duplicate seconds option must fail");
        expect(result.error == Burnout3PacingProbeOptionError::DuplicateOption,
               "duplicates must report DuplicateOption");
    }

    {
        const auto result = parse({"--wat"});
        expect(!result.ok(), "unknown options must fail");
        expect(result.error == Burnout3PacingProbeOptionError::UnknownOption,
               "unknown option must report UnknownOption");
    }

    std::cout << "burnout3_pacing_probe_options_tests: PASS\n";
    return EXIT_SUCCESS;
}
