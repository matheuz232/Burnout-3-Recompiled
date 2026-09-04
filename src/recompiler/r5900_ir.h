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
};

enum class R5900IrOperandKind {
    Gpr = 0,
    Immediate,
};

struct R5900IrRegister {
    std::uint8_t index{};
};

struct R5900IrOperand {
    R5900IrOperandKind kind{R5900IrOperandKind::Gpr};
    std::uint8_t gpr_index{};
    std::int64_t immediate{};
};

struct R5900IrInstruction {
    std::uint32_t guest_pc{};
    std::uint32_t guest_raw{};
    R5900IrOpcode opcode{R5900IrOpcode::Nop};
    std::optional<R5900IrRegister> destination{};
    std::vector<R5900IrOperand> inputs{};
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
