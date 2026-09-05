#include "recompiler/windows/r5900_block_dispatcher.h"

#include "recompiler/r5900_ir.h"

#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace b3r::recompiler {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

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
    case R5900Instruction::Andi:
    case R5900Instruction::Lui:
    case R5900Instruction::Mthi:
    case R5900Instruction::Mtlo:
    case R5900Instruction::Mthi1:
    case R5900Instruction::Mtlo1:
    case R5900Instruction::Mtsah:
    case R5900Instruction::Padduw:
    case R5900Instruction::Mtc1:
    case R5900Instruction::Ctc1:
    case R5900Instruction::AddaS:
    case R5900Instruction::Sync:
        return true;
    default:
        return false;
    }
}

R5900IrOperand dispatcher_gpr(std::uint8_t index) {
    R5900IrOperand operand{};
    operand.kind = R5900IrOperandKind::Gpr;
    operand.gpr_index = index;
    return operand;
}

void fnv_byte(std::uint64_t& hash, std::uint8_t byte) noexcept {
    hash ^= byte;
    hash *= kFnvPrime;
}

void fnv_u32_le(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned shift = 0u; shift < 32u; shift += 8u) {
        fnv_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void fnv_u64_le(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned shift = 0u; shift < 64u; shift += 8u) {
        fnv_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

std::uint64_t fingerprint_guest_words(
    std::uint32_t start_pc,
    const std::vector<std::uint32_t>& words) noexcept {
    std::uint64_t hash = kFnvOffset;
    fnv_u32_le(hash, start_pc);
    fnv_u64_le(hash, static_cast<std::uint64_t>(words.size()));
    for (const auto word : words) {
        fnv_u32_le(hash, word);
    }
    return hash;
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
        result.message = format_stage_error(
            "budget", start_pc, "R5900 dispatcher block budget must be non-zero");
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
        const bool has_supported_beq =
            block.end_kind == analysis::R5900BlockEndKind::ConditionalBranch &&
            !block.instructions.empty() &&
            block.instructions.back().decoded.instruction == R5900Instruction::Beq;

        const analysis::R5900InstructionSite* beq_site =
            has_supported_beq ? &block.instructions.back() : nullptr;

        std::vector<analysis::R5900InstructionSite> body_sites{};
        body_sites.reserve(block.instructions.size());

        std::optional<R5900DispatchStopReason> boundary_reason{};
        std::uint32_t boundary_pc = current_pc;

        for (std::size_t index = 0; index < block.instructions.size(); ++index) {
            const auto& site = block.instructions[index];
            if (has_supported_beq && index + 1u == block.instructions.size()) {
                break;
            }

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

            body_sites.push_back(site);
        }

        if (body_sites.empty() && !has_supported_beq) {
            if (boundary_reason.has_value()) {
                result.reason = *boundary_reason;
                result.next_pc = boundary_pc;
                return result;
            }

            result.reason = R5900DispatchStopReason::UnsupportedInstruction;
            result.next_pc = current_pc;
            result.message = format_stage_error(
                "dispatch", current_pc, "analyzed block has no v0-executable instruction candidate");
            return result;
        }

        std::vector<std::uint32_t> guest_words{};
        guest_words.reserve(body_sites.size() + (has_supported_beq ? 2u : 0u));
        for (const auto& site : body_sites) {
            const auto word = memory_.read_u32(site.pc);
            if (!word.has_value()) {
                result.reason = R5900DispatchStopReason::AnalysisFailure;
                result.next_pc = site.pc;
                result.message = format_stage_error(
                    "analysis", site.pc, "selected guest instruction became unreadable");
                return result;
            }
            guest_words.push_back(*word);
        }

        if (has_supported_beq) {
            if (beq_site == nullptr || !block.delay_slot.has_value()) {
                result.reason = R5900DispatchStopReason::AnalysisFailure;
                result.next_pc = current_pc;
                result.message = format_stage_error(
                    "analysis", current_pc, "supported BEQ candidate lacks terminator or delay slot");
                return result;
            }

            const auto beq_word = memory_.read_u32(beq_site->pc);
            if (!beq_word.has_value()) {
                result.reason = R5900DispatchStopReason::AnalysisFailure;
                result.next_pc = beq_site->pc;
                result.message = format_stage_error(
                    "analysis", beq_site->pc, "selected BEQ became unreadable");
                return result;
            }
            guest_words.push_back(*beq_word);

            const auto delay_word = memory_.read_u32(block.delay_slot->pc);
            if (!delay_word.has_value()) {
                result.reason = R5900DispatchStopReason::AnalysisFailure;
                result.next_pc = block.delay_slot->pc;
                result.message = format_stage_error(
                    "analysis", block.delay_slot->pc, "selected delay slot became unreadable");
                return result;
            }
            guest_words.push_back(*delay_word);
        }

        const auto fingerprint = fingerprint_guest_words(current_pc, guest_words);
        auto cached = cache_.find(current_pc);
        const bool exact_cache_hit =
            cached != cache_.end() &&
            cached->second.start_pc == current_pc &&
            cached->second.guest_instruction_count == guest_words.size() &&
            cached->second.fingerprint == fingerprint &&
            cached->second.guest_words == guest_words;

        std::uint32_t native_next_pc{};
        std::size_t executed_instruction_count = guest_words.size();

        if (exact_cache_hit) {
            ++result.cache_hits;
            native_next_pc = cached->second.native_block.execute(state);
            executed_instruction_count = cached->second.guest_instruction_count;
        } else {
            if (cached == cache_.end()) {
                ++result.cache_misses;
            } else {
                ++result.recompilations;
            }

            R5900IrBlock ir_block{};
            ir_block.body.reserve(body_sites.size());
            for (const auto& site : body_sites) {
                const auto lowered = lower_r5900_instruction(site.decoded, site.pc);
                if (!lowered.ok()) {
                    result.reason = R5900DispatchStopReason::LoweringFailure;
                    result.next_pc = site.pc;
                    result.message = format_stage_error("lowering", site.pc, lowered.message);
                    return result;
                }

                ir_block.body.insert(ir_block.body.end(),
                                     lowered.instructions.begin(),
                                     lowered.instructions.end());
            }

            if (has_supported_beq) {
                const auto target = beq_site->decoded.direct_target(beq_site->pc);
                if (!target.has_value()) {
                    result.reason = R5900DispatchStopReason::AnalysisFailure;
                    result.next_pc = beq_site->pc;
                    result.message = format_stage_error(
                        "analysis", beq_site->pc, "decoded BEQ unexpectedly lacks direct target");
                    return result;
                }

                ir_block.terminator.guest_pc = beq_site->pc;
                ir_block.terminator.guest_raw = beq_site->decoded.raw;
                ir_block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;
                ir_block.terminator.inputs = {
                    dispatcher_gpr(beq_site->decoded.rs),
                    dispatcher_gpr(beq_site->decoded.rt),
                };
                ir_block.terminator.taken_pc = *target;
                ir_block.terminator.fallthrough_pc = beq_site->pc + 8u;

                const auto& delay = *block.delay_slot;
                const auto lowered_delay = lower_r5900_instruction(delay.decoded, delay.pc);
                if (!lowered_delay.ok() || lowered_delay.instructions.size() != 1u) {
                    result.reason = R5900DispatchStopReason::LoweringFailure;
                    result.next_pc = delay.pc;
                    result.message = format_stage_error(
                        "lowering",
                        delay.pc,
                        lowered_delay.ok()
                            ? "BEQ delay slot must lower to exactly one IR instruction"
                            : lowered_delay.message);
                    return result;
                }
                ir_block.terminator.delay_slot = lowered_delay.instructions;
            } else {
                const auto fallthrough_pc =
                    current_pc + static_cast<std::uint32_t>(body_sites.size() * 4u);
                ir_block.terminator.guest_pc = fallthrough_pc;
                ir_block.terminator.kind = R5900IrTerminatorKind::Fallthrough;
                ir_block.terminator.fallthrough_pc = fallthrough_pc;
            }

            auto compiled = compile_r5900_ir_x64(ir_block);
            if (!compiled.ok()) {
                result.reason = R5900DispatchStopReason::CompileFailure;
                result.next_pc = current_pc;
                result.message = format_stage_error("x64 compile", current_pc, compiled.message);
                return result;
            }

            CachedBlock replacement{};
            replacement.start_pc = current_pc;
            replacement.end_pc_exclusive = has_supported_beq
                ? beq_site->pc + 8u
                : current_pc + static_cast<std::uint32_t>(body_sites.size() * 4u);
            replacement.fingerprint = fingerprint;
            replacement.guest_words = guest_words;
            replacement.guest_instruction_count = guest_words.size();
            replacement.native_block = std::move(*compiled.block);

            if (cached == cache_.end()) {
                auto [inserted, did_insert] = cache_.emplace(current_pc, std::move(replacement));
                (void)did_insert;
                native_next_pc = inserted->second.native_block.execute(state);
                executed_instruction_count = inserted->second.guest_instruction_count;
            } else {
                cached->second = std::move(replacement);
                native_next_pc = cached->second.native_block.execute(state);
                executed_instruction_count = cached->second.guest_instruction_count;
            }
        }

        ++result.blocks_executed;
        result.instructions_executed += executed_instruction_count;
        current_pc = native_next_pc;
        result.next_pc = native_next_pc;

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
