#include "tools/burnout3_analyze_app.h"

#include "analysis/ps2_elf_analysis.h"
#include "analysis/ps2_pad_binding_report.h"

#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace b3r::tools {
namespace {

Burnout3AnalyzeRunResult fail(Burnout3AnalyzeRunError error, std::string message) {
    Burnout3AnalyzeRunResult result{};
    result.error = error;
    result.message = std::move(message);
    return result;
}

} // namespace

Burnout3AnalyzeRunResult
run_burnout3_analyze(const Burnout3AnalyzeOptions& options,
                     std::ostream& standard_output) {
    std::ifstream input(options.elf_path, std::ios::binary | std::ios::ate);
    if (!input) {
        return fail(Burnout3AnalyzeRunError::InputOpenFailed,
                    "could not open input ELF: " + options.elf_path);
    }

    const std::streampos end_position = input.tellg();
    if (end_position < std::streampos{0}) {
        return fail(Burnout3AnalyzeRunError::InputReadFailed,
                    "could not determine input ELF size: " + options.elf_path);
    }

    const auto file_size = static_cast<std::uintmax_t>(end_position);
    if (file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()) ||
        file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        return fail(Burnout3AnalyzeRunError::InputReadFailed,
                    "input ELF is too large to analyze in memory: " + options.elf_path);
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
    input.seekg(0, std::ios::beg);
    if (!input) {
        return fail(Burnout3AnalyzeRunError::InputReadFailed,
                    "could not seek to the beginning of input ELF: " + options.elf_path);
    }

    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            return fail(Burnout3AnalyzeRunError::InputReadFailed,
                        "could not read the complete input ELF: " + options.elf_path);
        }
    }

    analysis::R5900ReachabilityOptions analysis_options{};
    analysis_options.max_blocks = options.max_blocks;
    analysis_options.follow_direct_calls = options.follow_direct_calls;
    const auto analysis_result = analysis::analyze_ps2_elf(bytes, analysis_options);
    if (!analysis_result.ok()) {
        return fail(Burnout3AnalyzeRunError::AnalysisFailed,
                    analysis_result.message.empty()
                        ? "PS2 ELF analysis failed"
                        : analysis_result.message);
    }

    std::string report = *analysis_result.report;
    if (options.pad_bindings) {
        const auto parsed = recompiler::parse_ps2_elf(bytes);
        if (!parsed.ok()) {
            return fail(Burnout3AnalyzeRunError::AnalysisFailed,
                        "PS2 ELF parse failed while preparing PAD binding discovery");
        }

        auto mapped = runtime::Ps2MemoryMap::from_elf(*parsed.image);
        if (!mapped.ok()) {
            return fail(Burnout3AnalyzeRunError::AnalysisFailed,
                        "PS2 ELF memory mapping failed while preparing PAD binding discovery");
        }

        const auto graph = analysis::analyze_r5900_reachability(
            *mapped.memory, parsed.image->entry_point(), analysis_options);
        if (!graph.ok()) {
            return fail(Burnout3AnalyzeRunError::AnalysisFailed,
                        "R5900 reachability analysis failed while preparing PAD binding discovery");
        }

        const auto bindings = analysis::discover_ps2_pad_bindings(
            bytes, *parsed.image, *mapped.memory, *graph.graph);
        report.push_back('\n');
        report += analysis::render_ps2_pad_binding_report(bindings);
    }

    if (options.output_path.has_value()) {
        std::ofstream output(*options.output_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return fail(Burnout3AnalyzeRunError::OutputOpenFailed,
                        "could not open report output: " + *options.output_path);
        }
        output.write(report.data(), static_cast<std::streamsize>(report.size()));
        if (!output) {
            return fail(Burnout3AnalyzeRunError::OutputWriteFailed,
                        "could not write complete report output: " + *options.output_path);
        }
        return {};
    }

    standard_output.write(report.data(), static_cast<std::streamsize>(report.size()));
    if (!standard_output) {
        return fail(Burnout3AnalyzeRunError::OutputWriteFailed,
                    "could not write analysis report to stdout");
    }

    return {};
}

} // namespace b3r::tools
