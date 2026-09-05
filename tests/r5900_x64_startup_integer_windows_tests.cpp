#include "recompiler/r5900_ir_executor.h"
#include "recompiler/windows/r5900_x64_backend.h"

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <vector>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_x64_startup_integer_windows_tests: FAIL: " << message << '\n';
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

R5900IrInstruction make_ir(R5900IrOpcode opcode,
                           R5900IrDestination destination,
                           R5900IrGprWriteMode mode,
                           std::initializer_list<R5900IrOperand> inputs,
                           std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = opcode;
    ir.destination = destination;
    ir.write_mode = mode;
    ir.inputs.assign(inputs.begin(), inputs.end());
    return ir;
}

R5900IrInstruction addiu(std::uint8_t rt,
                         std::uint8_t rs,
                         std::int16_t imm,
                         std::uint32_t pc) {
    return make_ir(
        R5900IrOpcode::AddWordSignExtend,
        {R5900IrDestinationKind::Gpr, rt},
        R5900IrGprWriteMode::Low64PreserveUpper64,
        {gpr(rs), immediate(imm)},
        pc);
}

R5900IrBlock beq_block(std::uint32_t pc,
                       std::uint8_t rs,
                       std::uint8_t rt,
                       std::uint32_t taken_pc,
                       std::uint32_t fallthrough_pc,
                       R5900IrInstruction delay) {
    R5900IrBlock block{};
    block.terminator.guest_pc = pc;
    block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;
    block.terminator.inputs = {gpr(rs), gpr(rt)};
    block.terminator.taken_pc = taken_pc;
    block.terminator.fallthrough_pc = fallthrough_pc;
    block.terminator.delay_slot = {delay};
    return block;
}

R5900IrGprValue packed_u32(std::uint32_t lane0,
                           std::uint32_t lane1,
                           std::uint32_t lane2,
                           std::uint32_t lane3) {
    return {
        static_cast<std::uint64_t>(lane0) |
            (static_cast<std::uint64_t>(lane1) << 32u),
        static_cast<std::uint64_t>(lane2) |
            (static_cast<std::uint64_t>(lane3) << 32u),
    };
}

