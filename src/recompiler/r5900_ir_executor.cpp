#include "recompiler/r5900_ir_executor.h"
#include "recompiler/r5900_ir_validation.h"

#include <bit>
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

std::uint32_t packed_lane(const R5900IrGprValue& value, std::size_t lane) {
    if (lane < 2u) {
        return static_cast<std::uint32_t>(value.low64 >> (lane * 32u));
    }
    return static_cast<std::uint32_t>(value.high64 >> ((lane - 2u) * 32u));
}

void set_packed_lane(R5900IrGprValue& value, std::size_t lane, std::uint32_t lane_value) {
    if (lane < 2u) {
        const auto shift = static_cast<unsigned>(lane * 32u);
        const auto mask = ~(0xffffffffull << shift);
        value.low64 = (value.low64 & mask) |
                      (static_cast<std::uint64_t>(lane_value) << shift);
        return;
    }

    const auto shift = static_cast<unsigned>((lane - 2u) * 32u);
    const auto mask = ~(0xffffffffull << shift);
    value.high64 = (value.high64 & mask) |
                   (static_cast<std::uint64_t>(lane_value) << shift);
}

std::uint32_t saturating_add_u32(std::uint32_t lhs, std::uint32_t rhs) {
    const auto sum = static_cast<std::uint64_t>(lhs) + static_cast<std::uint64_t>(rhs);
    return sum > 0xffffffffull ? 0xffffffffu : static_cast<std::uint32_t>(sum);
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

        case R5900IrOpcode::And64: {
            const auto value = read_operand_value(ir.inputs[0], state) &
                               read_operand_value(ir.inputs[1], state);
            if (ir.destination->index != 0u) {
                state.gpr[ir.destination->index].low64 = value;
            }
            break;
        }

        case R5900IrOpcode::LoadUpperImmediateSignExtend: {
            const auto immediate16 = static_cast<std::uint16_t>(ir.inputs[0].immediate);
            const auto word = static_cast<std::uint32_t>(immediate16) << 16u;
            if (ir.destination->index != 0u) {
                state.gpr[ir.destination->index].low64 = sign_extend_word(word);
            }
            break;
        }

        case R5900IrOpcode::MoveGprLow64: {
            const auto value = state.gpr[ir.inputs[0].gpr_index].low64;
            switch (ir.destination->kind) {
            case R5900IrDestinationKind::Hi:
                state.hi = value;
                break;
            case R5900IrDestinationKind::Lo:
                state.lo = value;
                break;
            case R5900IrDestinationKind::Hi1:
                state.hi1 = value;
                break;
            case R5900IrDestinationKind::Lo1:
                state.lo1 = value;
                break;
            default:
                break;
            }
            break;
        }

        case R5900IrOpcode::ComputeMtsah: {
            const auto source = static_cast<std::uint32_t>(
                state.gpr[ir.inputs[0].gpr_index].low64);
            const auto immediate_value = static_cast<std::uint32_t>(ir.inputs[1].immediate);
            state.sa = ((source & 0x7u) ^ (immediate_value & 0x7u)) << 1u;
            break;
        }

        case R5900IrOpcode::AddPackedU32Saturate128: {
            const auto lhs = state.gpr[ir.inputs[0].gpr_index];
            const auto rhs = state.gpr[ir.inputs[1].gpr_index];
            R5900IrGprValue result{};
            for (std::size_t lane = 0; lane < 4u; ++lane) {
                set_packed_lane(
                    result,
                    lane,
                    saturating_add_u32(packed_lane(lhs, lane), packed_lane(rhs, lane)));
            }
            if (ir.destination->index != 0u) {
                state.gpr[ir.destination->index] = result;
            }
            break;
        }

        case R5900IrOpcode::MoveBits32: {
            const auto raw = static_cast<std::uint32_t>(
                state.gpr[ir.inputs[0].gpr_index].low64);
            if (ir.destination->kind == R5900IrDestinationKind::Fpr) {
                state.fpr[ir.destination->index] = raw;
            } else {
                state.fcr31 = raw;
            }
            break;
        }

        case R5900IrOpcode::AddF32ToAccumulator: {
            const auto lhs = std::bit_cast<float>(state.fpr[ir.inputs[0].gpr_index]);
            const auto rhs = std::bit_cast<float>(state.fpr[ir.inputs[1].gpr_index]);
            state.fp_acc = std::bit_cast<std::uint32_t>(lhs + rhs);
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
