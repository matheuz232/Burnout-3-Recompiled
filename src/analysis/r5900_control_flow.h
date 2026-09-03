#pragma once

#include "recompiler/r5900_decoder.h"
#include "runtime/ps2_memory_map.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace b3r::analysis {

enum class R5900ControlFlowError {
    None = 0,
    InvalidInstructionLimit,
    UnalignedStart,
    UnmappedInstruction,
    NonExecutableInstruction,
    MissingDelaySlot,
    NonExecutableDelaySlot,
};

enum class R5900BlockEndKind {
    InstructionLimit = 0,
    ConditionalBranch,
    DirectJump,
    DirectCall,
    IndirectJump,
    IndirectCall,
    Trap,
    UnsupportedInstruction,
};

enum class R5900EdgeKind {
    BranchTaken = 0,
    BranchNotTaken,
    DirectJump,
    DirectCall,
    CallContinuation,
    IndirectJump,
    IndirectCall,
    Fallthrough,
};

struct R5900InstructionSite {
    std::uint32_t pc{};
    recompiler::R5900DecodedInstruction decoded{};
};

struct R5900ControlFlowEdge {
    R5900EdgeKind kind{R5900EdgeKind::Fallthrough};
    std::optional<std::uint32_t> target{};
};

struct R5900BasicBlock {
    std::uint32_t start_pc{};
    std::vector<R5900InstructionSite> instructions{};
    std::optional<R5900InstructionSite> delay_slot{};
    R5900BlockEndKind end_kind{R5900BlockEndKind::InstructionLimit};
    bool delay_slot_executes_on_fallthrough{true};
    std::vector<R5900ControlFlowEdge> edges{};
};

struct R5900ControlFlowOptions {
    std::size_t max_instructions{1024};
    bool require_executable{true};
};

struct R5900BasicBlockResult {
    R5900ControlFlowError error{R5900ControlFlowError::None};
    std::string message{};
    std::optional<R5900BasicBlock> block{};

    [[nodiscard]] bool ok() const noexcept {
        return error == R5900ControlFlowError::None && block.has_value();
    }
};

[[nodiscard]] R5900BasicBlockResult
analyze_r5900_basic_block(const runtime::Ps2MemoryMap& memory,
                          std::uint32_t start_pc,
                          R5900ControlFlowOptions options = {});

} // namespace b3r::analysis
