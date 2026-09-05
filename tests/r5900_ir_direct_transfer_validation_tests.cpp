#include "r5900_direct_transfer_test_support.h"
#include "recompiler/r5900_ir_validation.h"

#include <cstdlib>
#include <iostream>

namespace {
using namespace b3r::recompiler;
using namespace b3r::test_support;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_direct_transfer_validation_tests: FAIL: " << message << '\n';
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
    const auto jump = direct_jump(0x00106000u, 0x00106100u, nop(0x00106004u));
    const auto call = direct_call(0x00106200u, 0x00106300u, nop(0x00106204u));
    expect(validate_r5900_ir_block(jump).ok(), "valid DirectJump must validate");
    expect(validate_r5900_ir_block(call).ok(), "valid DirectCall must validate");

    {
        auto invalid = jump;
        invalid.terminator.target_pc |= 2u;
        expect_malformed(invalid, "unaligned DirectJump target must fail");
    }
    {
        auto invalid = jump;
        invalid.terminator.link_pc = 0x00106008u;
        expect_malformed(invalid, "DirectJump must reject link state");
    }
    {
        auto invalid = call;
        invalid.terminator.link_pc += 4u;
        expect_malformed(invalid, "DirectCall link must equal PC+8");
    }
    {
        auto invalid = call;
        invalid.terminator.inputs = {gpr(1u)};
        expect_malformed(invalid, "direct transfer inputs must be empty");
    }
    {
        auto invalid = jump;
        invalid.terminator.taken_pc = 0x00106080u;
        expect_malformed(invalid, "DirectJump branch fields must be empty");
    }
    {
        auto invalid = call;
        invalid.terminator.fallthrough_pc = 0x00106208u;
        expect_malformed(invalid, "DirectCall fallthrough field must be empty");
    }
    {
        auto invalid = call;
        invalid.terminator.delay_slot.clear();
        expect_malformed(invalid, "direct call requires one delay instruction");
    }
    {
        auto invalid = jump;
        invalid.terminator.delay_slot.push_back(nop(0x00106008u));
        expect_malformed(invalid, "direct jump rejects multiple delay instructions");
    }
    {
        auto invalid = call;
        invalid.terminator.delay_slot.front().opcode =
            static_cast<R5900IrOpcode>(0xffu);
        expect(validate_r5900_ir_block(invalid).error ==
                   R5900IrValidationError::UnsupportedOpcode,
               "invalid delay opcode must propagate UnsupportedOpcode");
    }

    std::cout << "r5900_ir_direct_transfer_validation_tests: PASS\n";
    return EXIT_SUCCESS;
}
