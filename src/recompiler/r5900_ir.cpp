#include "recompiler/r5900_ir.h"

#include <string>

namespace b3r::recompiler {
namespace {

R5900IrOperand gpr(std::uint8_t index) {
    R5900IrOperand operand{};
    operand.kind = R5900IrOperandKind::Gpr;
    operand.gpr_index = index;
    return operand;
}

R5900IrOperand immediate(std::int64_t value) {
    R5900IrOperand operand{};
    operand.kind = R5900IrOperandKind::Immediate;
    operand.immediate = value;
    return operand;
}

R5900IrInstruction base_instruction(const R5900DecodedInstruction& decoded,
                                    std::uint32_t guest_pc,
                                    R5900IrOpcode opcode) {
    R5900IrInstruction ir{};
    ir.guest_pc = guest_pc;
    ir.guest_raw = decoded.raw;
    ir.opcode = opcode;
    return ir;
}

} // namespace

R5900IrLoweringResult
lower_r5900_instruction(const R5900DecodedInstruction& decoded, std::uint32_t guest_pc) {
    R5900IrLoweringResult result{};

    switch (decoded.instruction) {
    case R5900Instruction::Nop: {
        result.instructions.push_back(base_instruction(decoded, guest_pc, R5900IrOpcode::Nop));
        return result;
    }

    case R5900Instruction::Addu: {
        auto ir = base_instruction(decoded, guest_pc, R5900IrOpcode::AddWordSignExtend);
        ir.destination = R5900IrRegister{decoded.rd};
        ir.inputs.push_back(gpr(decoded.rs));
        ir.inputs.push_back(gpr(decoded.rt));
        result.instructions.push_back(ir);
        return result;
    }

    case R5900Instruction::Addiu: {
        auto ir = base_instruction(decoded, guest_pc, R5900IrOpcode::AddWordSignExtend);
        ir.destination = R5900IrRegister{decoded.rt};
        ir.inputs.push_back(gpr(decoded.rs));
        ir.inputs.push_back(immediate(decoded.signed_immediate()));
        result.instructions.push_back(ir);
        return result;
    }

    case R5900Instruction::Ori: {
        auto ir = base_instruction(decoded, guest_pc, R5900IrOpcode::Or64);
        ir.destination = R5900IrRegister{decoded.rt};
        ir.inputs.push_back(gpr(decoded.rs));
        ir.inputs.push_back(immediate(static_cast<std::int64_t>(decoded.immediate)));
        result.instructions.push_back(ir);
        return result;
    }

    default:
        result.error = R5900IrLoweringError::UnsupportedInstruction;
        result.message = std::string("unsupported R5900 IR lowering: ") + r5900_instruction_name(decoded.instruction);
        return result;
    }
}

} // namespace b3r::recompiler
