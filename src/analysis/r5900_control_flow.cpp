#include "analysis/r5900_control_flow.h"

#include <cstdint>
#include <utility>

namespace b3r::analysis {
namespace {

constexpr std::uint32_t kElfPfExecute = 0x1u;
constexpr std::uint64_t kGuestAddressSpaceSize = (std::uint64_t{1} << 32u);

enum class RangeStatus {
    Unmapped,
    NonExecutable,
    Executable,
};

R5900BasicBlockResult fail(R5900ControlFlowError error, const char* message) {
    R5900BasicBlockResult result{};
    result.error = error;
    result.message = message;
    return result;
}

R5900BasicBlockResult succeed(R5900BasicBlock block) {
    R5900BasicBlockResult result{};
    result.block = std::move(block);
    return result;
}

RangeStatus range_status(const runtime::Ps2MemoryMap& memory,
                         std::uint32_t address,
                         std::size_t length) noexcept {
    if (length > kGuestAddressSpaceSize) {
        return RangeStatus::Unmapped;
    }

    const std::uint64_t access_begin = address;
    const std::uint64_t access_end = access_begin + static_cast<std::uint64_t>(length);
    if (access_end > kGuestAddressSpaceSize) {
        return RangeStatus::Unmapped;
    }

    for (const auto& region : memory.regions()) {
        const std::uint64_t region_begin = region.guest_base;
        const std::uint64_t region_end = region_begin + static_cast<std::uint64_t>(region.size);
        if (access_begin >= region_begin && access_end <= region_end) {
            return (region.flags & kElfPfExecute) != 0u ? RangeStatus::Executable
                                                       : RangeStatus::NonExecutable;
        }
    }

    return RangeStatus::Unmapped;
}

std::optional<R5900InstructionSite>
fetch_site(const runtime::Ps2MemoryMap& memory,
           std::uint32_t pc,
           bool require_executable) noexcept {
    const auto word = memory.read_u32(pc);
    if (!word.has_value()) {
        return std::nullopt;
    }

    if (require_executable && range_status(memory, pc, 4u) != RangeStatus::Executable) {
        return std::nullopt;
    }

    return R5900InstructionSite{pc, recompiler::decode_r5900(*word)};
}

R5900BasicBlockResult fetch_instruction_failure(const runtime::Ps2MemoryMap& memory,
                                                 std::uint32_t pc,
                                                 bool require_executable) {
    const auto status = range_status(memory, pc, 4u);
    if (require_executable && status == RangeStatus::NonExecutable) {
        return fail(R5900ControlFlowError::NonExecutableInstruction,
                    "R5900 instruction fetch is mapped but not executable");
    }
    return fail(R5900ControlFlowError::UnmappedInstruction,
                "R5900 instruction fetch is outside mapped guest memory");
}

R5900BasicBlockResult fetch_delay_slot_failure(const runtime::Ps2MemoryMap& memory,
                                                std::uint32_t pc,
                                                bool require_executable) {
    const auto status = range_status(memory, pc, 4u);
    if (require_executable && status == RangeStatus::NonExecutable) {
        return fail(R5900ControlFlowError::NonExecutableDelaySlot,
                    "R5900 architectural delay slot is mapped but not executable");
    }
    return fail(R5900ControlFlowError::MissingDelaySlot,
                "R5900 control transfer has no mapped architectural delay slot");
}

void append_edge(R5900BasicBlock& block,
                 R5900EdgeKind kind,
                 std::optional<std::uint32_t> target = std::nullopt) {
    block.edges.push_back(R5900ControlFlowEdge{kind, target});
}

R5900BasicBlockResult finish_control_transfer(const runtime::Ps2MemoryMap& memory,
                                               R5900BasicBlock block,
                                               const R5900InstructionSite& terminator,
                                               bool require_executable) {
    const std::uint32_t delay_pc = terminator.pc + 4u;
    const auto delay_slot = fetch_site(memory, delay_pc, require_executable);
    if (!delay_slot.has_value()) {
        return fetch_delay_slot_failure(memory, delay_pc, require_executable);
    }

    block.delay_slot = *delay_slot;
    block.delay_slot_executes_on_fallthrough = !terminator.decoded.likely;

    if (terminator.decoded.is_branch()) {
        const auto target = terminator.decoded.direct_target(terminator.pc);
        if (!target.has_value()) {
            return fail(R5900ControlFlowError::UnmappedInstruction,
                        "decoded immediate branch unexpectedly lacks a direct target");
        }
        block.end_kind = R5900BlockEndKind::ConditionalBranch;
        append_edge(block, R5900EdgeKind::BranchTaken, *target);
        append_edge(block, R5900EdgeKind::BranchNotTaken, terminator.pc + 8u);
        return succeed(std::move(block));
    }

    using recompiler::R5900Instruction;
    switch (terminator.decoded.instruction) {
    case R5900Instruction::J: {
        const auto target = terminator.decoded.direct_target(terminator.pc);
        if (!target.has_value()) {
            return fail(R5900ControlFlowError::UnmappedInstruction,
                        "decoded direct jump unexpectedly lacks a target");
        }
        block.end_kind = R5900BlockEndKind::DirectJump;
        append_edge(block, R5900EdgeKind::DirectJump, *target);
        return succeed(std::move(block));
    }
    case R5900Instruction::Jal: {
        const auto target = terminator.decoded.direct_target(terminator.pc);
        if (!target.has_value()) {
            return fail(R5900ControlFlowError::UnmappedInstruction,
                        "decoded direct call unexpectedly lacks a target");
        }
        block.end_kind = R5900BlockEndKind::DirectCall;
        append_edge(block, R5900EdgeKind::DirectCall, *target);
        append_edge(block, R5900EdgeKind::CallContinuation, terminator.pc + 8u);
        return succeed(std::move(block));
    }
    case R5900Instruction::Jr:
        block.end_kind = R5900BlockEndKind::IndirectJump;
        append_edge(block, R5900EdgeKind::IndirectJump);
        return succeed(std::move(block));
    case R5900Instruction::Jalr:
        block.end_kind = R5900BlockEndKind::IndirectCall;
        append_edge(block, R5900EdgeKind::IndirectCall);
        append_edge(block, R5900EdgeKind::CallContinuation, terminator.pc + 8u);
        return succeed(std::move(block));
    default:
        block.end_kind = R5900BlockEndKind::UnsupportedInstruction;
        block.delay_slot.reset();
        block.edges.clear();
        return succeed(std::move(block));
    }
}

} // namespace

R5900BasicBlockResult analyze_r5900_basic_block(const runtime::Ps2MemoryMap& memory,
                                                 std::uint32_t start_pc,
                                                 R5900ControlFlowOptions options) {
    if (options.max_instructions == 0u) {
        return fail(R5900ControlFlowError::InvalidInstructionLimit,
                    "R5900 control-flow instruction limit must be non-zero");
    }
    if ((start_pc & 0x3u) != 0u) {
        return fail(R5900ControlFlowError::UnalignedStart,
                    "R5900 basic-block start PC must be 4-byte aligned");
    }

    R5900BasicBlock block{};
    block.start_pc = start_pc;
    block.instructions.reserve(options.max_instructions);

    std::uint32_t pc = start_pc;
    for (std::size_t i = 0; i < options.max_instructions; ++i) {
        const auto site = fetch_site(memory, pc, options.require_executable);
        if (!site.has_value()) {
            return fetch_instruction_failure(memory, pc, options.require_executable);
        }

        block.instructions.push_back(*site);
        const auto& decoded = site->decoded;

        if (decoded.instruction == recompiler::R5900Instruction::Unknown) {
            block.end_kind = R5900BlockEndKind::UnsupportedInstruction;
            return succeed(std::move(block));
        }

        if (decoded.instruction_class == recompiler::R5900InstructionClass::System) {
            block.end_kind = R5900BlockEndKind::Trap;
            return succeed(std::move(block));
        }

        if (decoded.is_branch() || decoded.is_jump()) {
            return finish_control_transfer(memory, std::move(block), *site, options.require_executable);
        }

        pc += 4u;
    }

    block.end_kind = R5900BlockEndKind::InstructionLimit;
    append_edge(block, R5900EdgeKind::Fallthrough, pc);
    return succeed(std::move(block));
}

} // namespace b3r::analysis
