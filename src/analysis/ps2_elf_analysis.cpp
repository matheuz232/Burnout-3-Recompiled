#include "analysis/ps2_elf_analysis.h"

#include "analysis/r5900_analysis_report.h"

#include <utility>

namespace b3r::analysis {
namespace {

Ps2ElfAnalysisResult fail_elf(const recompiler::Ps2ElfParseResult& parsed) {
    Ps2ElfAnalysisResult result{};
    result.error = Ps2ElfAnalysisError::ElfParseFailed;
    result.message = parsed.message;
    result.elf_error = parsed.error;
    return result;
}

Ps2ElfAnalysisResult fail_memory(const runtime::Ps2MemoryMapBuildResult& mapped) {
    Ps2ElfAnalysisResult result{};
    result.error = Ps2ElfAnalysisError::MemoryMapFailed;
    result.message = mapped.message;
    result.memory_error = mapped.error;
    return result;
}

Ps2ElfAnalysisResult fail_reachability(const R5900ReachabilityResult& analyzed) {
    Ps2ElfAnalysisResult result{};
    result.error = Ps2ElfAnalysisError::ReachabilityFailed;
    result.message = analyzed.message;
    result.reachability_error = analyzed.error;
    return result;
}

} // namespace

Ps2ElfAnalysisResult analyze_ps2_elf(std::span<const std::uint8_t> bytes,
                                     R5900ReachabilityOptions options) {
    const auto parsed = recompiler::parse_ps2_elf(bytes);
    if (!parsed.ok()) {
        return fail_elf(parsed);
    }

    auto mapped = runtime::Ps2MemoryMap::from_elf(*parsed.image);
    if (!mapped.ok()) {
        return fail_memory(mapped);
    }

    const auto analyzed = analyze_r5900_reachability(
        *mapped.memory, parsed.image->entry_point(), options);
    if (!analyzed.ok()) {
        return fail_reachability(analyzed);
    }

    Ps2ElfAnalysisResult result{};
    result.report = render_r5900_analysis_report(*analyzed.graph);
    return result;
}

} // namespace b3r::analysis
