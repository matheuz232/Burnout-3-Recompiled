#include "recompiler/windows/r5900_block_dispatcher.h"

#include <iomanip>
#include <sstream>
#include <string_view>

namespace b3r::recompiler {
namespace {

std::string format_stage_error(std::string_view stage,
                               std::uint32_t pc,
                               const std::string& detail) {
    std::ostringstream out;
    out << stage << " at guest PC 0x"
        << std::hex << std::setw(8) << std::setfill('0') << pc
        << ": " << detail;
    return out.str();
}

} // namespace

R5900BlockDispatcher::R5900BlockDispatcher(const runtime::Ps2MemoryMap& memory,
                                           R5900BlockDispatcherOptions options)
    : memory_(memory), options_(options) {}

void R5900BlockDispatcher::clear_cache() noexcept {
    cache_.clear();
}

std::size_t R5900BlockDispatcher::cache_size() const noexcept {
    return cache_.size();
}

R5900DispatchResult R5900BlockDispatcher::run(std::uint32_t start_pc,
                                              R5900IrExecutionState& state,
                                              std::size_t max_blocks) {
    (void)state;
    R5900DispatchResult result{};
    result.next_pc = start_pc;

    if (max_blocks == 0u) {
        result.reason = R5900DispatchStopReason::InvalidBlockBudget;
        result.message = "R5900 dispatcher block budget must be non-zero";
        return result;
    }

    const auto analyzed = analysis::analyze_r5900_basic_block(
        memory_, start_pc, options_.block_options);
    if (!analyzed.ok()) {
        result.reason = R5900DispatchStopReason::AnalysisFailure;
        result.message = format_stage_error("analysis", start_pc, analyzed.message);
        return result;
    }

    result.reason = R5900DispatchStopReason::UnsupportedInstruction;
    result.message = "R5900 dispatcher Task 1 supports entry validation only";
    return result;
}

} // namespace b3r::recompiler
