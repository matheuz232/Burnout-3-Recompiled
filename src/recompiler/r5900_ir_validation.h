#pragma once

#include "recompiler/r5900_ir.h"

#include <cstddef>
#include <string>

namespace b3r::recompiler {

enum class R5900IrValidationError {
    None = 0,
    MalformedInstruction,
    InvalidRegister,
    UnsupportedOpcode,
};

struct R5900IrValidationResult {
    R5900IrValidationError error{R5900IrValidationError::None};
    std::string message{};

    [[nodiscard]] bool ok() const noexcept {
        return error == R5900IrValidationError::None;
    }
};

[[nodiscard]] R5900IrValidationResult validate_r5900_ir_instruction(
    const R5900IrInstruction& instruction,
    std::size_t instruction_index);

[[nodiscard]] R5900IrValidationResult
validate_r5900_ir_block(const R5900IrBlock& block);

} // namespace b3r::recompiler