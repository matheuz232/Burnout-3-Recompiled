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
    std::uint64_t hi{};
    std::uint64_t lo{};
    std::uint64_t hi1{};
    std::uint64_t lo1{};
    std::uint32_t sa{};
    std::array<std::uint32_t, 32> fpr{};
    std::uint32_t fcr31{};
    std::uint32_t fp_acc{};
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

struct R5900IrBlockExecutionResult {
    R5900IrExecutionError error{R5900IrExecutionError::None};
    std::string message{};
    std::uint32_t next_pc{};

    [[nodiscard]] bool ok() const noexcept {
        return error == R5900IrExecutionError::None;
    }
};

[[nodiscard]] R5900IrExecutionResult
execute_r5900_ir(const std::vector<R5900IrInstruction>& instructions,
                 R5900IrExecutionState& state);

[[nodiscard]] R5900IrBlockExecutionResult
execute_r5900_ir_block(const R5900IrBlock& block,
                       R5900IrExecutionState& state);

} // namespace b3r::recompiler
