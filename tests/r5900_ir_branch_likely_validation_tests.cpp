#include "r5900_branch_likely_test_support.h"
#include "recompiler/r5900_ir_validation.h"

#include <cstdlib>
#include <iostream>

namespace {
using namespace b3r::recompiler;
using namespace b3r::test_support;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_branch_likely_validation_tests: FAIL: "
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
    const auto beql = branch_equal_likely(
        0x0010b000u, 4u, 5u, 0x0010b100u, nop(0x0010b004u));
    const auto bnel = branch_not_equal_likely(
        0x0010b200u, 6u, 7u, 0x0010b300u, nop(0x0010b204u));

    expect(validate_r5900_ir_block(beql).ok(), "valid BEQL IR must validate");
    expect(validate_r5900_ir_block(bnel).ok(), "valid BNEL IR must validate");

    {
        auto invalid = beql;
        invalid.terminator.inputs.clear();
        expect_malformed(invalid, "missing likely inputs must fail");
    }
    {
        auto invalid = bnel;
        invalid.terminator.inputs.push_back(gpr(8u));
        expect_malformed(invalid, "extra likely input must fail");
    }
    {
        auto invalid = beql;
        invalid.terminator.inputs[0].kind = R5900IrOperandKind::Immediate;
        expect_malformed(invalid, "non-GPR likely input must fail");
    }
    {
        auto invalid = bnel;
        invalid.terminator.inputs[1].gpr_index = 32u;
        expect(validate_r5900_ir_block(invalid).error ==
                   R5900IrValidationError::InvalidRegister,
               "out-of-range likely GPR must fail");
    }
    {
        auto invalid = beql;
        invalid.terminator.target_pc = 0x0010b400u;
        expect_malformed(invalid, "likely target_pc must be zero");
    }
    {
        auto invalid = bnel;
        invalid.terminator.link_pc = 0x0010b208u;
        expect_malformed(invalid, "likely link_pc must be zero");
    }
    {
        auto invalid = beql;
        invalid.terminator.link_gpr = 1u;
        expect_malformed(invalid, "likely link_gpr must be zero");
    }
    {
        auto invalid = bnel;
        invalid.terminator.taken_pc |= 2u;
        expect_malformed(invalid, "unaligned likely taken_pc must fail");
    }
    {
        auto invalid = beql;
        invalid.terminator.fallthrough_pc |= 2u;
        expect_malformed(invalid, "unaligned likely fallthrough_pc must fail");
    }
    {
        auto invalid = bnel;
        invalid.terminator.delay_slot.clear();
        expect_malformed(invalid, "missing likely delay must fail");
    }
    {
        auto invalid = beql;
        invalid.terminator.delay_slot.push_back(nop(0x0010b008u));
        expect_malformed(invalid, "multiple likely delay instructions must fail");
    }
    {
        auto invalid = bnel;
        invalid.terminator.delay_slot.front().opcode =
            static_cast<R5900IrOpcode>(0xffu);
        expect(validate_r5900_ir_block(invalid).error ==
                   R5900IrValidationError::UnsupportedOpcode,
               "invalid likely delay opcode must propagate");
    }

    std::cout << "r5900_ir_branch_likely_validation_tests: PASS\n";
    return EXIT_SUCCESS;
}
