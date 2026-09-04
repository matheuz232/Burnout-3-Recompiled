#include "recompiler/windows/r5900_block_dispatcher.h"

#include "recompiler/r5900_ir.h"

#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

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

bool is_dispatcher_v0_eligible(R5900Instruction instruction) noexcept {
    switch (instruction) {
    case R5900Instruction::Nop:
    case R5900Instruction::Addu:
    case R5900Instruction::Addiu:
    case R5900Instruction::Ori:
        return true;
    default:
        return false;
    }
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
    R5900DispatchResult result{};
    result.next_pc = start_pc;

    if (max_blocks == 0u) {
        result.reason = R5900DispatchStopReason::InvalidBlockBudget;
        result.message = "R5900 dispatcher block budget must be non-zero";
        return result;
    }

    std::uint32_t current_pc = start_pc;

    while (result.blocks_executed < max_blocks) {
        const auto analyzed = analysis::analyze_r5900_basic_block(
            memory_, current_pc, options_.block_options);
        if (!analyzed.ok()) {
            result.reason = R5900DispatchStopReason::AnalysisFailure;
            result.next_pc = current_pc;
            result.message = format_stage_error("analysis", current_pc, analyzed.message);
            return result;
        }

        const auto& block = *analyzed.block;
        std::vector<analysis::R5900InstructionSite> prefix{};
        prefix.reserve(block.instructions.size());

        std::optional<R5900DispatchStopReason> boundary_reason{};
        std::uint32_t boundary_pc = current_pc;

        for (const auto& site : block.instructions) {
            if (site.decoded.is_branch() || site.decoded.is_jump()) {
                boundary_reason = R5900DispatchStopReason::ControlFlow;
                boundary_pc = site.pc;
                break;
            }

            if (site.decoded.instruction_class == R5900InstructionClass::System) {
                boundary_reason = R5900DispatchStopReason::Trap;
                boundary_pc = site.pc;
                break;
            }

            if (!is_dispatcher_v0_eligible(site.decoded.instruction)) {
                boundary_reason = R5900DispatchStopReason::UnsupportedInstruction;
                boundary_pc = site.pc;
                break;
            }

            prefix.push_back(site);
        }

        if (prefix.empty()) {
            if (boundary_reason.has_value()) {
                result.reason = *boundary_reason;
                result.next_pc = boundary_pc;
                return result;
            }

            result.reason = R5900DispatchStopReason::UnsupportedInstruction;
            result.next_pc = current_pc;
            result.message = format_stage_error(
                "dispatch", current_pc, "analyzed block has no v0-executable instruction prefix");
            return result;
        }

        std::vector<std::uint32_t> guest_words{};
        guest_words.reserve(prefix.size());
        for (const auto& site : prefix) {
            guest_words.push_back(site.decoded.raw);
        }

        auto cached = cache_.find(current_pc);
        const bool exact_cache_hit =
            cached != cache_.end() &&
            cached->second.guest_instruction_count == guest_words.size() &&
            cached->second.guest_words == guest_words;

        if (exact_cache_hit) {
            ++result.cache_hits;
            cached->second.native_block.execute(state);
        } else {
            std::vector<R5900IrInstruction> ir{};
            ir.reserve(prefix.size());
            for (const auto& site : prefix) {
                const auto lowered = lower_r5900_instruction(site.decoded, site.pc);
                if (!lowered.ok()) {
                    result.reason = R5900DispatchStopReason::LoweringFailure;
                    result.next_pc = site.pc;
                    result.message = format_stage_error("lowering", site.pc, lowered.message);
                    return result;
                }

                ir.insert(ir.end(), lowered.instructions.begin(), lowered.instructions.end());
            }

            auto compiled = compile_r5900_ir_x64(ir);
            if (!compiled.ok()) {
                result.reason = R5900DispatchStopReason::CompileFailure;
                result.next_pc = current_pc;
                result.message = format_stage_error("x64 compile", current_pc, compiled.message);
                return result;
            }

            ++result.cache_misses;
            CachedBlock replacement{};
            replacement.start_pc = current_pc;
            replacement.end_pc_exclusive = current_pc +
                                             static_cast<std::uint32_t>(prefix.size() * 4u);
            replacement.guest_words = guest_words;
            replacement.guest_instruction_count = guest_words.size();
            replacement.native_block = std::move(*compiled.block);

            if (cached == cache_.end()) {
                auto [inserted, did_insert] = cache_.emplace(current_pc, std::move(replacement));
                (void)did_insert;
                inserted->second.native_block.execute(state);
            } else {
                cached->second = std::move(replacement);
                cached->second.native_block.execute(state);
            }
        }

        ++result.blocks_executed;
        result.instructions_executed += prefix.size();
        current_pc += static_cast<std::uint32_t>(prefix.size() * 4u);
        result.next_pc = current_pc;

        if (boundary_reason.has_value()) {
            result.reason = *boundary_reason;
            result.next_pc = boundary_pc;
            return result;
        }

        if (result.blocks_executed == max_blocks) {
            result.reason = R5900DispatchStopReason::BlockBudgetExhausted;
            return result;
        }
    }

    result.reason = R5900DispatchStopReason::BlockBudgetExhausted;
    return result;
}

} // namespace b3r::recompiler
