#include "recompiler/r5900_ir_executor.h"
#include "recompiler/r5900_ir_validation.h"

#include <cstddef>
#include <cstdint>

namespace b3r::recompiler {
namespace {

void normalize_zero(R5900IrExecutionState& state) {
    state.gpr[0] = {};
}

R5900IrExecutionResult map_validation_failure(const R5900IrValidationResult& validation) {
    switch (validation.error) {
    case R5900IrValidationError::MalformedInstruction:
        return {R5900IrExecutionError::MalformedInstruction, validation.message};
    case R5900IrValidationError::InvalidRegister:
        return {R5900IrExecutionError::InvalidRegister, validation.message};
    case R5900IrValidationError::UnsupportedOpcode:
        return {R5900IrExecutionError::UnsupportedOpcode, validation.message};
    case R5900IrValidationError::None:
    default:
        return {};
    }
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
        const auto validation = validate_r5900_ir_instruction(ir, index);
        if (!validation.ok()) {
            normalize_zero(state);
            return map_validation_failure(validation);
        }

        switch (ir.opcode) {
        case R5900IrOpcode::Nop:
            break;

        case R5900IrOpcode::AddWordSignExtend: {
            const auto lhs = static_cast<std::uint32_t>(read_operand_value(ir.inputs[0], state));
            const auto rhs = static_cast<std::uint32_t>(read_operand_value(ir.inputs[1], state));
            const auto word = static_cast<std::uint32_t>(lhs + rhs);
            if (ir.destination->index != 0u) {
                state.gpr[ir.destination->index].low64 = sign_extend_word(word);
            }
            break;
        }

        case R5900IrOpcode::Or64: {
            const auto value = read_operand_value(ir.inputs[0], state) |
                               read_operand_value(ir.inputs[1], state);
            if (ir.destination->index != 0u) {
                state.gpr[ir.destination->index].low64 = value;
            }
            break;
        }

        default:
            break;
        }
    }

    normalize_zero(state);
    return {};
}

} // namespace b3r::recompiler
