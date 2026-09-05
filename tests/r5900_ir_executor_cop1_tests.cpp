#include "recompiler/r5900_ir_executor.h"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_executor_cop1_tests: FAIL: " << message << '\n';
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
                           std::uint32_t guest_pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = guest_pc;
    ir.opcode = opcode;
    ir.destination = destination;
    ir.write_mode = R5900IrGprWriteMode::None;
    ir.inputs.assign(inputs.begin(), inputs.end());
    return ir;
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    {
        R5900IrExecutionState state{};
        state.gpr[3].low64 = 0xdeadbeef12345678ull;
        const auto mtc1 = make_ir(
            R5900IrOpcode::MoveBits32,
            {R5900IrDestinationKind::Fpr, 5u},
            {gpr(3)},
            0x00103000u);
        const auto result = execute_r5900_ir({mtc1}, state);
        expect(result.ok(), "MTC1 must execute");
        expect(state.fpr[5] == 0x12345678u,
               "MTC1 must raw-copy the source GPR low word to the FPR");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 0x11223344a5a5c3c3ull;
        const auto ctc1 = make_ir(
            R5900IrOpcode::MoveBits32,
            {R5900IrDestinationKind::Fcr31, 0u},
            {gpr(4)},
            0x00103004u);
        const auto result = execute_r5900_ir({ctc1}, state);
        expect(result.ok(), "CTC1/FCR31 must execute");
        expect(state.fcr31 == 0xa5a5c3c3u,
               "CTC1 must raw-copy the source GPR low word to FCR31");
    }

    {
        R5900IrExecutionState state{};
        state.fpr[1] = std::bit_cast<std::uint32_t>(1.5f);
        state.fpr[2] = std::bit_cast<std::uint32_t>(2.25f);
        state.fcr31 = 0x12345678u;
        const auto addas = make_ir(
            R5900IrOpcode::AddF32ToAccumulator,
            {R5900IrDestinationKind::FpAccumulator, 0u},
            {fpr(1), fpr(2)},
            0x00103008u);
        const auto result = execute_r5900_ir({addas}, state);
        expect(result.ok(), "ADDA.S must execute");
        expect(state.fp_acc == std::bit_cast<std::uint32_t>(3.75f),
               "ADDA.S must store the raw float32 sum in the FP accumulator");
        expect(state.fcr31 == 0x12345678u,
               "ADDA.S v0 must leave FCR31 unchanged");
    }

    {
        R5900IrExecutionState state{};
        state.fpr[7] = std::bit_cast<std::uint32_t>(+0.0f);
        state.fpr[8] = std::bit_cast<std::uint32_t>(-0.0f);
        const auto addas_zero = make_ir(
            R5900IrOpcode::AddF32ToAccumulator,
            {R5900IrDestinationKind::FpAccumulator, 0u},
            {fpr(7), fpr(8)},
            0x0010300cu);
        const auto result = execute_r5900_ir({addas_zero}, state);
        expect(result.ok(), "ADDA.S signed-zero case must execute");
        expect(state.fp_acc == std::bit_cast<std::uint32_t>(+0.0f),
               "+0.0f + -0.0f must produce raw +0.0f under host round-to-nearest semantics");
    }

    std::cout << "r5900_ir_executor_cop1_tests: PASS\n";
    return EXIT_SUCCESS;
}
