#include "recompiler/r5900_decoder.h"
#include "recompiler/windows/r5900_x64_backend.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
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

void expect_states_equal(const R5900IrExecutionState& expected,
                         const R5900IrExecutionState& actual,
                         const char* message) {
    for (std::size_t index = 0; index < expected.gpr.size(); ++index) {
        if (expected.gpr[index].low64 != actual.gpr[index].low64 ||
            expected.gpr[index].high64 != actual.gpr[index].high64) {
            fail(message);
        }
    }
}

constexpr std::uint32_t r_type(std::uint8_t rs,
                               std::uint8_t rt,
                               std::uint8_t rd,
                               std::uint8_t sa,
                               std::uint8_t funct) {
    return (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) |
           funct;
}

constexpr std::uint32_t i_type(std::uint8_t op,
                               std::uint8_t rs,
                               std::uint8_t rt,
                               std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           imm;
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
        expect(compiled.ok() && compiled.block.has_value(), "empty program must compile");
        compiled.block->execute(state);
        expect(state.gpr[0].low64 == 0 && state.gpr[0].high64 == 0,
               "native block must normalize GPR0");
        expect(state.gpr[1].low64 == 0x1122334455667788ull &&
                   state.gpr[1].high64 == 0x8877665544332211ull,
               "empty block must preserve other GPRs");
    }

    {
        R5900IrInstruction nop{};
        nop.guest_pc = 0x00103000u;
        nop.opcode = R5900IrOpcode::Nop;
        R5900IrExecutionState state{};
        state.gpr[7] = {0x0123456789abcdefull, 0xfedcba9876543210ull};
        auto compiled = compile_r5900_ir_x64({nop});
        expect(compiled.ok(), "Nop must compile");
        compiled.block->execute(state);
        expect(state.gpr[7].low64 == 0x0123456789abcdefull &&
                   state.gpr[7].high64 == 0xfedcba9876543210ull,
               "Nop must preserve GPR state");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[9].low64 = 5u;
        state.gpr[10].low64 = 7u;
        state.gpr[8].high64 = 0x1111222233334444ull;
        const auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 8, gpr(9), gpr(10), 0x00103100u);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok(), "ADDU-style IR must compile");
        compiled.block->execute(state);
        expect(state.gpr[8].low64 == 12u, "native word add must produce positive result");
        expect(state.gpr[8].high64 == 0x1111222233334444ull, "word add must preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x7fffffffull;
        state.gpr[2].low64 = 1u;
        const auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 3, gpr(1), gpr(2), 0x00103104u);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok(), "negative word result must compile");
        compiled.block->execute(state);
        expect(state.gpr[3].low64 == 0xffffffff80000000ull,
               "word add must sign-extend negative result");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0xffffffffull;
        const auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 2, gpr(1), immediate(1), 0x00103108u);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok(), "wrapping word add must compile");
        compiled.block->execute(state);
        expect(state.gpr[2].low64 == 0u, "word add must wrap modulo 2^32");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[29] = {0x1000u, 0xaaaabbbbccccddddull};
        const auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 29, gpr(29), immediate(-16), 0x0010310cu);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok(), "ADDIU-style aliasing must compile");
        compiled.block->execute(state);
        expect(state.gpr[29].low64 == 0x0ff0u, "ADDIU-style aliasing must execute");
        expect(state.gpr[29].high64 == 0xaaaabbbbccccddddull, "ADDIU must preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 0x1234567800000000ull;
        state.gpr[5].high64 = 0x5555666677778888ull;
        const auto ir = write_ir(R5900IrOpcode::Or64, 5, gpr(4), immediate(0xff00), 0x00103200u);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok(), "ORI-style IR must compile");
        compiled.block->execute(state);
        expect(state.gpr[5].low64 == 0x123456780000ff00ull, "Or64 must operate on full low64");
        expect(state.gpr[5].high64 == 0x5555666677778888ull, "Or64 must preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[6] = {0xffu, 0x123456789abcdef0ull};
        const auto ir = write_ir(
            R5900IrOpcode::Or64,
            6,
            gpr(6),
            immediate(static_cast<std::int64_t>(0x8000000100000000ull)),
            0x00103204u);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok(), "full imm64 Or64 must compile");
        compiled.block->execute(state);
        expect(state.gpr[6].low64 == 0x80000001000000ffull,
               "Or64 must preserve full immediate bit pattern");
        expect(state.gpr[6].high64 == 0x123456789abcdef0ull, "aliased Or64 must preserve high64");
    }

    {
        auto compiled = compile_r5900_ir_x64({});
        expect(compiled.ok(), "move-ownership source must compile");
        R5900X64CompiledBlock moved(std::move(*compiled.block));
        expect(moved.valid(), "move construction must transfer executable ownership");
        expect(!compiled.block->valid(), "move construction must invalidate source block");
        R5900X64CompiledBlock assigned;
        assigned = std::move(moved);
        expect(assigned.valid(), "move assignment must transfer executable ownership");
        expect(!moved.valid(), "move assignment must invalidate source block");
        R5900IrExecutionState state{};
        state.gpr[0] = {1u, 2u};
        assigned.execute(state);
        expect(state.gpr[0].low64 == 0u && state.gpr[0].high64 == 0u,
               "moved compiled block must remain callable");
    }

    {
        const auto valid = write_ir(R5900IrOpcode::Or64, 1, immediate(1), immediate(2), 0x00103300u);
        auto bad_source = valid;
        bad_source.inputs[0] = gpr(32);
        const auto source_result = compile_r5900_ir_x64({bad_source});
        expect(source_result.error == R5900X64CompileError::InvalidRegister && !source_result.block.has_value(),
               "backend must reject invalid source GPR without a block");

        auto bad_destination = valid;
        bad_destination.destination = R5900IrRegister{32};
        expect(compile_r5900_ir_x64({bad_destination}).error == R5900X64CompileError::InvalidRegister,
               "backend must reject invalid destination GPR");

        auto missing_destination = valid;
        missing_destination.destination.reset();
        expect(compile_r5900_ir_x64({missing_destination}).error == R5900X64CompileError::MalformedInstruction,
               "backend must reject missing destination");

        auto wrong_count = valid;
        wrong_count.inputs.pop_back();
        expect(compile_r5900_ir_x64({wrong_count}).error == R5900X64CompileError::MalformedInstruction,
               "backend must reject wrong operand count");

        auto wrong_mode = valid;
        wrong_mode.write_mode = R5900IrGprWriteMode::None;
        expect(compile_r5900_ir_x64({wrong_mode}).error == R5900X64CompileError::MalformedInstruction,
               "backend must reject wrong write mode");

        R5900IrInstruction bad_nop{};
        bad_nop.guest_pc = 0x00103304u;
        bad_nop.opcode = R5900IrOpcode::Nop;
        bad_nop.inputs = {immediate(1)};
        expect(compile_r5900_ir_x64({bad_nop}).error == R5900X64CompileError::MalformedInstruction,
               "backend must reject malformed Nop");

        auto unknown = valid;
        unknown.opcode = static_cast<R5900IrOpcode>(0xff);
        const auto unknown_result = compile_r5900_ir_x64({unknown});
        expect(unknown_result.error == R5900X64CompileError::UnsupportedOpcode && !unknown_result.block.has_value(),
               "backend must reject unknown opcode without a block");
        expect(unknown_result.message.find("IR instruction 0") != std::string::npos,
               "backend validation diagnostic must include instruction index");
    }

    {
        R5900IrExecutionState initial{};
        for (std::size_t index = 0; index < initial.gpr.size(); ++index) {
            initial.gpr[index].low64 = 0x0101010101010101ull * static_cast<std::uint64_t>(index + 1u);
            initial.gpr[index].high64 = 0xf000000000000000ull | static_cast<std::uint64_t>(index);
        }
        initial.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
        initial.gpr[29].low64 = 0x1000u;

        R5900IrInstruction nop{};
        nop.guest_pc = 0x00103400u;
        nop.opcode = R5900IrOpcode::Nop;
        const std::vector<R5900IrInstruction> program = {
            nop,
            write_ir(R5900IrOpcode::AddWordSignExtend, 8, gpr(9), gpr(10), 0x00103404u),
            write_ir(R5900IrOpcode::AddWordSignExtend, 29, gpr(29), immediate(-16), 0x00103408u),
            write_ir(R5900IrOpcode::Or64, 5, gpr(4), immediate(0xff00), 0x0010340cu),
            write_ir(R5900IrOpcode::Or64, 0, immediate(1), immediate(2), 0x00103410u),
        };

        auto expected = initial;
        auto actual = initial;
        expect(execute_r5900_ir(program, expected).ok(), "reference executor must accept differential program");
        auto compiled = compile_r5900_ir_x64(program);
        expect(compiled.ok(), "backend must compile differential program");
        compiled.block->execute(actual);
        expect_states_equal(expected, actual,
                            "native backend must match reference executor for all 32 GPR low/high halves");
    }

    {
        R5900IrExecutionState initial{};
        initial.gpr[9].low64 = 5u;
        initial.gpr[10].low64 = 7u;
        initial.gpr[8].high64 = 0x1111222233334444ull;
        initial.gpr[29] = {0x1000u, 0xaaaabbbbccccddddull};
        initial.gpr[4].low64 = 0x1234567800000000ull;
        initial.gpr[5].high64 = 0x5555666677778888ull;

        const std::uint32_t words[] = {
            0u,
            r_type(9, 10, 8, 0, 0x21),
            i_type(0x09, 29, 29, 0xfff0),
            i_type(0x0d, 4, 5, 0xff00),
        };

        std::vector<R5900IrInstruction> program;
        std::uint32_t pc = 0x00104000u;
        for (const auto word : words) {
            const auto lowered = lower_r5900_instruction(decode_r5900(word), pc);
            expect(lowered.ok(), "synthetic guest instruction must lower");
            expect(lowered.instructions.size() == 1u, "each synthetic instruction must lower to one IR op");
            program.push_back(lowered.instructions.front());
            pc += 4u;
        }

        auto expected = initial;
        auto actual = initial;
        expect(execute_r5900_ir(program, expected).ok(), "reference synthetic pipeline must execute");
        auto compiled = compile_r5900_ir_x64(program);
        expect(compiled.ok(), "decoder/lowering output must compile natively");
        compiled.block->execute(actual);
        expect_states_equal(expected, actual,
                            "decoder -> IR -> x64 native state must match reference executor bit-for-bit");
    }

    std::cout << "r5900_x64_backend_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
