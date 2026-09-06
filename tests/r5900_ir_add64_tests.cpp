#include "recompiler/r5900_decoder.h"
#include "recompiler/r5900_ir.h"
#include "recompiler/r5900_ir_validation.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_add64_tests: FAIL: " << message << '\n';
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

constexpr std::uint32_t r_type(std::uint8_t rs,
                               std::uint8_t rt,
                               std::uint8_t rd,
                               std::uint8_t funct) {
    return (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           funct;
}

R5900IrInstruction valid_add64() {
    R5900IrInstruction ir{};
    ir.guest_pc = 0x00114edcu;
    ir.guest_raw = r_type(29u, 0u, 4u, 0x2du);
    ir.opcode = R5900IrOpcode::Add64;
    ir.destination = R5900IrDestination{4u};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
    ir.inputs = {gpr(29u), gpr(0u)};
    return ir;
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    const auto raw = r_type(29u, 0u, 4u, 0x2du);
    const auto lowered = lower_r5900_instruction(decode_r5900(raw), 0x00114edcu);
    expect(lowered.ok() && lowered.instructions.size() == 1u,
           "DADDU must lower to one IR instruction");

    const auto& ir = lowered.instructions[0];
    expect(ir.opcode == R5900IrOpcode::Add64,
           "DADDU must lower to Add64");
    expect(ir.destination.has_value() &&
               ir.destination->kind == R5900IrDestinationKind::Gpr &&
               ir.destination->index == 4u &&
               ir.write_mode == R5900IrGprWriteMode::Low64PreserveUpper64,
           "DADDU destination contract mismatch");
    expect(ir.inputs.size() == 2u &&
               ir.inputs[0].kind == R5900IrOperandKind::Gpr &&
               ir.inputs[0].gpr_index == 29u &&
               ir.inputs[1].kind == R5900IrOperandKind::Gpr &&
               ir.inputs[1].gpr_index == 0u,
           "DADDU inputs must be rs then rt");
    expect(ir.guest_pc == 0x00114edcu && ir.guest_raw == raw,
           "DADDU provenance must be preserved");
    expect(validate_r5900_ir_instruction(ir, 0u).ok(),
           "lowered Add64 must validate");

    const auto zero = lower_r5900_instruction(
        decode_r5900(r_type(7u, 8u, 0u, 0x2du)), 0x00120000u);
    expect(zero.ok() && zero.instructions.size() == 1u &&
               zero.instructions[0].opcode == R5900IrOpcode::Nop &&
               zero.instructions[0].guest_pc == 0x00120000u,
           "DADDU to r0 must lower to provenance Nop");

    {
        auto bad = valid_add64();
        bad.destination.reset();
        expect(!validate_r5900_ir_instruction(bad, 0u).ok(),
               "Add64 requires a destination");
    }
    {
        auto bad = valid_add64();
        bad.destination = R5900IrDestination{R5900IrDestinationKind::Hi, 0u};
        expect(!validate_r5900_ir_instruction(bad, 0u).ok(),
               "Add64 requires a GPR destination");
    }
    {
        auto bad = valid_add64();
        bad.destination = R5900IrDestination{32u};
        expect(validate_r5900_ir_instruction(bad, 0u).error ==
                   R5900IrValidationError::InvalidRegister,
               "Add64 destination register range must be validated");
    }
    {
        auto bad = valid_add64();
        bad.write_mode = R5900IrGprWriteMode::None;
        expect(!validate_r5900_ir_instruction(bad, 0u).ok(),
               "Add64 requires Low64PreserveUpper64");
    }
    {
        auto bad = valid_add64();
        bad.inputs = {gpr(29u)};
        expect(!validate_r5900_ir_instruction(bad, 0u).ok(),
               "Add64 requires two inputs");
    }
    {
        auto bad = valid_add64();
        bad.inputs[1] = immediate(1);
        expect(!validate_r5900_ir_instruction(bad, 0u).ok(),
               "Add64 rejects immediate inputs");
    }
    {
        auto bad = valid_add64();
        bad.inputs[0].kind = R5900IrOperandKind::Fpr;
        expect(!validate_r5900_ir_instruction(bad, 0u).ok(),
               "Add64 rejects FPR inputs");
    }
    {
        auto bad = valid_add64();
        bad.inputs[0] = gpr(32u);
        expect(validate_r5900_ir_instruction(bad, 0u).error ==
                   R5900IrValidationError::InvalidRegister,
               "Add64 source register range must be validated");
    }

    std::cout << "r5900_ir_add64_tests: PASS\n";
    return EXIT_SUCCESS;
}