void expect_states_equal(const R5900IrExecutionState& expected,
                         const R5900IrExecutionState& actual) {
    for (std::size_t index = 0; index < expected.gpr.size(); ++index) {
        expect(expected.gpr[index].low64 == actual.gpr[index].low64,
               "GPR low64 mismatch");
        expect(expected.gpr[index].high64 == actual.gpr[index].high64,
               "GPR high64 mismatch");
    }
    expect(expected.hi == actual.hi, "HI mismatch");
    expect(expected.lo == actual.lo, "LO mismatch");
    expect(expected.hi1 == actual.hi1, "HI1 mismatch");
    expect(expected.lo1 == actual.lo1, "LO1 mismatch");
    expect(expected.sa == actual.sa, "SA mismatch");
    for (std::size_t index = 0; index < expected.fpr.size(); ++index) {
        expect(expected.fpr[index] == actual.fpr[index], "FPR mismatch");
    }
    expect(expected.fcr31 == actual.fcr31, "FCR31 mismatch");
    expect(expected.fp_acc == actual.fp_acc, "FP accumulator mismatch");
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    R5900IrExecutionState initial{};
    for (std::size_t index = 0; index < initial.gpr.size(); ++index) {
        initial.gpr[index].low64 = 0x0101010101010101ull * static_cast<std::uint64_t>(index + 1u);
        initial.gpr[index].high64 = 0xf000000000000000ull | static_cast<std::uint64_t>(index);
    }
    initial.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
    initial.gpr[1].low64 = 0xabcdef12345678f3ull;
    initial.gpr[4].low64 = 0x1111222233334444ull;
    initial.gpr[5].low64 = 0x5555666677778888ull;
    initial.gpr[6].low64 = 0x9999aaaabbbbccccull;
    initial.gpr[7].low64 = 0xddddeeeeffff0001ull;
    initial.gpr[8].low64 = 5u;
    initial.gpr[9] = packed_u32(0xffffffffu, 1u, 0xfffffffeu, 0x80000000u);
    initial.gpr[10] = packed_u32(1u, 2u, 2u, 0x80000000u);
    initial.hi = 0x100u;
    initial.lo = 0x200u;
    initial.hi1 = 0x300u;
    initial.lo1 = 0x400u;
    initial.sa = 0x500u;
    initial.fpr[3] = 0x12345678u;
    initial.fcr31 = 0xa5a5c3c3u;
    initial.fp_acc = 0x3f800000u;

    const std::vector<R5900IrInstruction> program = {
        make_ir(R5900IrOpcode::And64,
                {R5900IrDestinationKind::Gpr, 2u},
                R5900IrGprWriteMode::Low64PreserveUpper64,
                {gpr(1), immediate(0x00f0)},
                0x00104000u),
        make_ir(R5900IrOpcode::LoadUpperImmediateSignExtend,
                {R5900IrDestinationKind::Gpr, 3u},
                R5900IrGprWriteMode::Low64PreserveUpper64,
                {immediate(0x8040)},
                0x00104004u),
        make_ir(R5900IrOpcode::MoveGprLow64,
                {R5900IrDestinationKind::Hi, 0u},
                R5900IrGprWriteMode::None,
                {gpr(4)},
                0x00104008u),
        make_ir(R5900IrOpcode::MoveGprLow64,
                {R5900IrDestinationKind::Lo, 0u},
                R5900IrGprWriteMode::None,
                {gpr(5)},
                0x0010400cu),
        make_ir(R5900IrOpcode::MoveGprLow64,
                {R5900IrDestinationKind::Hi1, 0u},
                R5900IrGprWriteMode::None,
                {gpr(6)},
                0x00104010u),
        make_ir(R5900IrOpcode::MoveGprLow64,
                {R5900IrDestinationKind::Lo1, 0u},
                R5900IrGprWriteMode::None,
                {gpr(7)},
                0x00104014u),
        make_ir(R5900IrOpcode::ComputeMtsah,
                {R5900IrDestinationKind::Sa, 0u},
                R5900IrGprWriteMode::None,
                {gpr(8), immediate(3)},
                0x00104018u),
        make_ir(R5900IrOpcode::AddPackedU32Saturate128,
                {R5900IrDestinationKind::Gpr, 11u},
                R5900IrGprWriteMode::Full128,
                {gpr(9), gpr(10)},
                0x0010401cu),
        make_ir(R5900IrOpcode::And64,
                {R5900IrDestinationKind::Gpr, 12u},
                R5900IrGprWriteMode::Low64PreserveUpper64,
                {gpr(4), gpr(5)},
                0x00104020u),
    };

    auto expected = initial;
    auto actual = initial;
    expect(execute_r5900_ir(program, expected).ok(),
           "reference executor must accept startup integer program");

    auto compiled = compile_r5900_ir_x64(program);
    if (!compiled.ok()) {
        std::cerr << "r5900_x64_startup_integer_windows_tests: compile error: "
                  << compiled.message << '\n';
        fail("backend must compile startup integer differential program");
    }
    expect(compiled.block.has_value(), "successful compile must return a native block");
    const auto next_pc = compiled.block->execute(actual);
    expect(next_pc == program.back().guest_pc + 4u,
           "vector x64 compile must return sequential fallthrough PC");
    expect_states_equal(expected, actual);

    {
        R5900IrExecutionState beq_initial{};
        beq_initial.gpr[1] = {5u, 0x1111111111111111ull};
        beq_initial.gpr[2] = {5u, 0x2222222222222222ull};
        const auto block = beq_block(
            0x00104100u,
            1u,
            2u,
            0x00104140u,
            0x00104108u,
            addiu(1u, 1u, 1, 0x00104104u));

        auto expected_state = beq_initial;
        auto actual_state = beq_initial;
        const auto expected_result = execute_r5900_ir_block(block, expected_state);
        expect(expected_result.ok(), "reference BEQ block must execute");

        auto native = compile_r5900_ir_x64(block);
        expect(native.ok() && native.block.has_value(),
               "x64 backend must compile BEQ block");
        const auto native_next_pc = native.block->execute(actual_state);
        expect(native_next_pc == expected_result.next_pc,
               "native BEQ must return reference next_pc");
        expect_states_equal(expected_state, actual_state);
    }

    std::cout << "r5900_x64_startup_integer_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
