#include "recompiler/r5900_ir_validation.h"

#include <cstdint>
#include <sstream>

namespace b3r::recompiler {
namespace {

R5900IrValidationResult failure(R5900IrValidationError error,
                                std::size_t index,
                                std::uint32_t guest_pc,
                                const char* reason) {
    std::ostringstream message;
    message << "IR instruction " << index << " at guest PC 0x"
            << std::hex << guest_pc << ": " << reason;
    return {error, message.str()};
}

R5900IrValidationResult validate_operand(const R5900IrOperand& operand,
                                         std::size_t index,
                                         std::uint32_t guest_pc) {
    switch (operand.kind) {
    case R5900IrOperandKind::Immediate:
        return {};
    case R5900IrOperandKind::Gpr:
        if (operand.gpr_index >= 32u) {
            return failure(R5900IrValidationError::InvalidRegister,
                           index,
                           guest_pc,
                           "invalid source GPR");
        }
        return {};
    case R5900IrOperandKind::Fpr:
        if (operand.gpr_index >= 32u) {
            return failure(R5900IrValidationError::InvalidRegister,
                           index,
                           guest_pc,
                           "invalid source FPR");
        }
        return {};
    default:
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       guest_pc,
                       "unsupported operand kind");
    }
}

R5900IrValidationResult require_destination(const R5900IrInstruction& ir,
                                            std::size_t index) {
    if (!ir.destination.has_value()) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "missing destination");
    }
    return {};
}

R5900IrValidationResult validate_gpr_destination(const R5900IrInstruction& ir,
                                                 std::size_t index,
                                                 R5900IrGprWriteMode expected_mode) {
    const auto present = require_destination(ir, index);
    if (!present.ok()) {
        return present;
    }
    if (ir.destination->kind != R5900IrDestinationKind::Gpr) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "expected GPR destination");
    }
    if (ir.destination->index >= 32u) {
        return failure(R5900IrValidationError::InvalidRegister,
                       index,
                       ir.guest_pc,
                       "invalid destination GPR");
    }
    if (ir.write_mode != expected_mode) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "invalid GPR write mode");
    }
    return {};
}

R5900IrValidationResult validate_existing_integer_write(const R5900IrInstruction& ir,
                                                        std::size_t index) {
    const auto destination = validate_gpr_destination(
        ir, index, R5900IrGprWriteMode::Low64PreserveUpper64);
    if (!destination.ok()) {
        return destination;
    }
    if (ir.inputs.size() != 2u) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "expected exactly two inputs");
    }
    for (const auto& operand : ir.inputs) {
        const auto validation = validate_operand(operand, index, ir.guest_pc);
        if (!validation.ok()) {
            return validation;
        }
        if (operand.kind == R5900IrOperandKind::Fpr) {
            return failure(R5900IrValidationError::MalformedInstruction,
                           index,
                           ir.guest_pc,
                           "FPR operand is not valid for this integer opcode");
        }
    }
    return {};
}

R5900IrValidationResult validate_and64(const R5900IrInstruction& ir,
                                       std::size_t index) {
    const auto destination = validate_gpr_destination(
        ir, index, R5900IrGprWriteMode::Low64PreserveUpper64);
    if (!destination.ok()) {
        return destination;
    }
    if (ir.inputs.size() != 2u) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "And64 expects exactly two inputs");
    }
    const auto lhs = validate_operand(ir.inputs[0], index, ir.guest_pc);
    if (!lhs.ok()) {
        return lhs;
    }
    const auto rhs = validate_operand(ir.inputs[1], index, ir.guest_pc);
    if (!rhs.ok()) {
        return rhs;
    }
    if (ir.inputs[0].kind != R5900IrOperandKind::Gpr ||
        (ir.inputs[1].kind != R5900IrOperandKind::Gpr &&
         ir.inputs[1].kind != R5900IrOperandKind::Immediate)) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "And64 expects GPR plus GPR or immediate input");
    }
    return {};
}

R5900IrValidationResult validate_lui(const R5900IrInstruction& ir,
                                     std::size_t index) {
    const auto destination = validate_gpr_destination(
        ir, index, R5900IrGprWriteMode::Low64PreserveUpper64);
    if (!destination.ok()) {
        return destination;
    }
    if (ir.inputs.size() != 1u ||
        ir.inputs[0].kind != R5900IrOperandKind::Immediate) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "LoadUpperImmediateSignExtend expects one immediate input");
    }
    return {};
}

