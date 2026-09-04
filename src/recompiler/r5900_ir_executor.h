#pragma once

#include "recompiler/r5900_ir.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace b3r::recompiler {

struct R5900IrGprValue {
    std::uint64_t low64{};
    std::uint64_t high64{};
};

struct R5900IrExecutionState {
    std::array<R5900IrGprValue, 32> gpr{};
};

enum class R5900IrExecutionError {
    None = 0,
    MalformedInstruction,
    InvalidRegister,
    UnsupportedOpcode,
};

struct R5900IrExecutionResult {
    R5900IrExecutionError error{R5900IrExecutionError::None};
    std::string message{};

    [[nodiscard]] bool ok() const noexcept {
        return error == R5900IrExecutionError::None;
    }
};

[[nodiscard]] R5900IrExecutionResult
execute_r5900_ir(const std::vector<R5900IrInstruction>& instructions,
                 R5900IrExecutionState& state);

} // namespace b3r::recompiler
