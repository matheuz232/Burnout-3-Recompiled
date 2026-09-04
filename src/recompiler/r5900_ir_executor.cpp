#include "recompiler/r5900_ir_executor.h"

#include <cstddef>
#include <cstdint>
#include <sstream>

namespace b3r::recompiler {
namespace {

void normalize_zero(R5900IrExecutionState& state) {
    state.gpr[0] = {};
}

R5900IrExecutionResult failure(R5900IrExecutionError error,
                               std::size_t index,
                               std::uint32_t guest_pc,
                               const char* reason) {
    std::ostringstream message;
    message << "IR instruction " << index << " at guest PC 0x" << std::hex << guest_pc << ": " << reason;
    return {error, message.str()};
}

R5900IrExecutionResult validate_operand(const R5900IrOperand& operand,
                                        std::size_t index,
                                        std::uint32_t guest_pc) {
    switch (operand.kind) {
    case R5900IrOperandKind::Immediate:
        return {};
    case R5900IrOperandKind::Gpr:
        if (operand.gpr_index >= 32u) {
            return failure(R5900IrExecutionError::InvalidRegister,
                           index,
                           guest_pc,
                           "invalid source GPR");
        }
        return {};
    default:
        return failure(R5900IrExecutionError::MalformedInstruction,
                       index,
                       guest_pc,
                       "unsupported operand kind");
    }
}

R5900IrExecutionResult validate_write(const R5900IrInstruction& ir,
                                      std::size_t index) {
    if (!ir.destination.has_value()) {
        return failure(R5900IrExecutionError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "missing destination");
    }
    if (ir.destination->index >= 32u) {
        return failure(R5900IrExecutionError::InvalidRegister,
                       index,
                       ir.guest_pc,
                       "invalid destination GPR");
    }
    if (ir.write_mode != R5900IrGprWriteMode::Low64PreserveUpper64) {
        return failure(R5900IrExecutionError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "invalid GPR write mode");
    }
    if (ir.inputs.size() != 2u) {
        return failure(R5900IrExecutionError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "expected exactly two inputs");
    }
    for (const auto& operand : ir.inputs) {
        const auto validation = validate_operand(operand, index, ir.guest_pc);
        if (!validation.ok()) {
            return validation;
        }
    }
    return {};
}

R5900IrExecutionResult validate_nop(const R5900IrInstruction& ir,
                                    std::size_t index) {
    if (ir.destination.has_value() ||
        !ir.inputs.empty() ||
        ir.write_mode != R5900IrGprWriteMode::None) {
        return failure(R5900IrExecutionError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "malformed Nop");
    }
    return {};
}

std::uint64_t read_operand_value(const R5900IrOperand& operand,
                                 const R5900IrExecutionState& state) {
    if (operand.kind == R5900IrOperandKind::Immediate) {
        return static_cast<std::uint64_t>(operand.immediate);
    }
    return state.gpr[operand.gpr_index].low64;
}

std::uint64_t sign_extend_word(std::uint32_t value) {
    if ((value & 0x80000000u) != 0u) {
        return 0xffffffff00000000ull | static_cast<std::uint64_t>(value);
    }
    return static_cast<std::uint64_t>(value);
}

} // namespace

R5900IrExecutionResult
execute_r5900_ir(const std::vector<R5900IrInstruction>& instructions,
                 R5900IrExecutionState& state) {
    normalize_zero(state);

    for (std::size_t index = 0; index < instructions.size(); ++index) {
        const auto& ir = instructions[index];

        switch (ir.opcode) {
        case R5900IrOpcode::Nop: {
            const auto validation = validate_nop(ir, index);
            if (!validation.ok()) {
                normalize_zero(state);
                return validation;
            }
            break;
        }

        case R5900IrOpcode::AddWordSignExtend: {
            const auto validation = validate_write(ir, index);
            if (!validation.ok()) {
                normalize_zero(state);
                return validation;
            }
            const auto lhs = static_cast<std::uint32_t>(read_operand_value(ir.inputs[0], state));
            const auto rhs = static_cast<std::uint32_t>(read_operand_value(ir.inputs[1], state));
            const auto word = static_cast<std::uint32_t>(lhs + rhs);
            if (ir.destination->index != 0u) {
                state.gpr[ir.destination->index].low64 = sign_extend_word(word);
            }
            break;
        }

        case R5900IrOpcode::Or64: {
            const auto validation = validate_write(ir, index);
            if (!validation.ok()) {
                normalize_zero(state);
                return validation;
            }
            const auto value = read_operand_value(ir.inputs[0], state) |
                               read_operand_value(ir.inputs[1], state);
            if (ir.destination->index != 0u) {
                state.gpr[ir.destination->index].low64 = value;
            }
            break;
        }

        default:
            normalize_zero(state);
            return failure(R5900IrExecutionError::UnsupportedOpcode,
                           index,
                           ir.guest_pc,
                           "unsupported opcode");
        }
    }

    normalize_zero(state);
    return {};
}

} // namespace b3r::recompiler
