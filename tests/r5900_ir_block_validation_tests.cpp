#include "recompiler/r5900_ir_validation.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {
using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_block_validation_tests: FAIL: " << message << '\n';
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

R5900IrInstruction nop(std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::Nop;
    return ir;
}
} // namespace

int main() {
    R5900IrBlock block{};
    block.body = {nop(0x00102000u)};
    block.terminator.guest_pc = 0x00102004u;
    block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;
    block.terminator.inputs = {gpr(1u), gpr(2u)};
    block.terminator.taken_pc = 0x00102020u;
    block.terminator.fallthrough_pc = 0x0010200cu;
    block.terminator.delay_slot = {nop(0x00102008u)};
    expect(validate_r5900_ir_block(block).ok(), "valid BEQ block must validate");

    {
        auto valid = block;
        valid.terminator.inputs = {gpr(0u), gpr(0u)};
        expect(validate_r5900_ir_block(valid).ok(), "BEQ r0,r0 must validate");
    }
    {
        auto invalid = block;
        invalid.terminator.inputs = {gpr(1u)};
        expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
               "BEQ with one input must be rejected");
    }
    {
        auto invalid = block;
        invalid.terminator.inputs[1] = immediate(0);
        expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
               "BEQ immediate operand must be rejected");
    }
    {
        auto invalid = block;
        invalid.terminator.inputs[0] = gpr(32u);
        expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::InvalidRegister,
               "BEQ GPR32 must be rejected");
    }
    {
        auto invalid = block;
        invalid.terminator.taken_pc |= 2u;
        expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
               "unaligned taken target must be rejected");
    }
    {
        auto invalid = block;
        invalid.terminator.fallthrough_pc |= 2u;
        expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
               "unaligned fallthrough target must be rejected");
    }
    {
        auto invalid = block;
        invalid.terminator.delay_slot.clear();
        expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
               "missing delay slot must be rejected");
    }
    {
        auto invalid = block;
        invalid.terminator.delay_slot = {nop(0x00102008u), nop(0x0010200cu)};
        expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
               "multiple delay-slot IR instructions must be rejected");
    }
    {
        auto bad_delay = nop(0x00102008u);
        bad_delay.opcode = static_cast<R5900IrOpcode>(0xffu);
        auto invalid = block;
        invalid.terminator.delay_slot = {bad_delay};
        expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::UnsupportedOpcode,
               "unsupported delay-slot IR must be rejected");
    }

    std::cout << "r5900_ir_block_validation_tests: PASS\n";
    return EXIT_SUCCESS;
}
