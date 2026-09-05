#include "r5900_direct_transfer_test_support.h"
#include "recompiler/r5900_ir_executor.h"

#include <cstdlib>
#include <iostream>

namespace {
using namespace b3r::recompiler;
using namespace b3r::test_support;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_direct_transfer_executor_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) fail(message);
}
} // namespace

int main() {
    {
        R5900IrExecutionState state{};
        state.gpr[7] = {9u, 0x7777777777777777ull};
        state.gpr[31] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
        const auto block = direct_jump(0x00107000u, 0x00107100u,
                                       addiu(7u, 7u, 1, 0x00107004u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok() && result.next_pc == 0x00107100u,
               "direct J must execute and return target");
        expect(state.gpr[7].low64 == 10u,
               "direct J delay must execute once");
        expect(state.gpr[31].low64 == 0x1111222233334444ull &&
                   state.gpr[31].high64 == 0xaaaabbbbccccddddull,
               "direct J must preserve r31");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[31] = {0xdeadbeefdeadbeefull, 0x0123456789abcdefull};
        const auto block = direct_call(0x00107200u, 0x00107300u,
                                       addiu(23u, 31u, 0, 0x00107204u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok() && result.next_pc == 0x00107300u,
               "direct JAL must execute and return target");
        expect(state.gpr[31].low64 == 0x00107208u &&
                   state.gpr[31].high64 == 0x0123456789abcdefull,
               "JAL must write PC+8 and preserve r31 high64");
        expect(state.gpr[23].low64 == 0x00107208u,
               "JAL delay must observe link-before-delay");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[31] = {0x5555666677778888ull, 0xfedcba9876543210ull};
        const auto block = direct_call(0x00107400u, 0x00107500u,
                                       addiu(31u, 0u, 9, 0x00107404u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok(), "JAL with r31-writing delay must execute");
        expect(state.gpr[31].low64 == 9u &&
                   state.gpr[31].high64 == 0xfedcba9876543210ull,
               "delay write must win after JAL link and preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[2].low64 = 0x00400000u;
        state.gpr[3] = {0x1111222233334444ull, 0x5555666677778888ull};
        state.gpr[24] = {0x9999u, 0xaaaaull};
        state.gpr[31] = {0x1234567890abcdefull, 0x0fedcba987654321ull};
        const auto before_r31 = state.gpr[31];
        const auto before_r24 = state.gpr[24];

        auto block = direct_call(0x00107604u, 0x00107700u,
                                 addiu(24u, 0u, 1, 0x00107608u));
        block.body = {store128(2u, 3u, 0, 0x00107600u)};
        R5900IrExecutionContext context{};
        context.state = &state;
        const auto result = execute_r5900_ir_block(block, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "body Store128 failure must precede JAL semantics");
        expect(state.gpr[31].low64 == before_r31.low64 &&
                   state.gpr[31].high64 == before_r31.high64,
               "body failure must prevent link write");
        expect(state.gpr[24].low64 == before_r24.low64 &&
                   state.gpr[24].high64 == before_r24.high64,
               "body failure must prevent delay execution");
        expect(context.memory_fault.active &&
                   context.memory_fault.guest_pc == 0x00107600u,
               "body memory fault must retain faulting guest PC");
    }

    std::cout << "r5900_ir_direct_transfer_executor_tests: PASS\n";
    return EXIT_SUCCESS;
}