R5900IrValidationResult validate_packed_u32_add(const R5900IrInstruction& ir,
                                                std::size_t index) {
    const auto destination = validate_gpr_destination(
        ir, index, R5900IrGprWriteMode::Full128);
    if (!destination.ok()) {
        return destination;
    }
    if (ir.inputs.size() != 2u) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "packed add expects exactly two GPR inputs");
    }
    for (const auto& operand : ir.inputs) {
        const auto validation = validate_operand(operand, index, ir.guest_pc);
        if (!validation.ok()) {
            return validation;
        }
        if (operand.kind != R5900IrOperandKind::Gpr) {
            return failure(R5900IrValidationError::MalformedInstruction,
                           index,
                           ir.guest_pc,
                           "packed add expects exactly two GPR inputs");
        }
    }
    return {};
}

bool is_hilo_destination(R5900IrDestinationKind kind) noexcept {
    switch (kind) {
    case R5900IrDestinationKind::Hi:
    case R5900IrDestinationKind::Lo:
    case R5900IrDestinationKind::Hi1:
    case R5900IrDestinationKind::Lo1:
        return true;
    default:
        return false;
    }
}

R5900IrValidationResult validate_move_gpr_low64(const R5900IrInstruction& ir,
                                                std::size_t index) {
    const auto present = require_destination(ir, index);
    if (!present.ok()) {
        return present;
    }
    if (!is_hilo_destination(ir.destination->kind) || ir.destination->index != 0u) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "MoveGprLow64 expects unindexed HI/LO/HI1/LO1 destination");
    }
    if (ir.write_mode != R5900IrGprWriteMode::None || ir.inputs.size() != 1u) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "MoveGprLow64 expects one GPR input and no GPR write mode");
    }
    const auto input = validate_operand(ir.inputs[0], index, ir.guest_pc);
    if (!input.ok()) {
        return input;
    }
    if (ir.inputs[0].kind != R5900IrOperandKind::Gpr) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "MoveGprLow64 expects one GPR input");
    }
    return {};
}

R5900IrValidationResult validate_mtsah(const R5900IrInstruction& ir,
                                       std::size_t index) {
    const auto present = require_destination(ir, index);
    if (!present.ok()) {
        return present;
    }
    if (ir.destination->kind != R5900IrDestinationKind::Sa ||
        ir.destination->index != 0u) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "ComputeMtsah expects unindexed SA destination");
    }
    if (ir.write_mode != R5900IrGprWriteMode::None || ir.inputs.size() != 2u) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "ComputeMtsah expects GPR and immediate inputs");
    }
    const auto source = validate_operand(ir.inputs[0], index, ir.guest_pc);
    if (!source.ok()) {
        return source;
    }
    if (ir.inputs[0].kind != R5900IrOperandKind::Gpr ||
        ir.inputs[1].kind != R5900IrOperandKind::Immediate) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "ComputeMtsah expects GPR and immediate inputs");
    }
    return {};
}

R5900IrValidationResult validate_move_bits32(const R5900IrInstruction& ir,
                                             std::size_t index) {
    const auto present = require_destination(ir, index);
    if (!present.ok()) {
        return present;
    }
    if (ir.write_mode != R5900IrGprWriteMode::None || ir.inputs.size() != 1u) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "MoveBits32 expects one GPR input and no GPR write mode");
    }
    const auto input = validate_operand(ir.inputs[0], index, ir.guest_pc);
    if (!input.ok()) {
        return input;
    }
    if (ir.inputs[0].kind != R5900IrOperandKind::Gpr) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "MoveBits32 expects one GPR input");
    }

    switch (ir.destination->kind) {
    case R5900IrDestinationKind::Fpr:
        if (ir.destination->index >= 32u) {
            return failure(R5900IrValidationError::InvalidRegister,
                           index,
                           ir.guest_pc,
                           "invalid destination FPR");
        }
        return {};
    case R5900IrDestinationKind::Fcr31:
        if (ir.destination->index != 0u) {
            return failure(R5900IrValidationError::MalformedInstruction,
                           index,
                           ir.guest_pc,
                           "FCR31 destination must be unindexed");
        }
        return {};
    default:
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "MoveBits32 expects FPR or FCR31 destination");
    }
}

