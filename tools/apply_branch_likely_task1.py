from pathlib import Path

validator = Path("src/recompiler/r5900_ir_validation.cpp")
text = validator.read_text(encoding="utf-8")
needle = """        return validate_single_delay_slot(terminator, terminator_index);\n\n    case R5900IrTerminatorKind::DirectJump:\n"""
insert = """        return validate_single_delay_slot(terminator, terminator_index);\n\n    case R5900IrTerminatorKind::BranchEqualLikely64:\n    case R5900IrTerminatorKind::BranchNotEqualLikely64:\n        if ((terminator.taken_pc & 0x3u) != 0u ||\n            (terminator.fallthrough_pc & 0x3u) != 0u ||\n            terminator.target_pc != 0u ||\n            terminator.link_pc != 0u ||\n            terminator.link_gpr != 0u ||\n            terminator.inputs.size() != 2u ||\n            terminator.inputs[0].kind != R5900IrOperandKind::Gpr ||\n            terminator.inputs[1].kind != R5900IrOperandKind::Gpr) {\n            return failure(R5900IrValidationError::MalformedInstruction,\n                           terminator_index,\n                           terminator.guest_pc,\n                           \"malformed likely-branch terminator\");\n        }\n        for (const auto& operand : terminator.inputs) {\n            const auto operand_validation =\n                validate_operand(operand, terminator_index, terminator.guest_pc);\n            if (!operand_validation.ok()) {\n                return operand_validation;\n            }\n        }\n        return validate_single_delay_slot(terminator, terminator_index);\n\n    case R5900IrTerminatorKind::DirectJump:\n"""
if needle not in text:
    raise SystemExit("validator insertion anchor not found")
validator.write_text(text.replace(needle, insert, 1), encoding="utf-8")

support = Path("tests/r5900_branch_likely_test_support.h")
support.write_text(r'''#pragma once

#include "r5900_direct_transfer_test_support.h"

namespace b3r::test_support {

inline R5900IrBlock branch_equal_likely(std::uint32_t pc,
                                        std::uint8_t rs,
                                        std::uint8_t rt,
                                        std::uint32_t target,
                                        R5900IrInstruction delay) {
    R5900IrBlock block{};
    block.terminator.guest_pc = pc;
    block.terminator.kind = R5900IrTerminatorKind::BranchEqualLikely64;
    block.terminator.inputs = {gpr(rs), gpr(rt)};
    block.terminator.taken_pc = target;
    block.terminator.fallthrough_pc = pc + 8u;
    block.terminator.delay_slot = {delay};
    return block;
}

inline R5900IrBlock branch_not_equal_likely(std::uint32_t pc,
                                            std::uint8_t rs,
                                            std::uint8_t rt,
                                            std::uint32_t target,
                                            R5900IrInstruction delay) {
    auto block = branch_equal_likely(pc, rs, rt, target, delay);
    block.terminator.kind = R5900IrTerminatorKind::BranchNotEqualLikely64;
    return block;
}

} // namespace b3r::test_support
''', encoding="utf-8")

validation_test = Path("tests/r5900_ir_branch_likely_validation_tests.cpp")
validation_test.write_text(r'''#include "r5900_branch_likely_test_support.h"
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
''', encoding="utf-8")

cmake = Path("CMakeLists.txt")
text = cmake.read_text(encoding="utf-8")
anchor = "  add_executable(r5900_ir_direct_transfer_executor_tests\n"
addition = """  add_executable(r5900_ir_branch_likely_validation_tests\n    tests/r5900_ir_branch_likely_validation_tests.cpp\n  )\n  target_link_libraries(r5900_ir_branch_likely_validation_tests PRIVATE b3r_recompiler)\n  add_test(NAME r5900_ir_branch_likely_validation_tests\n    COMMAND r5900_ir_branch_likely_validation_tests)\n\n"""
if anchor not in text:
    raise SystemExit("CMake insertion anchor not found")
cmake.write_text(text.replace(anchor, addition + anchor, 1), encoding="utf-8")

red_test = Path("tests/r5900_ir_direct_transfer_validation_tests.cpp")
text = red_test.read_text(encoding="utf-8")
start_marker = "    // Task 1 RED: branch-likely terminators intentionally do not exist yet.\n"
end_marker = '    std::cout << "r5900_ir_direct_transfer_validation_tests: PASS\\n";'
if start_marker not in text or end_marker not in text:
    raise SystemExit("temporary RED block anchors not found")
start = text.index(start_marker)
end = text.index(end_marker, start)
red_test.write_text(text[:start] + text[end:], encoding="utf-8")
