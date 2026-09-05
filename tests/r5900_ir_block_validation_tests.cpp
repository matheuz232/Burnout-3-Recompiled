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

R5900IrBlock direct_jump(std::uint32_t pc,
                         std::uint32_t target,
                         R5900IrInstruction delay) {
    R5900IrBlock block{};
    block.terminator.guest_pc = pc;
    block.terminator.kind = R5900IrTerminatorKind::DirectJump;
    block.terminator.target_pc = target;
    block.terminator.delay_slot = {delay};
    return block;
}

R5900IrBlock direct_call(std::uint32_t pc,
                         std::uint32_t target,
                         R5900IrInstruction delay) {
    auto block = direct_jump(pc, target, delay);
    block.terminator.kind = R5900IrTerminatorKind::DirectCall;
    block.terminator.link_pc = pc + 8u;
    return block;
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

    const auto jump = direct_jump(0x00106000u,
                                  0x00106100u,
                                  nop(0x00106004u));
    expect(validate_r5900_ir_block(jump).ok(),
           "valid DirectJump must validate");

    const auto call = direct_call(0x00106200u,
                                  0x00106300u,
                                  nop(0x00106204u));
    expect(validate_r5900_ir_block(call).ok(),
           "valid DirectCall must validate");

    {
        auto invalid = jump;
        invalid.terminator.target_pc |= 2u;
        expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
               "unaligned DirectJump target must fail");
    }
    {
        auto invalid = jump;
        invalid.terminator.link_pc = 0x00106008u;
        expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
               "DirectJump link state must fail");
    }
    {
        auto invalid = call;
        invalid.terminator.link_pc += 4u;
        expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
               "DirectCall link must equal guest PC plus eight");
    }
    {
        auto invalid = call;
        invalid.terminator.inputs = {gpr(1u)};
        expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
               "DirectCall inputs must be empty");
    }
    {
        auto invalid = jump;
        invalid.terminator.taken_pc = 0x00106080u;
        expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
               "DirectJump branch fields must be empty");
    }
    {
        auto invalid = call;
        invalid.terminator.fallthrough_pc = 0x00106208u;
        expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
               "DirectCall fallthrough state must be empty");
    }
    {
        auto invalid = call;
        invalid.terminator.delay_slot.clear();
        expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
               "DirectCall requires one delay slot");
    }
    {
        auto invalid = jump;
        invalid.terminator.delay_slot.push_back(nop(0x00106008u));
        expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
               "DirectJump rejects multiple delay-slot IR instructions");
    }
    {
        auto invalid = call;
        invalid.terminator.delay_slot.front().opcode =
            static_cast<R5900IrOpcode>(0xffu);
        expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::UnsupportedOpcode,
               "invalid direct-transfer delay IR must propagate validation error");
    }

    std::cout << "r5900_ir_block_validation_tests: PASS\n";
    return EXIT_SUCCESS;
}
