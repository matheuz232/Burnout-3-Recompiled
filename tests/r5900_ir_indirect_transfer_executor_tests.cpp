#include "r5900_branch_likely_test_support.h"
#include "recompiler/r5900_ir_executor.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {
using namespace b3r::recompiler;
using namespace b3r::test_support;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_indirect_transfer_executor_tests: FAIL: "
              << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) fail(message);
}

struct MemoryProbe {
    bool succeed{true};
    std::size_t calls{};
    std::uint32_t address{};
    std::uint64_t low64{};
    std::uint64_t high64{};
};

bool write128(void* user,
              std::uint32_t address,
              std::uint64_t low64,
              std::uint64_t high64) noexcept {
    auto& probe = *static_cast<MemoryProbe*>(user);
    ++probe.calls;
    probe.address = address;
    probe.low64 = low64;
    probe.high64 = high64;
    return probe.succeed;
}

R5900IrExecutionContext context_for(R5900IrExecutionState& state,
                                    MemoryProbe& probe) {
    R5900IrExecutionContext context{};
    context.state = &state;
    context.memory.user = &probe;
    context.memory.write128 = &write128;
    return context;
}
} // namespace

int main() {
    {
        R5900IrExecutionState state{};
        state.gpr[5] = {0x1234000012345678ull, 0xfeedfacefeedfaceull};
        state.gpr[31] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
        const auto before_r31 = state.gpr[31];
        const auto block = indirect_jump(
            0x00109000u, 5u, addiu(5u, 0u, 9, 0x00109004u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok() && result.next_pc == 0x12345678u,
               "JR must return snapshotted low32 target");
        expect(state.gpr[5].low64 == 9u &&
                   state.gpr[5].high64 == 0xfeedfacefeedfaceull,
               "JR delay may mutate source only after target snapshot");
        expect(state.gpr[31].low64 == before_r31.low64 &&
                   state.gpr[31].high64 == before_r31.high64,
               "JR must not modify link registers");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[5] = {0x000000000010a000ull, 0x0123456789abcdefull};
        const auto block = indirect_call(
            0x00109200u, 5u, 5u, addiu(6u, 5u, 0, 0x00109204u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok() && result.next_pc == 0x0010a000u,
               "JALR rd==rs must jump using pre-link source");
        expect(state.gpr[5].low64 == 0x00109208u &&
                   state.gpr[5].high64 == 0x0123456789abcdefull,
               "JALR must write zero-extended PC+8 low64 and preserve high64");
        expect(state.gpr[6].low64 == 0x00109208u,
               "JALR delay must observe link-before-delay");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[7].low64 = 0x0010b000u;
        state.gpr[0] = {0x1111u, 0x2222u};
        const auto block = indirect_call(
            0x00109400u, 7u, 0u, nop(0x00109404u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok() && result.next_pc == 0x0010b000u,
               "JALR rd==0 must still jump");
        expect(state.gpr[0].low64 == 0u && state.gpr[0].high64 == 0u,
               "JALR rd==0 must leave GPR0 normalized");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[8].low64 = 0x0010c000u;
        state.gpr[9] = {0x7777u, 0x9999aaaabbbbccccull};
        const auto block = indirect_call(
            0x00109600u, 8u, 9u, addiu(9u, 0u, 3, 0x00109604u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok() && result.next_pc == 0x0010c000u,
               "JALR with link-writing delay must still use snapshot target");
        expect(state.gpr[9].low64 == 3u &&
                   state.gpr[9].high64 == 0x9999aaaabbbbccccull,
               "delay write must win after JALR link and preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[5].low64 = 0x0010d000u;
        state.gpr[2].low64 = 0x00004017u;
        state.gpr[3] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
        state.gpr[9] = {0x5555666677778888ull, 0x123456789abcdef0ull};
        MemoryProbe probe{};
        probe.succeed = false;
        auto context = context_for(state, probe);
        const auto block = indirect_call(
            0x00109800u, 5u, 9u,
            store128(2u, 3u, 0, 0x00109804u));
        const auto result = execute_r5900_ir_block(block, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "JALR delay memory failure must propagate");
        expect(state.gpr[9].low64 == 0x00109808u &&
                   state.gpr[9].high64 == 0x123456789abcdef0ull,
               "JALR link must remain committed before failing delay");
        expect(probe.calls == 1u && context.memory_fault.active &&
                   context.memory_fault.guest_pc == 0x00109804u &&
                   context.memory_fault.width_bytes == 16u,
               "JALR delay memory fault details mismatch");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[5].low64 = 0x0010e000u;
        state.gpr[2].low64 = 0x00400000u;
        state.gpr[3] = {0x1111222233334444ull, 0x5555666677778888ull};
        state.gpr[9] = {0x9999u, 0xaaaabbbbccccddddull};
        state.gpr[24] = {0x1234u, 0x5678u};
        const auto before_link = state.gpr[9];
        const auto before_delay = state.gpr[24];

        auto block = indirect_call(
            0x00109a04u, 5u, 9u, addiu(24u, 0u, 1, 0x00109a08u));
        block.body = {store128(2u, 3u, 0, 0x00109a00u)};
        R5900IrExecutionContext context{};
        context.state = &state;
        const auto result = execute_r5900_ir_block(block, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "body Store128 failure must precede JALR semantics");
        expect(state.gpr[9].low64 == before_link.low64 &&
                   state.gpr[9].high64 == before_link.high64,
               "body failure must prevent JALR link write");
        expect(state.gpr[24].low64 == before_delay.low64 &&
                   state.gpr[24].high64 == before_delay.high64,
               "body failure must prevent JALR delay execution");
        expect(context.memory_fault.active &&
                   context.memory_fault.guest_pc == 0x00109a00u,
               "body failure must report body fault PC");
    }

    // Task 2 RED: likely terminators validate, but executor semantics do not exist yet.
    {
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 7u;
        state.gpr[5].low64 = 7u;
        const auto block = branch_equal_likely(
            0x00109c00u, 4u, 5u, 0x00109d00u,
            addiu(8u, 8u, 1, 0x00109c04u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok() && result.next_pc == 0x00109d00u &&
                   state.gpr[8].low64 == 1u,
               "BEQL taken must execute delay and return target");
    }

    std::cout << "r5900_ir_indirect_transfer_executor_tests: PASS\n";
    return EXIT_SUCCESS;
}