R5900IrValidationResult validate_add_f32_accumulator(const R5900IrInstruction& ir,
                                                     std::size_t index) {
    const auto present = require_destination(ir, index);
    if (!present.ok()) {
        return present;
    }
    if (ir.destination->kind != R5900IrDestinationKind::FpAccumulator ||
        ir.destination->index != 0u) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "AddF32ToAccumulator expects unindexed FP accumulator destination");
    }
    if (ir.write_mode != R5900IrGprWriteMode::None || ir.inputs.size() != 2u) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "AddF32ToAccumulator expects two FPR inputs and no GPR write mode");
    }
    for (const auto& operand : ir.inputs) {
        const auto validation = validate_operand(operand, index, ir.guest_pc);
        if (!validation.ok()) {
            return validation;
        }
        if (operand.kind != R5900IrOperandKind::Fpr) {
            return failure(R5900IrValidationError::MalformedInstruction,
                           index,
                           ir.guest_pc,
                           "AddF32ToAccumulator expects exactly two FPR inputs");
        }
    }
    return {};
}

R5900IrValidationResult validate_store128(const R5900IrInstruction& ir,
                                          std::size_t index) {
    if (ir.destination.has_value() ||
        ir.write_mode != R5900IrGprWriteMode::None) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "Store128 must not have a destination or GPR write mode");
    }
    if (ir.inputs.size() != 3u ||
        ir.inputs[0].kind != R5900IrOperandKind::Gpr ||
        ir.inputs[1].kind != R5900IrOperandKind::Gpr ||
        ir.inputs[2].kind != R5900IrOperandKind::Immediate) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "Store128 expects base GPR, value GPR, signed immediate");
    }
    if (ir.inputs[0].gpr_index >= 32u ||
        ir.inputs[1].gpr_index >= 32u) {
        return failure(R5900IrValidationError::InvalidRegister,
                       index,
                       ir.guest_pc,
                       "Store128 GPR index out of range");
    }
    if (ir.inputs[2].immediate < -32768 ||
        ir.inputs[2].immediate > 32767) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "Store128 immediate must fit signed 16 bits");
    }
    return {};
}

R5900IrValidationResult validate_single_delay_slot(
    const R5900IrTerminator& terminator,
    std::size_t index) {
    if (terminator.delay_slot.size() != 1u) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       terminator.guest_pc,
                       "control transfer requires exactly one delay-slot instruction");
    }
    return validate_r5900_ir_instruction(terminator.delay_slot.front(), index);
}

} // namespace

R5900IrValidationResult validate_r5900_ir_instruction(
    const R5900IrInstruction& instruction,
    std::size_t instruction_index) {
    switch (instruction.opcode) {
    case R5900IrOpcode::Nop:
        if (instruction.destination.has_value() ||
            !instruction.inputs.empty() ||
            instruction.write_mode != R5900IrGprWriteMode::None) {
            return failure(R5900IrValidationError::MalformedInstruction,
                           instruction_index,
                           instruction.guest_pc,
                           "malformed Nop");
        }
        return {};

    case R5900IrOpcode::AddWordSignExtend:
    case R5900IrOpcode::Or64:
        return validate_existing_integer_write(instruction, instruction_index);

    case R5900IrOpcode::And64:
        return validate_and64(instruction, instruction_index);

    case R5900IrOpcode::LoadUpperImmediateSignExtend:
        return validate_lui(instruction, instruction_index);

    case R5900IrOpcode::AddPackedU32Saturate128:
        return validate_packed_u32_add(instruction, instruction_index);

    case R5900IrOpcode::MoveGprLow64:
        return validate_move_gpr_low64(instruction, instruction_index);

    case R5900IrOpcode::ComputeMtsah:
        return validate_mtsah(instruction, instruction_index);

    case R5900IrOpcode::MoveBits32:
        return validate_move_bits32(instruction, instruction_index);

    case R5900IrOpcode::AddF32ToAccumulator:
        return validate_add_f32_accumulator(instruction, instruction_index);

    case R5900IrOpcode::Store128:
        return validate_store128(instruction, instruction_index);

    default:
        return failure(R5900IrValidationError::UnsupportedOpcode,
                       instruction_index,
                       instruction.guest_pc,
                       "unsupported opcode");
    }
}

