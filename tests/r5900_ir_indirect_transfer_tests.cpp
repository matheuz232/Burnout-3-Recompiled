#include "r5900_direct_transfer_test_support.h"
#include "recompiler/r5900_ir_executor.h"
#include "recompiler/r5900_ir_validation.h"
#if defined(B3R_TEST_NATIVE)
#include "recompiler/windows/r5900_x64_backend.h"
#endif

#include <cstdlib>
#include <iostream>

namespace {
using namespace b3r::recompiler;
using namespace b3r::test_support;
constexpr std::uint32_t pc = 0x00108004u;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "indirect transfer: FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

R5900IrBlock transfer(bool call, std::uint8_t rs = 7u, std::uint8_t rd = 31u) {
    R5900IrBlock block{};
    block.terminator.guest_pc = pc;
    block.terminator.kind = call ? R5900IrTerminatorKind::IndirectCall
                                 : R5900IrTerminatorKind::IndirectJump;
    block.terminator.inputs = {gpr(rs)};
    block.terminator.delay_slot = {nop(pc + 4u)};
    if (call) {
        block.terminator.link_gpr = rd;
        block.terminator.link_pc = pc + 8u;
    }
    return block;
}

void equal_state(const R5900IrExecutionState& a, const R5900IrExecutionState& b) {
    for (std::size_t i = 0; i < a.gpr.size(); ++i) {
        expect(a.gpr[i].low64 == b.gpr[i].low64 && a.gpr[i].high64 == b.gpr[i].high64,
               "all GPR halves must match expected/reference state");
    }
    expect(a.hi == b.hi && a.lo == b.lo && a.hi1 == b.hi1 && a.lo1 == b.lo1 &&
           a.sa == b.sa && a.fpr == b.fpr && a.fcr31 == b.fcr31 && a.fp_acc == b.fp_acc,
           "special and COP1 state must match expected/reference state");
}

R5900IrBlockExecutionResult run(const R5900IrBlock& block, R5900IrExecutionState& state) {
#if defined(B3R_TEST_NATIVE)
    auto reference = state;
    const auto expected = execute_r5900_ir_block(block, reference);
    auto compiled = compile_r5900_ir_x64(block);
    expect(compiled.ok(), "valid indirect block must compile");
    R5900IrExecutionContext context{};
    context.state = &state;
    const auto actual = compiled.block->execute(context);
    expect(actual.error == expected.error && actual.next_pc == expected.next_pc,
           "native status/target must match reference");
    equal_state(state, reference);
    return {actual.error, actual.message, actual.next_pc};
#else
    return execute_r5900_ir_block(block, state);
#endif
}

void rejected(const R5900IrBlock& invalid, R5900IrValidationError expected) {
    expect(validate_r5900_ir_block(invalid).error == expected,
           "malformed indirect block must fail validation with expected category");
    R5900IrExecutionState state{};
    state.gpr[0] = {0xdead, 0xbeef};
    state.gpr[31] = {123, 456};
    const auto before = state;
    expect(!execute_r5900_ir_block(invalid, state).ok(), "invalid block must not execute");
    equal_state(state, before);
#if defined(B3R_TEST_NATIVE)
    expect(!compile_r5900_ir_x64(invalid).ok(), "invalid block must not publish native code");
#endif
}
} // namespace

