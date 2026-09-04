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
        const auto result = parse({"--seconds", "90", "--output", "pacing.txt"});
        expect(result.ok(), "complete pacing-probe options must parse");
        expect(result.options->seconds == 90u, "seconds must parse as a positive integer");
        expect(result.options->output_path.has_value() && *result.options->output_path == "pacing.txt",
               "output path must be retained");
        expect(!result.options->show_help, "normal invocation must not request help");
    }

    {
        const auto result = parse({});
        expect(result.ok(), "empty invocation must use the 60-second default");
        expect(result.options->seconds == 60u, "default capture duration must be 60 seconds");
        expect(!result.options->output_path.has_value(), "output must default to stdout");
    }

    {
        const auto result = parse({"--help"});
        expect(result.ok(), "help must parse without other options");
        expect(result.options->show_help, "help flag must be preserved");
    }

    {
        const auto result = parse({"--seconds"});
        expect(!result.ok(), "seconds without a value must fail");
        expect(result.error == Burnout3PacingProbeOptionError::MissingValue,
               "missing seconds value must have a specific error");
    }

    {
        const auto result = parse({"--seconds", "0"});
        expect(!result.ok(), "zero seconds must fail");
        expect(result.error == Burnout3PacingProbeOptionError::InvalidSeconds,
               "zero seconds must have an invalid-seconds error");
    }

    {
        const auto result = parse({"--seconds", "abc"});
        expect(!result.ok(), "non-numeric seconds must fail");
        expect(result.error == Burnout3PacingProbeOptionError::InvalidSeconds,
               "non-numeric seconds must have an invalid-seconds error");
    }

    {
        const auto result = parse({"--seconds", "5", "--seconds", "6"});
        expect(!result.ok(), "duplicate seconds option must fail");
        expect(result.error == Burnout3PacingProbeOptionError::DuplicateOption,
               "duplicate seconds option must have a duplicate-option error");
    }

    {
        const auto result = parse({"--output", "a.txt", "--output", "b.txt"});
        expect(!result.ok(), "duplicate output option must fail");
        expect(result.error == Burnout3PacingProbeOptionError::DuplicateOption,
               "duplicate output option must have a duplicate-option error");
    }

    {
        const auto result = parse({"--wat"});
        expect(!result.ok(), "unknown option must fail");
        expect(result.error == Burnout3PacingProbeOptionError::UnknownOption,
               "unknown option must have a specific error");
    }

    const std::string_view usage = burnout3_pacing_probe_usage();
    expect(usage.find("Burnout3PacingProbe") != std::string_view::npos,
           "usage must name the pacing probe executable");
    expect(usage.find("--seconds") != std::string_view::npos,
           "usage must document the seconds option");
    expect(usage.find("120 Hz") != std::string_view::npos,
           "usage must state the fixed 120 Hz target");

    std::cout << "burnout3_pacing_probe_options_tests: PASS\n";
    return EXIT_SUCCESS;
}
