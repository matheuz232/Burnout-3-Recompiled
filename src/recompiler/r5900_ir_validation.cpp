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

R5900IrValidationResult validate_write(const R5900IrInstruction& ir,
                                       std::size_t index) {
    if (!ir.destination.has_value()) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "missing destination");
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
    if (ir.write_mode != R5900IrGprWriteMode::Low64PreserveUpper64) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "invalid GPR write mode");
    }
    if (ir.inputs.size() != 2u) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       ir.guest_pc,
                       "expected exactly two inputs");
    }
    for (const auto& operand : ir.inputs) {
        if (operand.kind == R5900IrOperandKind::Fpr) {
            return failure(R5900IrValidationError::MalformedInstruction,
                           index,
                           ir.guest_pc,
                           "FPR operand is not valid for this integer opcode");
        }
        const auto validation = validate_operand(operand, index, ir.guest_pc);
        if (!validation.ok()) {
            return validation;
        }
    }
    return {};
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
        return validate_write(instruction, instruction_index);

    default:
        return failure(R5900IrValidationError::UnsupportedOpcode,
                       instruction_index,
                       instruction.guest_pc,
                       "unsupported opcode");
    }
}

} // namespace b3r::recompiler
