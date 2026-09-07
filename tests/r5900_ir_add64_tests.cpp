#include "recompiler/r5900_decoder.h"
#include "recompiler/r5900_ir.h"
#include "recompiler/r5900_ir_executor.h"
#include "recompiler/r5900_ir_validation.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_add64_tests: FAIL: " << message << '\n';
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

constexpr std::uint32_t r_type(std::uint8_t rs,
                               std::uint8_t rt,
                               std::uint8_t rd,
                               std::uint8_t funct) {
    return (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           funct;
}

R5900IrInstruction add64(std::uint8_t destination,
                         std::uint8_t lhs,
                         std::uint8_t rhs,
                         std::uint32_t guest_pc = 0x00114edcu) {
    R5900IrInstruction ir{};
    ir.guest_pc = guest_pc;
    ir.guest_raw = r_type(lhs, rhs, destination, 0x2du);
    ir.opcode = R5900IrOpcode::Add64;
    ir.destination = R5900IrDestination{destination};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
    ir.inputs = {gpr(lhs), gpr(rhs)};
    return ir;
}

R5900IrInstruction valid_add64() {
    return add64(4u, 29u, 0u);
}

void expect_add(std::uint64_t lhs,
                std::uint64_t rhs,
                std::uint64_t expected,
                const char* message) {
    R5900IrExecutionState state{};
    state.gpr[1].low64 = lhs;
    state.gpr[2].low64 = rhs;
    state.gpr[3].high64 = 0x1122334455667788ull;
    state.hi = 0x8877665544332211ull;

    const auto result = execute_r5900_ir({add64(3u, 1u, 2u)}, state);
    expect(result.ok(), message);
    expect(state.gpr[3].low64 == expected, message);
    expect(state.gpr[3].high64 == 0x1122334455667788ull,
           "Add64 must preserve destination high64");
    expect(state.hi == 0x8877665544332211ull,
           "Add64 must preserve unrelated architectural state");
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    const auto raw = r_type(29u, 0u, 4u, 0x2du);
    const auto lowered = lower_r5900_instruction(decode_r5900(raw), 0x00114edcu);
    expect(lowered.ok() && lowered.instructions.size() == 1u,
           "DADDU must lower to one IR instruction");

    const auto& ir = lowered.instructions[0];
    expect(ir.opcode == R5900IrOpcode::Add64,
           "DADDU must lower to Add64");
    expect(ir.destination.has_value() &&
               ir.destination->kind == R5900IrDestinationKind::Gpr &&
               ir.destination->index == 4u &&
               ir.write_mode == R5900IrGprWriteMode::Low64PreserveUpper64,
           "DADDU destination contract mismatch");
    expect(ir.inputs.size() == 2u &&
               ir.inputs[0].kind == R5900IrOperandKind::Gpr &&
               ir.inputs[0].gpr_index == 29u &&
               ir.inputs[1].kind == R5900IrOperandKind::Gpr &&
               ir.inputs[1].gpr_index == 0u,
           "DADDU inputs must be rs then rt");
    expect(ir.guest_pc == 0x00114edcu && ir.guest_raw == raw,
           "DADDU provenance must be preserved");
    expect(validate_r5900_ir_instruction(ir, 0u).ok(),
           "lowered Add64 must validate");

    const auto zero = lower_r5900_instruction(
        decode_r5900(r_type(7u, 8u, 0u, 0x2du)), 0x00120000u);
    expect(zero.ok() && zero.instructions.size() == 1u &&
               zero.instructions[0].opcode == R5900IrOpcode::Nop &&
               zero.instructions[0].guest_pc == 0x00120000u,
           "DADDU to r0 must lower to provenance Nop");

    {
        auto bad = valid_add64();
        bad.destination.reset();
        expect(!validate_r5900_ir_instruction(bad, 0u).ok(),
               "Add64 requires a destination");
    }
    {
        auto bad = valid_add64();
        bad.destination = R5900IrDestination{R5900IrDestinationKind::Hi, 0u};
        expect(!validate_r5900_ir_instruction(bad, 0u).ok(),
               "Add64 requires a GPR destination");
    }
    {
        auto bad = valid_add64();
        bad.destination = R5900IrDestination{32u};
        expect(validate_r5900_ir_instruction(bad, 0u).error ==
                   R5900IrValidationError::InvalidRegister,
               "Add64 destination register range must be validated");
    }
    {
        auto bad = valid_add64();
        bad.write_mode = R5900IrGprWriteMode::None;
        expect(!validate_r5900_ir_instruction(bad, 0u).ok(),
               "Add64 requires Low64PreserveUpper64");
    }
    {
        auto bad = valid_add64();
        bad.inputs = {gpr(29u)};
        expect(!validate_r5900_ir_instruction(bad, 0u).ok(),
               "Add64 requires two inputs");
    }
    {
        auto bad = valid_add64();
        bad.inputs[1] = immediate(1);
        expect(!validate_r5900_ir_instruction(bad, 0u).ok(),
               "Add64 rejects immediate inputs");
    }
    {
        auto bad = valid_add64();
        bad.inputs[0].kind = R5900IrOperandKind::Fpr;
        expect(!validate_r5900_ir_instruction(bad, 0u).ok(),
               "Add64 rejects FPR inputs");
    }
    {
        auto bad = valid_add64();
        bad.inputs[0] = gpr(32u);
        expect(validate_r5900_ir_instruction(bad, 0u).error ==
                   R5900IrValidationError::InvalidRegister,
               "Add64 source register range must be validated");
    }

    expect_add(1u, 2u, 3u, "Add64 ordinary addition must execute");
    expect_add(0x0000000100000000ull, 2u, 0x0000000100000002ull,
               "Add64 must use all 64 source bits");
    expect_add(std::numeric_limits<std::uint64_t>::max(), 1u, 0u,
               "Add64 must wrap modulo 2^64");
    expect_add(0x7fffffffffffffffull, 1u, 0x8000000000000000ull,
               "Add64 must cross the signed boundary without trapping");

    {
        R5900IrExecutionState state{};
        state.gpr[4] = {5u, 0xaaaaaaaa55555555ull};
        state.gpr[5].low64 = 7u;
        const auto result = execute_r5900_ir({add64(4u, 4u, 5u)}, state);
        expect(result.ok() && state.gpr[4].low64 == 12u &&
                   state.gpr[4].high64 == 0xaaaaaaaa55555555ull,
               "Add64 must support rd==rs aliasing");
    }
    {
        R5900IrExecutionState state{};
        state.gpr[4] = {5u, 0xbbbbbbbb66666666ull};
        state.gpr[5].low64 = 7u;
        const auto result = execute_r5900_ir({add64(4u, 5u, 4u)}, state);
        expect(result.ok() && state.gpr[4].low64 == 12u &&
                   state.gpr[4].high64 == 0xbbbbbbbb66666666ull,
               "Add64 must support rd==rt aliasing");
    }
    {
        R5900IrExecutionState state{};
        state.gpr[4] = {9u, 0xcccccccc77777777ull};
        const auto result = execute_r5900_ir({add64(4u, 4u, 4u)}, state);
        expect(result.ok() && state.gpr[4].low64 == 18u &&
                   state.gpr[4].high64 == 0xcccccccc77777777ull,
               "Add64 must support rd==rs==rt aliasing");
    }
    {
        R5900IrExecutionState state{};
        state.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
        const auto result = execute_r5900_ir({add64(0u, 0u, 0u)}, state);
        expect(result.ok() && state.gpr[0].low64 == 0u && state.gpr[0].high64 == 0u,
               "GPR0 must remain architectural zero during Add64 execution");
    }

    std::cout << "r5900_ir_add64_tests: PASS\n";
    return EXIT_SUCCESS;
}
