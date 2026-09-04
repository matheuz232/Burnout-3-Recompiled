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

R5900IrLoweringResult discarded_gpr_zero_write(const R5900DecodedInstruction& decoded,
                                                std::uint32_t guest_pc) {
    R5900IrLoweringResult result{};
    result.instructions.push_back(base_instruction(decoded, guest_pc, R5900IrOpcode::Nop));
    return result;
}

void set_low64_destination(R5900IrInstruction& ir, std::uint8_t index) {
    ir.destination = R5900IrRegister{index};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
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
        if (decoded.rd == 0u) {
            return discarded_gpr_zero_write(decoded, guest_pc);
        }

        auto ir = base_instruction(decoded, guest_pc, R5900IrOpcode::AddWordSignExtend);
        set_low64_destination(ir, decoded.rd);
        ir.inputs.push_back(gpr(decoded.rs));
        ir.inputs.push_back(gpr(decoded.rt));
        result.instructions.push_back(ir);
        return result;
    }

    case R5900Instruction::Addiu: {
        if (decoded.rt == 0u) {
            return discarded_gpr_zero_write(decoded, guest_pc);
        }

        auto ir = base_instruction(decoded, guest_pc, R5900IrOpcode::AddWordSignExtend);
        set_low64_destination(ir, decoded.rt);
        ir.inputs.push_back(gpr(decoded.rs));
        ir.inputs.push_back(immediate(decoded.signed_immediate()));
        result.instructions.push_back(ir);
        return result;
    }

    case R5900Instruction::Ori: {
        if (decoded.rt == 0u) {
            return discarded_gpr_zero_write(decoded, guest_pc);
        }

        auto ir = base_instruction(decoded, guest_pc, R5900IrOpcode::Or64);
        set_low64_destination(ir, decoded.rt);
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