R5900IrValidationResult validate_r5900_ir_block(const R5900IrBlock& block) {
    for (std::size_t index = 0; index < block.body.size(); ++index) {
        const auto body_validation =
            validate_r5900_ir_instruction(block.body[index], index);
        if (!body_validation.ok()) {
            return body_validation;
        }
    }

    const auto& terminator = block.terminator;
    const auto terminator_index = block.body.size();
    if (terminator.link_gpr.has_value() &&
        terminator.kind != R5900IrTerminatorKind::IndirectCall) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       terminator_index, terminator.guest_pc,
                       "only an indirect call may name a link GPR");
    }
    if ((terminator.guest_pc & 0x3u) != 0u) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       terminator_index,
                       terminator.guest_pc,
                       "block terminator guest PC must be 4-byte aligned");
    }

    switch (terminator.kind) {
    case R5900IrTerminatorKind::Fallthrough:
        if ((terminator.fallthrough_pc & 0x3u) != 0u ||
            !terminator.inputs.empty() ||
            !terminator.delay_slot.empty() ||
            terminator.taken_pc != 0u ||
            terminator.target_pc != 0u ||
            terminator.link_pc != 0u) {
            return failure(R5900IrValidationError::MalformedInstruction,
                           terminator_index,
                           terminator.guest_pc,
                           "malformed fallthrough terminator");
        }
        return {};

    case R5900IrTerminatorKind::BranchEqual64:
        if ((terminator.taken_pc & 0x3u) != 0u ||
            (terminator.fallthrough_pc & 0x3u) != 0u ||
            terminator.target_pc != 0u ||
            terminator.link_pc != 0u ||
            terminator.inputs.size() != 2u ||
            terminator.inputs[0].kind != R5900IrOperandKind::Gpr ||
            terminator.inputs[1].kind != R5900IrOperandKind::Gpr) {
            return failure(R5900IrValidationError::MalformedInstruction,
                           terminator_index,
                           terminator.guest_pc,
                           "malformed BranchEqual64 terminator");
        }
        for (const auto& operand : terminator.inputs) {
            const auto operand_validation =
                validate_operand(operand, terminator_index, terminator.guest_pc);
            if (!operand_validation.ok()) {
                return operand_validation;
            }
        }
        return validate_single_delay_slot(terminator, terminator_index);

    case R5900IrTerminatorKind::DirectJump:
        if (!terminator.inputs.empty() ||
            terminator.taken_pc != 0u ||
            terminator.fallthrough_pc != 0u ||
            terminator.link_pc != 0u ||
            (terminator.target_pc & 0x3u) != 0u) {
            return failure(R5900IrValidationError::MalformedInstruction,
                           terminator_index,
                           terminator.guest_pc,
                           "malformed direct-jump terminator");
        }
        return validate_single_delay_slot(terminator, terminator_index);

    case R5900IrTerminatorKind::DirectCall:
        if (!terminator.inputs.empty() ||
            terminator.taken_pc != 0u ||
            terminator.fallthrough_pc != 0u ||
            (terminator.target_pc & 0x3u) != 0u ||
            (terminator.link_pc & 0x3u) != 0u ||
            terminator.link_pc !=
                static_cast<std::uint32_t>(terminator.guest_pc + 8u)) {
            return failure(R5900IrValidationError::MalformedInstruction,
                           terminator_index,
                           terminator.guest_pc,
                           "malformed direct-call terminator");
        }
        return validate_single_delay_slot(terminator, terminator_index);

    case R5900IrTerminatorKind::IndirectJump:
    case R5900IrTerminatorKind::IndirectCall: {
        const bool is_call = terminator.kind == R5900IrTerminatorKind::IndirectCall;
        if (terminator.inputs.size() != 1u ||
            terminator.inputs.front().kind != R5900IrOperandKind::Gpr ||
            terminator.taken_pc != 0u || terminator.fallthrough_pc != 0u ||
            terminator.target_pc != 0u ||
            (is_call ? (!terminator.link_gpr.has_value() ||
                        terminator.link_pc != terminator.guest_pc + 8u)
                     : terminator.link_pc != 0u)) {
            return failure(R5900IrValidationError::MalformedInstruction,
                           terminator_index, terminator.guest_pc,
                           "malformed indirect-transfer terminator");
        }
        const auto source = validate_operand(terminator.inputs.front(),
                                             terminator_index, terminator.guest_pc);
        if (!source.ok()) return source;
        if (is_call && *terminator.link_gpr >= 32u) {
            return failure(R5900IrValidationError::InvalidRegister,
                           terminator_index, terminator.guest_pc,
                           "invalid indirect-call link GPR");
        }
        const auto delay = validate_single_delay_slot(terminator, terminator_index);
        if (!delay.ok()) return delay;
        if (terminator.delay_slot.front().opcode == R5900IrOpcode::Store128) {
            return failure(R5900IrValidationError::UnsupportedOpcode,
                           terminator_index, terminator.delay_slot.front().guest_pc,
                           "SQ in an indirect-transfer delay slot is outside v0 scope");
        }
        return {};
    }

    default:
        return failure(R5900IrValidationError::UnsupportedOpcode,
                       terminator_index,
                       terminator.guest_pc,
                       "unsupported block terminator");
    }
}

} // namespace b3r::recompiler
