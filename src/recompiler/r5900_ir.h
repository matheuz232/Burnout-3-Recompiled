#pragma once

#include "recompiler/r5900_decoder.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace b3r::recompiler {

enum class R5900IrOpcode {
    Nop = 0,
    AddWordSignExtend,
    Or64,
    And64,
    LoadUpperImmediateSignExtend,
    AddPackedU32Saturate128,
    MoveGprLow64,
    ComputeMtsah,
    MoveBits32,
    AddF32ToAccumulator,
    Store128,
};

enum class R5900IrOperandKind {
    Gpr = 0,
    Fpr,
    Immediate,
};

// Standard EE integer instructions may update only the low 64 bits of a
// 128-bit GPR, while packed operations can replace the complete 128-bit value.
enum class R5900IrGprWriteMode {
    None = 0,
    Low64PreserveUpper64,
    Full128,
};

enum class R5900IrDestinationKind {
    Gpr = 0,
    Hi,
    Lo,
    Hi1,
    Lo1,
    Sa,
    Fpr,
    Fcr31,
    FpAccumulator,
};

struct R5900IrDestination {
    R5900IrDestinationKind kind{R5900IrDestinationKind::Gpr};
    std::uint8_t index{};

    constexpr R5900IrDestination() noexcept = default;
    constexpr explicit R5900IrDestination(std::uint8_t gpr_index) noexcept
        : kind(R5900IrDestinationKind::Gpr), index(gpr_index) {}
    constexpr R5900IrDestination(R5900IrDestinationKind destination_kind,
                                 std::uint8_t destination_index) noexcept
        : kind(destination_kind), index(destination_index) {}
};

// Transitional source-compatibility name for the previous GPR-only IR type.
// Existing producers continue to construct a GPR destination while new IR can
// name special/FPU destinations explicitly.
using R5900IrRegister = R5900IrDestination;

struct R5900IrOperand {
    R5900IrOperandKind kind{R5900IrOperandKind::Gpr};
    // Indexed register-file operand. Used for both GPR and FPR kinds.
    std::uint8_t gpr_index{};
    std::int64_t immediate{};
};

struct R5900IrInstruction {
    std::uint32_t guest_pc{};
    std::uint32_t guest_raw{};
    R5900IrOpcode opcode{R5900IrOpcode::Nop};
    std::optional<R5900IrDestination> destination{};
    R5900IrGprWriteMode write_mode{R5900IrGprWriteMode::None};
    std::vector<R5900IrOperand> inputs{};
};

enum class R5900IrTerminatorKind {
    Fallthrough = 0,
    BranchEqual64,
    DirectJump,
    DirectCall,
    IndirectJump,
    IndirectCall,
};

struct R5900IrTerminator {
    std::uint32_t guest_pc{};
    std::uint32_t guest_raw{};
    R5900IrTerminatorKind kind{R5900IrTerminatorKind::Fallthrough};
    std::vector<R5900IrOperand> inputs{};
    std::uint32_t taken_pc{};
    std::uint32_t fallthrough_pc{};
    std::uint32_t target_pc{};
    std::uint32_t link_pc{};
    std::vector<R5900IrInstruction> delay_slot{};
    // Only IndirectCall names a link GPR; DirectCall retains its fixed r31.
    std::optional<std::uint8_t> link_gpr{};
};

struct R5900IrBlock {
    std::vector<R5900IrInstruction> body{};
    R5900IrTerminator terminator{};
};

enum class R5900IrLoweringError {
    None = 0,
    UnsupportedInstruction,
};

struct R5900IrLoweringResult {
    R5900IrLoweringError error{R5900IrLoweringError::None};
    std::string message{};
    std::vector<R5900IrInstruction> instructions{};

    [[nodiscard]] bool ok() const noexcept {
        return error == R5900IrLoweringError::None;
    }
};

[[nodiscard]] R5900IrLoweringResult
lower_r5900_instruction(const R5900DecodedInstruction& decoded, std::uint32_t guest_pc);

} // namespace b3r::recompiler
