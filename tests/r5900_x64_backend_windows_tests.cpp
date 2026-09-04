#include "recompiler/windows/r5900_x64_backend.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>
#include <vector>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_x64_backend_windows_tests: FAIL: " << message << '\n';
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

    static_assert(std::is_move_constructible_v<R5900X64CompiledBlock>);
    static_assert(std::is_move_assignable_v<R5900X64CompiledBlock>);
    static_assert(!std::is_copy_constructible_v<R5900X64CompiledBlock>);
    static_assert(!std::is_copy_assignable_v<R5900X64CompiledBlock>);

    {
        R5900IrExecutionState state{};
        state.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
        state.gpr[1] = {0x1122334455667788ull, 0x8877665544332211ull};

        auto compiled = compile_r5900_ir_x64({});
        expect(compiled.ok() && compiled.block.has_value(),
               "empty program must compile to a callable block");
        expect(compiled.block->valid(), "compiled block must report valid ownership");
        compiled.block->execute(state);

        expect(state.gpr[0].low64 == 0 && state.gpr[0].high64 == 0,
               "native block must normalize GPR0");
        expect(state.gpr[1].low64 == 0x1122334455667788ull &&
                   state.gpr[1].high64 == 0x8877665544332211ull,
               "empty native block must preserve nonzero GPRs");
    }

    {
        R5900IrInstruction nop{};
        nop.guest_pc = 0x00103000u;
        nop.opcode = R5900IrOpcode::Nop;

        R5900IrExecutionState state{};
        state.gpr[7] = {0x0123456789abcdefull, 0xfedcba9876543210ull};

        auto compiled = compile_r5900_ir_x64(std::vector<R5900IrInstruction>{nop});
        expect(compiled.ok() && compiled.block.has_value(), "valid Nop must compile");
        compiled.block->execute(state);
        expect(state.gpr[7].low64 == 0x0123456789abcdefull &&
                   state.gpr[7].high64 == 0xfedcba9876543210ull,
               "native Nop must preserve GPR state");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[9].low64 = 5u;
        state.gpr[10].low64 = 7u;
        state.gpr[8].high64 = 0x1111222233334444ull;

        const auto ir = write_ir(
            R5900IrOpcode::AddWordSignExtend, 8, gpr(9), gpr(10), 0x00103100u);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok() && compiled.block.has_value(), "ADDU-style IR must compile");
        compiled.block->execute(state);
        expect(state.gpr[8].low64 == 12u, "native word add must produce positive result");
        expect(state.gpr[8].high64 == 0x1111222233334444ull,
               "native word add must preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x000000007fffffffull;
        state.gpr[2].low64 = 1u;

        const auto ir = write_ir(
            R5900IrOpcode::AddWordSignExtend, 3, gpr(1), gpr(2), 0x00103104u);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok() && compiled.block.has_value(), "negative word result IR must compile");
        compiled.block->execute(state);
        expect(state.gpr[3].low64 == 0xffffffff80000000ull,
               "native word add must sign-extend negative 32-bit result");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x00000000ffffffffull;

        const auto ir = write_ir(
            R5900IrOpcode::AddWordSignExtend, 2, gpr(1), immediate(1), 0x00103108u);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok() && compiled.block.has_value(), "wrapping word add IR must compile");
        compiled.block->execute(state);
        expect(state.gpr[2].low64 == 0u, "native word add must wrap modulo 2^32");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[29].low64 = 0x1000u;
        state.gpr[29].high64 = 0xaaaabbbbccccddddull;

        const auto ir = write_ir(
            R5900IrOpcode::AddWordSignExtend, 29, gpr(29), immediate(-16), 0x0010310cu);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok() && compiled.block.has_value(), "ADDIU-style aliasing IR must compile");
        compiled.block->execute(state);
        expect(state.gpr[29].low64 == 0x0ff0u,
               "native ADDIU-style add must support source/destination aliasing");
        expect(state.gpr[29].high64 == 0xaaaabbbbccccddddull,
               "native ADDIU-style add must preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 0x1234567800000000ull;
        state.gpr[5].high64 = 0x5555666677778888ull;

        const auto ir = write_ir(
            R5900IrOpcode::Or64, 5, gpr(4), immediate(0xff00), 0x00103200u);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok() && compiled.block.has_value(), "ORI-style IR must compile");
        compiled.block->execute(state);
        expect(state.gpr[5].low64 == 0x123456780000ff00ull,
               "native Or64 must OR across full low64");
        expect(state.gpr[5].high64 == 0x5555666677778888ull,
               "native Or64 must preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[6].low64 = 0x00000000000000ffull;
        state.gpr[6].high64 = 0x123456789abcdef0ull;

        const auto ir = write_ir(
            R5900IrOpcode::Or64,
            6,
            gpr(6),
            immediate(static_cast<std::int64_t>(0x8000000100000000ull)),
            0x00103204u);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok() && compiled.block.has_value(), "full-imm64 Or64 IR must compile");
        compiled.block->execute(state);
        expect(state.gpr[6].low64 == 0x80000001000000ffull,
               "native Or64 must preserve full 64-bit immediate bit pattern");
        expect(state.gpr[6].high64 == 0x123456789abcdef0ull,
               "aliased native Or64 must preserve high64");
    }

    std::cout << "r5900_x64_backend_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
