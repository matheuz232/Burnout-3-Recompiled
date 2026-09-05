#include "r5900_direct_transfer_test_support.h"
#include "recompiler/r5900_ir_validation.h"

#include <cstdlib>
#include <iostream>

namespace {
using namespace b3r::recompiler;
using namespace b3r::test_support;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_indirect_transfer_validation_tests: FAIL: "
              << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) fail(message);
}

void expect_malformed(const R5900IrBlock& block, const char* message) {
    expect(validate_r5900_ir_block(block).error ==
               R5900IrValidationError::MalformedInstruction,
           message);
}
} // namespace

int main() {
    const auto jump = indirect_jump(0x00108000u, 5u, nop(0x00108004u));
    const auto call = indirect_call(0x00108200u, 7u, 9u, nop(0x00108204u));
    expect(validate_r5900_ir_block(jump).ok(), "valid IndirectJump must validate");
    expect(validate_r5900_ir_block(call).ok(), "valid IndirectCall must validate");

    {
        auto invalid = jump;
        invalid.terminator.inputs.clear();
        expect_malformed(invalid, "IndirectJump requires one target GPR");
    }
    {
        auto invalid = jump;
        invalid.terminator.inputs = {gpr(5u), gpr(6u)};
        expect_malformed(invalid, "IndirectJump rejects multiple target inputs");
    }
    {
        auto invalid = jump;
        invalid.terminator.inputs[0].kind = R5900IrOperandKind::Immediate;
        expect_malformed(invalid, "IndirectJump target must be a GPR");
    }
    {
        auto invalid = jump;
        invalid.terminator.inputs[0].gpr_index = 32u;
        expect(validate_r5900_ir_block(invalid).error ==
                   R5900IrValidationError::InvalidRegister,
               "IndirectJump rejects out-of-range target GPR");
    }
    {
        auto invalid = jump;
        invalid.terminator.target_pc = 0x00109000u;
        expect_malformed(invalid, "IndirectJump rejects target_pc");
    }
    {
        auto invalid = jump;
        invalid.terminator.taken_pc = 0x00109000u;
        expect_malformed(invalid, "IndirectJump rejects taken_pc");
    }
    {
        auto invalid = jump;
        invalid.terminator.fallthrough_pc = 0x00108008u;
        expect_malformed(invalid, "IndirectJump rejects fallthrough_pc");
    }
    {
        auto invalid = jump;
        invalid.terminator.link_pc = 0x00108008u;
        expect_malformed(invalid, "IndirectJump rejects link_pc");
    }
    {
        auto invalid = jump;
        invalid.terminator.link_gpr = 31u;
        expect_malformed(invalid, "IndirectJump rejects link_gpr");
    }
    {
        auto invalid = call;
        invalid.terminator.link_pc += 4u;
        expect_malformed(invalid, "IndirectCall link must equal PC+8");
    }
    {
        auto invalid = call;
        invalid.terminator.link_pc |= 2u;
        expect_malformed(invalid, "IndirectCall link must be aligned");
    }
    {
        auto invalid = call;
        invalid.terminator.link_gpr = 32u;
        expect(validate_r5900_ir_block(invalid).error ==
                   R5900IrValidationError::InvalidRegister,
               "IndirectCall rejects out-of-range link GPR");
    }
    {
        auto invalid = call;
        invalid.terminator.delay_slot.clear();
        expect_malformed(invalid, "IndirectCall requires exactly one delay instruction");
    }
    {
        auto invalid = jump;
        invalid.terminator.delay_slot.push_back(nop(0x00108008u));
        expect_malformed(invalid, "IndirectJump rejects multiple delay instructions");
    }
    {
        auto invalid = call;
        invalid.terminator.delay_slot.front().opcode =
            static_cast<R5900IrOpcode>(0xffu);
        expect(validate_r5900_ir_block(invalid).error ==
                   R5900IrValidationError::UnsupportedOpcode,
               "invalid delay opcode must propagate UnsupportedOpcode");
    }

    {
        R5900IrBlock invalid{};
        invalid.terminator.guest_pc = 0x00108400u;
        invalid.terminator.kind = R5900IrTerminatorKind::Fallthrough;
        invalid.terminator.fallthrough_pc = 0x00108404u;
        invalid.terminator.link_gpr = 1u;
        expect_malformed(invalid, "Fallthrough must reject link_gpr");
    }
    {
        R5900IrBlock invalid{};
        invalid.terminator.guest_pc = 0x00108400u;
        invalid.terminator.kind = R5900IrTerminatorKind::BranchEqual64;
        invalid.terminator.inputs = {gpr(0u), gpr(0u)};
        invalid.terminator.taken_pc = 0x00108408u;
        invalid.terminator.fallthrough_pc = 0x00108408u;
        invalid.terminator.link_gpr = 1u;
        invalid.terminator.delay_slot = {nop(0x00108404u)};
        expect_malformed(invalid, "BranchEqual64 must reject link_gpr");
    }
    {
        auto invalid = direct_jump(0x00108400u, 0x00108500u,
                                   nop(0x00108404u));
        invalid.terminator.link_gpr = 1u;
        expect_malformed(invalid, "DirectJump must reject link_gpr");
    }
    {
        auto invalid = direct_call(0x00108400u, 0x00108500u,
                                   nop(0x00108404u));
        invalid.terminator.link_gpr = 1u;
        expect_malformed(invalid, "DirectCall must reject link_gpr");
    }

    std::cout << "r5900_ir_indirect_transfer_validation_tests: PASS\n";
    return EXIT_SUCCESS;
}