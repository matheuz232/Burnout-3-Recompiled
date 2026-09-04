#include "recompiler/r5900_ir_executor.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_executor_tests: FAIL: " << message << '\n';
    std::exit(EXIT_SUCCESS == EXIT_FAILURE ? EXIT_SUCCESS : EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

R5900IrOperand gpr(std::uint8_t index) {
    R5900IrOperand value{};
    value.kind = R5900IrOperandKind::Gpr;
    value.gpr_index = index;
    return value;
}

R5900IrOperand immediate(std::int64_t value) {
    R5900IrOperand operand{};
    operand.kind = R5900IrOperandKind::Immediate;
    operand.immediate = value;
    return operand;
}

R5900IrInstruction write_ir(R5900IrOpcode opcode,
                            std::uint8_t destination,
                            R5900IrOperand lhs,
                            R5900IrOperand rhs,
                            std::uint32_t guest_pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = guest_pc;
    ir.opcode = opcode;
    ir.destination = R5900IrRegister{destination};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
    ir.inputs = {lhs, rhs};
    return ir;
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x1122334455667788ull;
        state.gpr[1].high64 = 0x8877665544332211ull;

        R5900IrInstruction nop{};
        nop.guest_pc = 0x00100000u;
        nop.opcode = R5900IrOpcode::Nop;

        const auto result = execute_r5900_ir(std::vector<R5900IrInstruction>{nop}, state);
        expect(result.ok(), "valid Nop must execute successfully");
        expect(state.gpr[1].low64 == 0x1122334455667788ull, "Nop must preserve low64");
        expect(state.gpr[1].high64 == 0x8877665544332211ull, "Nop must preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[9].low64 = 0x000000007fffffffull;
        state.gpr[10].low64 = 1u;
        state.gpr[8].high64 = 0x0123456789abcdefull;

        const auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 8, gpr(9), gpr(10), 0x00100010u);
        const auto result = execute_r5900_ir({ir}, state);
        expect(result.ok(), "word add must execute");
        expect(state.gpr[8].low64 == 0xffffffff80000000ull, "negative word result must sign-extend to 64 bits");
        expect(state.gpr[8].high64 == 0x0123456789abcdefull, "word add must preserve destination high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x00000000ffffffffull;
        const auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 2, gpr(1), immediate(1), 0x00100014u);
        const auto result = execute_r5900_ir({ir}, state);
        expect(result.ok(), "wrapping word add must execute");
        expect(state.gpr[2].low64 == 0u, "word add must wrap modulo 2^32");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[29].low64 = 0x0000000000001000ull;
        state.gpr[29].high64 = 0xfeedfacecafebeefull;
        const auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 29, gpr(29), immediate(-16), 0x00100018u);
        const auto result = execute_r5900_ir({ir}, state);
        expect(result.ok(), "ADDIU-style immediate add must execute");
        expect(state.gpr[29].low64 == 0x0000000000000ff0ull, "signed immediate must contribute its low 32 bits to word addition");
        expect(state.gpr[29].high64 == 0xfeedfacecafebeefull, "ADDIU-style write must preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 0x1234567800000000ull;
        state.gpr[5].high64 = 0xaabbccddeeff0011ull;
        const auto ir = write_ir(R5900IrOpcode::Or64, 5, gpr(4), immediate(0xff00), 0x0010001cu);
        const auto result = execute_r5900_ir({ir}, state);
        expect(result.ok(), "ORI-style OR must execute");
        expect(state.gpr[5].low64 == 0x123456780000ff00ull, "Or64 must operate across the full low 64-bit register value");
        expect(state.gpr[5].high64 == 0xaabbccddedeff0011ull, "Or64 must preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[0].low64 = 0xffffffffffffffffull;
        state.gpr[0].high64 = 0xffffffffffffffffull;
        const auto ir = write_ir(R5900IrOpcode::Or64, 0, immediate(1), immediate(2), 0x00100020u);
        const auto result = execute_r5900_ir({ir}, state);
        expect(result.ok(), "write to GPR zero must be accepted as a discarded architectural write");
        expect(state.gpr[0].low64 == 0u && state.gpr[0].high64 == 0u, "GPR zero must remain immutable zero");
    }

    std::cout << "r5900_ir_executor_tests: PASS\n";
    return EXIT_SUCCESS;
}
