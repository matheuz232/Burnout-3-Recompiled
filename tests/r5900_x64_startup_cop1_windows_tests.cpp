#include "recompiler/r5900_ir_executor.h"
#include "recompiler/windows/r5900_x64_backend.h"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <vector>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_x64_startup_cop1_windows_tests: FAIL: " << message << '\n';
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

R5900IrOperand fpr(std::uint8_t index) {
    R5900IrOperand operand{};
    operand.kind = R5900IrOperandKind::Fpr;
    operand.gpr_index = index;
    return operand;
}

R5900IrInstruction make_ir(R5900IrOpcode opcode,
                           R5900IrDestination destination,
                           std::initializer_list<R5900IrOperand> inputs,
                           std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = opcode;
    ir.destination = destination;
    ir.write_mode = R5900IrGprWriteMode::None;
    ir.inputs.assign(inputs.begin(), inputs.end());
    return ir;
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

void run_differential(const std::vector<R5900IrInstruction>& program,
                      const R5900IrExecutionState& initial) {
    auto expected = initial;
    auto actual = initial;
    expect(execute_r5900_ir(program, expected).ok(),
           "reference executor must accept COP1 differential program");

    auto compiled = compile_r5900_ir_x64(program);
    if (!compiled.ok()) {
        std::cerr << "r5900_x64_startup_cop1_windows_tests: compile error: "
                  << compiled.message << '\n';
        fail("backend must compile COP1 differential program");
    }
    expect(compiled.block.has_value(), "successful compile must return native block");
    compiled.block->execute(actual);
    expect_states_equal(expected, actual);
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    {
        R5900IrExecutionState initial{};
        for (std::size_t index = 0; index < initial.gpr.size(); ++index) {
            initial.gpr[index].low64 = 0x1111000000000000ull | static_cast<std::uint64_t>(index);
            initial.gpr[index].high64 = 0xeeee000000000000ull | static_cast<std::uint64_t>(index);
        }
        initial.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
        initial.gpr[3].low64 = 0xdeadbeef00000000ull |
                               std::bit_cast<std::uint32_t>(1.5f);
        initial.gpr[4].low64 = 0xcafebabe00000000ull |
                               std::bit_cast<std::uint32_t>(2.25f);
        initial.gpr[7].low64 = 0x11223344a5a5c3c3ull;
        initial.hi = 0x1111222233334444ull;
        initial.lo = 0x5555666677778888ull;
        initial.hi1 = 0x9999aaaabbbbccccull;
        initial.lo1 = 0xddddeeeeffff0001ull;
        initial.sa = 6u;
        initial.fpr[5] = 0x7fc00001u;
        initial.fpr[6] = 0x7fc00002u;
        initial.fcr31 = 0x01020304u;
        initial.fp_acc = 0x7fc00003u;

        const std::vector<R5900IrInstruction> program = {
            make_ir(R5900IrOpcode::MoveBits32,
                    {R5900IrDestinationKind::Fpr, 5u},
                    {gpr(3)},
                    0x00105000u),
            make_ir(R5900IrOpcode::MoveBits32,
                    {R5900IrDestinationKind::Fpr, 6u},
                    {gpr(4)},
                    0x00105004u),
            make_ir(R5900IrOpcode::MoveBits32,
                    {R5900IrDestinationKind::Fcr31, 0u},
                    {gpr(7)},
                    0x00105008u),
            make_ir(R5900IrOpcode::AddF32ToAccumulator,
                    {R5900IrDestinationKind::FpAccumulator, 0u},
                    {fpr(5), fpr(6)},
                    0x0010500cu),
        };

        run_differential(program, initial);
    }

    {
        R5900IrExecutionState initial{};
        initial.gpr[0] = {1u, 2u};
        initial.fpr[9] = std::bit_cast<std::uint32_t>(+0.0f);
        initial.fpr[10] = std::bit_cast<std::uint32_t>(-0.0f);
        initial.fcr31 = 0x89abcdefu;
        initial.fp_acc = 0xffffffffu;

        const std::vector<R5900IrInstruction> program = {
            make_ir(R5900IrOpcode::AddF32ToAccumulator,
                    {R5900IrDestinationKind::FpAccumulator, 0u},
                    {fpr(9), fpr(10)},
                    0x00105010u),
        };

        run_differential(program, initial);
    }

    std::cout << "r5900_x64_startup_cop1_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