int main() {
    for (bool call : {false, true}) {
        auto block = transfer(call);
        block.body = {addiu(7u, 7u, 4, pc - 4u)};
        block.terminator.delay_slot = {addiu(7u, 0u, 9, pc + 4u)};
        R5900IrExecutionState state{};
        state.gpr[7] = {0x00109000u, 0x1122334455667788ull};
        const auto result = run(block, state);
        expect(result.ok() && result.next_pc == 0x00109004u,
               "snapshot must occur after body and before source-changing delay");
        expect(state.gpr[7].low64 == 9u && state.gpr[7].high64 == 0x1122334455667788ull,
               "delay executes once and preserves high64");
    }

    for (auto rd : {std::uint8_t{0}, std::uint8_t{7}, std::uint8_t{19}, std::uint8_t{31}}) {
        auto block = transfer(true, 7u, rd);
        block.terminator.delay_slot = {addiu(8u, rd, 0, pc + 4u)};
        R5900IrExecutionState state{};
        state.gpr[0] = {123, 456};
        state.gpr[7] = {0xabcdef1200109000ull, 0xaaaaaaaaaaaaaaaaull};
        state.gpr[rd].high64 = 0xbbbbbbbbbbbbbbbbull;
        const auto result = run(block, state);
        expect(result.ok() && result.next_pc == 0x00109000u,
               "JALR must use low32 snapshot, including rd == rs");
        expect(state.gpr[8].low64 == (rd == 0u ? 0u : pc + 8u),
               "delay must observe new link, except immutable GPR0");
        expect(state.gpr[0].low64 == 0u && state.gpr[0].high64 == 0u,
               "GPR0 must be normalized");
        if (rd != 0u) expect(state.gpr[rd].high64 == 0xbbbbbbbbbbbbbbbbull,
                             "link must preserve upper 64 bits");
    }

    for (bool call : {false, true}) {
        for (const std::uint32_t target : {0u, 0x80001234u, 0xfffffffcu, 0x00101235u}) {
            R5900IrExecutionState state{};
            state.gpr[7] = {0xabcdef0000000000ull | target, 0x87654321u};
            const auto result = run(transfer(call), state);
            expect(result.ok() && result.next_pc == target,
                   "target bits must survive; fetch validation must not be replaced by rounding");
        }
        R5900IrExecutionState state{};
        state.gpr[0] = {123, 456};
        expect(run(transfer(call, 0u), state).next_pc == 0u,
               "zero-register source must produce guest address zero");
    }

    {
        auto block = transfer(true);
        block.terminator.guest_pc = 0xfffffffcu;
        block.terminator.link_pc = 4u;
        block.terminator.delay_slot = {addiu(31u, 31u, 1, 0u)};
        R5900IrExecutionState state{};
        state.gpr[7].low64 = 0x00109000u;
        expect(run(block, state).next_pc == 0x00109000u && state.gpr[31].low64 == 5u,
               "wrapped PC+8 link must be visible and overwritable by delay");
    }

    for (bool call : {false, true}) {
        auto block = transfer(call);
        block.body = {store128(2u, 3u, 0, pc - 4u)};
        block.terminator.delay_slot = {addiu(8u, 0u, 99, pc + 4u)};
        R5900IrExecutionState state{};
        state.gpr[31] = {123, 456};
        expect(run(block, state).error == R5900IrExecutionError::MemoryAccessFailure,
               "body store failure must stop before indirect transfer");
        expect(state.gpr[31].low64 == 123u && state.gpr[8].low64 == 0u,
               "body failure must prevent link and delay effects");
    }

    const auto malformed = R5900IrValidationError::MalformedInstruction;
    const auto invalid_register = R5900IrValidationError::InvalidRegister;
    for (bool call : {false, true}) {
        auto invalid = transfer(call);
        invalid.terminator.inputs.clear(); rejected(invalid, malformed);
        invalid = transfer(call);
        invalid.terminator.inputs = {immediate(7)}; rejected(invalid, malformed);
        invalid = transfer(call);
        invalid.terminator.inputs = {gpr(32)}; rejected(invalid, invalid_register);
        invalid = transfer(call);
        invalid.terminator.target_pc = 4u; rejected(invalid, malformed);
        invalid = transfer(call);
        invalid.terminator.delay_slot.clear(); rejected(invalid, malformed);
        invalid = transfer(call);
        invalid.terminator.delay_slot.push_back(nop(pc + 8u)); rejected(invalid, malformed);
        invalid = transfer(call);
        invalid.terminator.delay_slot = {store128(2, 3, 0, pc + 4u)};
        rejected(invalid, R5900IrValidationError::UnsupportedOpcode);
    }
    auto invalid = transfer(true);
    invalid.terminator.link_gpr.reset(); rejected(invalid, malformed);
    invalid = transfer(true);
    invalid.terminator.link_gpr = 32u; rejected(invalid, invalid_register);
    invalid = transfer(true);
    invalid.terminator.link_pc += 4u; rejected(invalid, malformed);
    invalid = transfer(false);
    invalid.terminator.link_gpr = 31u; rejected(invalid, malformed);
    invalid = direct_call(pc, 0x00109000u, nop(pc + 4u));
    invalid.terminator.link_gpr = 19u; rejected(invalid, malformed);

    for (const std::uint32_t word : {0x00e10008u, 0x00e00808u, 0x00e00048u,
                                     0x00e1f809u, 0x00e0f849u}) {
        expect(decode_r5900(word).instruction == R5900Instruction::Unknown,
               "reserved JR/JALR encoding fields must be rejected");
    }
    expect(decode_r5900(0x00e00008u).instruction == R5900Instruction::Jr &&
           decode_r5900(0x00e0f809u).instruction == R5900Instruction::Jalr,
           "canonical indirect instructions must decode");
    std::cout << "r5900 indirect transfer tests: PASS\n";
}
