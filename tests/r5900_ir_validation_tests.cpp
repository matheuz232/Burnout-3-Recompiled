#include "recompiler/r5900_ir_validation.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_validation_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

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

R5900IrInstruction write_ir(R5900IrOpcode opcode,
                            std::uint8_t destination,
                            R5900IrOperand lhs,
                            R5900IrOperand rhs,
                            std::uint32_t guest_pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = guest_pc;
    ir.opcode = opcode;
    ir.destination = R5900IrRegister{destination};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
    ir.inputs = {lhs, rhs};
    return ir;
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    // RED for startup-execution v0 model: these types/fields must be introduced
    // before any production semantics are added.
    R5900IrInstruction model_probe{};
    model_probe.destination = R5900IrDestination{R5900IrDestinationKind::Fpr, 3u};
    model_probe.write_mode = R5900IrGprWriteMode::Full128;
    R5900IrOperand fpr_probe{};
    fpr_probe.kind = R5900IrOperandKind::Fpr;
    fpr_probe.gpr_index = 4u;
    (void)model_probe;
    (void)fpr_probe;

    R5900IrInstruction nop{};
    nop.guest_pc = 0x00102000u;
    nop.opcode = R5900IrOpcode::Nop;
    expect(validate_r5900_ir_instruction(nop, 0).ok(), "valid Nop must validate");

    const auto valid_or64 = write_ir(
        R5900IrOpcode::Or64, 5, gpr(4), immediate(0xff00), 0x00102004u);
    expect(validate_r5900_ir_instruction(valid_or64, 1).ok(), "valid Or64 must validate");

    const auto valid_add = write_ir(
        R5900IrOpcode::AddWordSignExtend, 8, gpr(9), gpr(10), 0x00102008u);
    expect(validate_r5900_ir_instruction(valid_add, 2).ok(), "valid AddWordSignExtend must validate");

    {
        auto ir = valid_or64;
        ir.inputs[0] = gpr(32);
        expect(validate_r5900_ir_instruction(ir, 3).error == R5900IrValidationError::InvalidRegister,
               "GPR 32 source must be rejected");
    }

    {
        auto ir = valid_or64;
        ir.destination = R5900IrRegister{32};
        expect(validate_r5900_ir_instruction(ir, 4).error == R5900IrValidationError::InvalidRegister,
               "GPR 32 destination must be rejected");
    }

    {
        auto ir = valid_or64;
        ir.destination.reset();
        expect(validate_r5900_ir_instruction(ir, 5).error == R5900IrValidationError::MalformedInstruction,
               "write instruction without destination must be rejected");
    }

    {
        auto ir = valid_or64;
        ir.write_mode = R5900IrGprWriteMode::None;
        expect(validate_r5900_ir_instruction(ir, 6).error == R5900IrValidationError::MalformedInstruction,
               "write instruction with wrong write mode must be rejected");
    }

    {
        auto ir = valid_or64;
        ir.inputs.pop_back();
        expect(validate_r5900_ir_instruction(ir, 7).error == R5900IrValidationError::MalformedInstruction,
               "write instruction with wrong operand count must be rejected");
    }

    {
        auto ir = valid_or64;
        ir.inputs[0].kind = static_cast<R5900IrOperandKind>(0xff);
        expect(validate_r5900_ir_instruction(ir, 8).error == R5900IrValidationError::MalformedInstruction,
               "unknown operand kind must be rejected");
    }

    {
        auto ir = nop;
        ir.destination = R5900IrRegister{1};
        expect(validate_r5900_ir_instruction(ir, 9).error == R5900IrValidationError::MalformedInstruction,
               "Nop with destination must be rejected");
    }

    {
        auto ir = nop;
        ir.inputs = {immediate(1)};
        expect(validate_r5900_ir_instruction(ir, 10).error == R5900IrValidationError::MalformedInstruction,
               "Nop with inputs must be rejected");
    }

    {
        auto ir = nop;
        ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
        expect(validate_r5900_ir_instruction(ir, 11).error == R5900IrValidationError::MalformedInstruction,
               "Nop with write mode must be rejected");
    }

    {
        auto ir = valid_or64;
        ir.opcode = static_cast<R5900IrOpcode>(0xff);
        const auto result = validate_r5900_ir_instruction(ir, 12);
        expect(result.error == R5900IrValidationError::UnsupportedOpcode,
               "unknown opcode must be rejected");
        expect(result.message.find("IR instruction 12") != std::string::npos,
               "diagnostic must include instruction index");
        expect(result.message.find("102004") != std::string::npos,
               "diagnostic must include guest PC");
    }

    std::cout << "r5900_ir_validation_tests: PASS\n";
    return EXIT_SUCCESS;
}
