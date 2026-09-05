#include "recompiler/r5900_ir_executor.h"

#include <cstddef>
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

R5900IrInstruction store128(std::uint8_t base,
                            std::uint8_t source,
                            std::int16_t imm,
                            std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::Store128;
    ir.inputs = {gpr(base), gpr(source), immediate(imm)};
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

R5900IrBlock direct_jump(std::uint32_t pc,
                         std::uint32_t target_pc,
                         R5900IrInstruction delay) {
    R5900IrBlock block{};
    block.terminator.guest_pc = pc;
    block.terminator.kind = R5900IrTerminatorKind::DirectJump;
    block.terminator.target_pc = target_pc;
    block.terminator.delay_slot = {delay};
    return block;
}

R5900IrBlock direct_call(std::uint32_t pc,
                         std::uint32_t target_pc,
                         R5900IrInstruction delay) {
    auto block = direct_jump(pc, target_pc, delay);
    block.terminator.kind = R5900IrTerminatorKind::DirectCall;
    block.terminator.link_pc = pc + 8u;
    return block;
}

bool states_equal(const R5900IrExecutionState& lhs,
                  const R5900IrExecutionState& rhs) {
    for (std::size_t index = 0; index < lhs.gpr.size(); ++index) {
        if (lhs.gpr[index].low64 != rhs.gpr[index].low64 ||
            lhs.gpr[index].high64 != rhs.gpr[index].high64) {
            return false;
        }
    }
    if (lhs.hi != rhs.hi || lhs.lo != rhs.lo ||
        lhs.hi1 != rhs.hi1 || lhs.lo1 != rhs.lo1 ||
        lhs.sa != rhs.sa) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.fpr.size(); ++index) {
        if (lhs.fpr[index] != rhs.fpr[index]) {
            return false;
        }
    }
    return lhs.fcr31 == rhs.fcr31 && lhs.fp_acc == rhs.fp_acc;
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
        state.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
        const auto block = beq_block(0x00105280u,
                                     0u,
                                     0u,
                                     0x001052c0u,
                                     0x00105288u,
                                     addiu(7u, 0u, 3, 0x00105284u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok(), "BEQ r0,r0 block must execute");
        expect(result.next_pc == 0x001052c0u,
               "BEQ r0,r0 must always take after GPR0 normalization");
        expect(state.gpr[0].low64 == 0u && state.gpr[0].high64 == 0u,
               "block execution must keep GPR0 normalized");
        expect(state.gpr[7].low64 == 3u,
               "BEQ r0,r0 delay slot must execute exactly once");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[8] = {12u, 0x8888888888888888ull};
        state.gpr[9] = {12u, 0x9999999999999999ull};
        const auto block = beq_block(0x001052d0u,
                                     8u,
                                     9u,
                                     0x00105310u,
                                     0x001052d8u,
                                     addiu(9u, 9u, 1, 0x001052d4u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok(), "BEQ with rt-mutating delay must execute");
        expect(result.next_pc == 0x00105310u,
               "BEQ predicate must be captured before delay slot mutates rt");
        expect(state.gpr[9].low64 == 13u,
               "rt-mutating delay slot must execute exactly once");
        expect(state.gpr[9].high64 == 0x9999999999999999ull,
               "delay ADDIU must preserve rt high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[5] = {10u, 0x5555555555555555ull};
        const auto before = state;
        auto block = beq_block(0x00105340u,
                               0u,
                               0u,
                               0x00105380u,
                               0x00105348u,
                               nop(0x00105344u));
        block.body = {addiu(5u, 5u, 1, 0x0010533cu)};
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

    {
        R5900IrExecutionState state{};
        state.gpr[7] = {9u, 0x7777777777777777ull};
        state.gpr[31] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
        const auto block = direct_jump(0x00107000u,
                                       0x00107100u,
                                       addiu(7u, 7u, 1, 0x00107004u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok(), "direct J block must execute");
        expect(result.next_pc == 0x00107100u,
               "direct J must return target PC");
        expect(state.gpr[7].low64 == 10u,
               "direct J delay slot must execute exactly once");
        expect(state.gpr[31].low64 == 0x1111222233334444ull &&
                   state.gpr[31].high64 == 0xaaaabbbbccccddddull,
               "direct J must not modify r31");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[31] = {0xdeadbeefdeadbeefull, 0x0123456789abcdefull};
        const auto block = direct_call(0x00107200u,
                                       0x00107300u,
                                       addiu(23u, 31u, 0, 0x00107204u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok(), "direct JAL block must execute");
        expect(result.next_pc == 0x00107300u,
               "JAL must return direct target");
        expect(state.gpr[31].low64 == 0x00107208u,
               "JAL link mismatch");
        expect(state.gpr[31].high64 == 0x0123456789abcdefull,
               "JAL must preserve r31 high64");
        expect(state.gpr[23].low64 == 0x00107208u,
               "JAL delay must observe new link");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[31] = {0x5555666677778888ull, 0xfedcba9876543210ull};
        const auto block = direct_call(0x00107400u,
                                       0x00107500u,
                                       addiu(31u, 0u, 9, 0x00107404u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok(), "JAL with r31-writing delay must execute");
        expect(state.gpr[31].low64 == 9u,
               "delay write to r31 must occur after link and win");
        expect(state.gpr[31].high64 == 0xfedcba9876543210ull,
               "delay ADDIU must preserve r31 high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[2].low64 = 0x00400000u;
        state.gpr[3] = {0x1111222233334444ull, 0x5555666677778888ull};
        state.gpr[24] = {0x9999u, 0xaaaaull};
        state.gpr[31] = {0x1234567890abcdefull, 0x0fedcba987654321ull};
        const auto before_r31 = state.gpr[31];
        const auto before_r24 = state.gpr[24];

        auto block = direct_call(0x00107604u,
                                 0x00107700u,
                                 addiu(24u, 0u, 1, 0x00107608u));
        block.body = {store128(2u, 3u, 0, 0x00107600u)};

        R5900IrExecutionContext context{};
        context.state = &state;
        const auto result = execute_r5900_ir_block(block, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "body Store128 failure must propagate before JAL");
        expect(state.gpr[31].low64 == before_r31.low64 &&
                   state.gpr[31].high64 == before_r31.high64,
               "body failure must prevent JAL link write");
        expect(state.gpr[24].low64 == before_r24.low64 &&
                   state.gpr[24].high64 == before_r24.high64,
               "body failure must prevent JAL delay execution");
        expect(context.memory_fault.active &&
                   context.memory_fault.guest_pc == 0x00107600u,
               "body failure must retain Store128 provenance");
    }

    std::cout << "r5900_ir_block_executor_tests: PASS\n";
    return EXIT_SUCCESS;
}
