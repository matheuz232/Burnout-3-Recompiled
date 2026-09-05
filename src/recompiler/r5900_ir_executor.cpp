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

void reset_memory_status(R5900IrExecutionContext& context) noexcept {
    context.memory_fault = {};
    context.current_memory_guest_pc = 0u;
}

R5900IrExecutionResult invalid_context_failure() {
    return {R5900IrExecutionError::MalformedInstruction,
            "R5900 execution context has no CPU state"};
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

R5900IrBlockExecutionResult map_block_validation_failure(
    const R5900IrValidationResult& validation) {
    const auto mapped = map_validation_failure(validation);
    return {mapped.error, mapped.message, 0u};
}

R5900IrBlockExecutionResult map_block_execution_failure(
    const R5900IrExecutionResult& execution) {
    return {execution.error, execution.message, 0u};
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

R5900IrExecutionResult execute_ir_sequence(
    const std::vector<R5900IrInstruction>& instructions,
    R5900IrExecutionContext& context) {
    if (context.state == nullptr) {
        return invalid_context_failure();
    }

    auto& state = *context.state;
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

        case R5900IrOpcode::Store128: {
            const auto base = static_cast<std::uint32_t>(
                state.gpr[ir.inputs[0].gpr_index].low64);
            const auto offset = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(ir.inputs[2].immediate));
            const auto address =
                static_cast<std::uint32_t>(base + offset) & 0xfffffff0u;
            const auto source = state.gpr[ir.inputs[1].gpr_index];

            context.current_memory_guest_pc = ir.guest_pc;
            const bool available = context.memory.user != nullptr &&
                                   context.memory.write128 != nullptr;
            const bool written = available &&
                context.memory.write128(context.memory.user,
                                        address,
                                        source.low64,
                                        source.high64);
            if (!written) {
                context.memory_fault = {
                    true,
                    R5900IrMemoryAccessKind::Store,
                    ir.guest_pc,
                    address,
                    16u,
                };
                normalize_zero(state);
                return {R5900IrExecutionError::MemoryAccessFailure,
                        "R5900 Store128 guest-memory write failed"};
            }
            break;
        }

        default:
            normalize_zero(state);
            return {R5900IrExecutionError::UnsupportedOpcode,
                    "unsupported R5900 IR execution opcode"};
        }
    }

    normalize_zero(state);
    return {};
}

} // namespace

R5900IrExecutionResult
execute_r5900_ir(const std::vector<R5900IrInstruction>& instructions,
                 R5900IrExecutionContext& context) {
    reset_memory_status(context);
    return execute_ir_sequence(instructions, context);
}

R5900IrExecutionResult
execute_r5900_ir(const std::vector<R5900IrInstruction>& instructions,
                 R5900IrExecutionState& state) {
    R5900IrExecutionContext context{};
    context.state = &state;
    return execute_r5900_ir(instructions, context);
}

R5900IrBlockExecutionResult
execute_r5900_ir_block(const R5900IrBlock& block,
                       R5900IrExecutionContext& context) {
    reset_memory_status(context);
    if (context.state == nullptr) {
        const auto failure = invalid_context_failure();
        return map_block_execution_failure(failure);
    }

    const auto validation = validate_r5900_ir_block(block);
    if (!validation.ok()) {
        return map_block_validation_failure(validation);
    }

    const auto body_result = execute_ir_sequence(block.body, context);
    if (!body_result.ok()) {
        return map_block_execution_failure(body_result);
    }

    auto& state = *context.state;
    switch (block.terminator.kind) {
    case R5900IrTerminatorKind::Fallthrough:
        return {R5900IrExecutionError::None,
                {},
                block.terminator.fallthrough_pc};

    case R5900IrTerminatorKind::BranchEqual64: {
        const bool taken =
            state.gpr[block.terminator.inputs[0].gpr_index].low64 ==
            state.gpr[block.terminator.inputs[1].gpr_index].low64;

        const auto delay_result =
            execute_ir_sequence(block.terminator.delay_slot, context);
        if (!delay_result.ok()) {
            return map_block_execution_failure(delay_result);
        }

        return {R5900IrExecutionError::None,
                {},
                taken ? block.terminator.taken_pc
                      : block.terminator.fallthrough_pc};
    }

    case R5900IrTerminatorKind::DirectJump: {
        const auto delay_result =
            execute_ir_sequence(block.terminator.delay_slot, context);
        if (!delay_result.ok()) {
            return map_block_execution_failure(delay_result);
        }
        return {R5900IrExecutionError::None,
                {},
                block.terminator.target_pc};
    }

    case R5900IrTerminatorKind::DirectCall: {
        state.gpr[31].low64 =
            static_cast<std::uint64_t>(block.terminator.link_pc);
        normalize_zero(state);
        const auto delay_result =
            execute_ir_sequence(block.terminator.delay_slot, context);
        if (!delay_result.ok()) {
            return map_block_execution_failure(delay_result);
        }
        return {R5900IrExecutionError::None,
                {},
                block.terminator.target_pc};
    }

    case R5900IrTerminatorKind::IndirectJump: {
        const auto target = static_cast<std::uint32_t>(
            state.gpr[block.terminator.inputs[0].gpr_index].low64);
        const auto delay_result =
            execute_ir_sequence(block.terminator.delay_slot, context);
        if (!delay_result.ok()) {
            return map_block_execution_failure(delay_result);
        }
        return {R5900IrExecutionError::None, {}, target};
    }

    case R5900IrTerminatorKind::IndirectCall: {
        const auto target = static_cast<std::uint32_t>(
            state.gpr[block.terminator.inputs[0].gpr_index].low64);
        const auto link_gpr = block.terminator.link_gpr;
        if (link_gpr != 0u) {
            state.gpr[link_gpr].low64 =
                static_cast<std::uint64_t>(block.terminator.link_pc);
        }
        normalize_zero(state);
        const auto delay_result =
            execute_ir_sequence(block.terminator.delay_slot, context);
        if (!delay_result.ok()) {
            return map_block_execution_failure(delay_result);
        }
        return {R5900IrExecutionError::None, {}, target};
    }

    default:
        return {R5900IrExecutionError::UnsupportedOpcode,
                "unsupported R5900 block terminator",
                0u};
    }
}

R5900IrBlockExecutionResult
execute_r5900_ir_block(const R5900IrBlock& block,
                       R5900IrExecutionState& state) {
    R5900IrExecutionContext context{};
    context.state = &state;
    return execute_r5900_ir_block(block, context);
}

} // namespace b3r::recompiler
