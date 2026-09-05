#include "recompiler/r5900_ir_executor.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_block_executor_tests: FAIL: " << message << '\n';
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

R5900IrInstruction addiu(std::uint8_t rt,
                         std::uint8_t rs,
                         std::int16_t imm,
                         std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::AddWordSignExtend;
    ir.destination = R5900IrDestination{R5900IrDestinationKind::Gpr, rt};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
    ir.inputs = {gpr(rs), immediate(imm)};
    return ir;
}

R5900IrInstruction nop(std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::Nop;
    return ir;
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

bool states_equal(const R5900IrExecutionState& lhs,
                  const R5900IrExecutionState& rhs) {
    if (lhs.gpr != rhs.gpr ||
        lhs.hi != rhs.hi || lhs.lo != rhs.lo ||
        lhs.hi1 != rhs.hi1 || lhs.lo1 != rhs.lo1 ||
        lhs.sa != rhs.sa || lhs.fpr != rhs.fpr ||
        lhs.fcr31 != rhs.fcr31 || lhs.fp_acc != rhs.fp_acc) {
        return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    {
        R5900IrExecutionState state{};
        state.gpr[1] = {5u, 0x1111111111111111ull};
        state.gpr[2] = {5u, 0x2222222222222222ull};
        const auto block = beq_block(0x00105000u,
                                     1u,
                                     2u,
                                     0x00105040u,
                                     0x00105008u,
                                     addiu(1u, 1u, 1, 0x00105004u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok(), "taken BEQ block must execute");
        expect(result.next_pc == 0x00105040u,
               "BEQ predicate must be captured before delay slot mutates rs");
        expect(state.gpr[1].low64 == 6u,
               "taken BEQ delay slot must execute exactly once");
        expect(state.gpr[1].high64 == 0x1111111111111111ull,
               "delay ADDIU must preserve rs high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[1] = {5u, 0xaaaaaaaaaaaaaaaaull};
        state.gpr[2] = {6u, 0xbbbbbbbbbbbbbbbbull};
        const auto block = beq_block(0x00105100u,
                                     1u,
                                     2u,
                                     0x00105140u,
                                     0x00105108u,
                                     addiu(2u, 2u, 1, 0x00105104u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok(), "not-taken BEQ block must execute");
        expect(result.next_pc == 0x00105108u,
               "not-taken BEQ must return fallthrough PC");
        expect(state.gpr[2].low64 == 7u,
               "not-taken BEQ delay slot must execute exactly once");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[3] = {0x123456789abcdef0ull, 0x1111111111111111ull};
        state.gpr[4] = {0x123456789abcdef0ull, 0x9999999999999999ull};
        const auto block = beq_block(0x00105200u,
                                     3u,
                                     4u,
                                     0x00105240u,
                                     0x00105208u,
                                     nop(0x00105204u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok(), "BEQ low64 equality block must execute");
        expect(result.next_pc == 0x00105240u,
               "BEQ must ignore GPR high64 halves");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[5] = {10u, 0x5555555555555555ull};
        const auto before = state;
        auto block = beq_block(0x00105300u,
                               0u,
                               0u,
                               0x00105340u,
                               0x00105308u,
                               nop(0x00105304u));
        block.body = {addiu(5u, 5u, 1, 0x001052fcu)};
        block.terminator.delay_slot.front().opcode =
            static_cast<R5900IrOpcode>(0xffu);
        const auto result = execute_r5900_ir_block(block, state);
        expect(!result.ok(), "malformed BEQ block must fail before execution");
        expect(states_equal(state, before),
               "failed block validation must preserve architectural state atomically");
    }

    {
        R5900IrExecutionState state{};
        R5900IrBlock block{};
        block.body = {addiu(6u, 0u, 9, 0x00105400u)};
        block.terminator.guest_pc = 0x00105404u;
        block.terminator.kind = R5900IrTerminatorKind::Fallthrough;
        block.terminator.fallthrough_pc = 0x00105404u;
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok(), "fallthrough block must execute");
        expect(result.next_pc == 0x00105404u,
               "fallthrough block must return explicit fallthrough PC");
        expect(state.gpr[6].low64 == 9u,
               "fallthrough block body must execute");
    }

    std::cout << "r5900_ir_block_executor_tests: PASS\n";
    return EXIT_SUCCESS;
}
