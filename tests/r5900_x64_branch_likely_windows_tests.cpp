#include "r5900_branch_likely_test_support.h"
#include "recompiler/r5900_ir_executor.h"
#include "recompiler/windows/r5900_x64_backend.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {
using namespace b3r::recompiler;
using namespace b3r::test_support;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_x64_branch_likely_windows_tests: FAIL: "
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
};

bool write128(void* user,
              std::uint32_t address,
              std::uint64_t,
              std::uint64_t) noexcept {
    auto& probe = *static_cast<MemoryProbe*>(user);
    ++probe.calls;
    probe.address = address;
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

void expect_states_equal(const R5900IrExecutionState& lhs,
                         const R5900IrExecutionState& rhs,
                         const char* message) {
    for (std::size_t i = 0; i < lhs.gpr.size(); ++i) {
        if (lhs.gpr[i].low64 != rhs.gpr[i].low64 ||
            lhs.gpr[i].high64 != rhs.gpr[i].high64) {
            fail(message);
        }
    }
}

void run_differential(const R5900IrBlock& block,
                      R5900IrExecutionState initial,
                      bool memory_succeeds,
                      std::size_t expected_calls,
                      const char* message) {
    auto reference_state = initial;
    auto native_state = initial;
    MemoryProbe reference_probe{};
    MemoryProbe native_probe{};
    reference_probe.succeed = memory_succeeds;
    native_probe.succeed = memory_succeeds;
    auto reference_context = context_for(reference_state, reference_probe);
    auto native_context = context_for(native_state, native_probe);

    const auto reference_result = execute_r5900_ir_block(block, reference_context);
    auto compiled = compile_r5900_ir_x64(block);
    expect(compiled.ok() && compiled.block.has_value(),
           "likely branch must compile natively");
    const auto native_result = compiled.block->execute(native_context);

    expect(reference_result.error == native_result.error,
           "reference/native likely error mismatch");
    if (reference_result.ok()) {
        expect(reference_result.next_pc == native_result.next_pc,
               "reference/native likely next-PC mismatch");
    }
    expect_states_equal(reference_state, native_state, message);
    expect(reference_probe.calls == expected_calls &&
               native_probe.calls == expected_calls,
           "reference/native helper-call count mismatch");
    expect(reference_context.memory_fault.active ==
               native_context.memory_fault.active,
           "reference/native likely fault activity mismatch");
}
} // namespace

int main() {
    {
        auto block = branch_equal_likely(
            0x0010d000u, 4u, 5u, 0x0010d100u,
            addiu(8u, 8u, 1, 0x0010d004u));
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 9u;
        state.gpr[5].low64 = 9u;
        run_differential(block, state, true, 0u,
                         "BEQL taken differential mismatch");
    }
    {
        auto block = branch_equal_likely(
            0x0010d000u, 4u, 5u, 0x0010d100u,
            addiu(8u, 8u, 1, 0x0010d004u));
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 9u;
        state.gpr[5].low64 = 10u;
        run_differential(block, state, true, 0u,
                         "BEQL not-taken differential mismatch");
    }
    {
        auto block = branch_not_equal_likely(
            0x0010d200u, 4u, 5u, 0x0010d300u,
            addiu(8u, 8u, 1, 0x0010d204u));
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 9u;
        state.gpr[5].low64 = 10u;
        run_differential(block, state, true, 0u,
                         "BNEL taken differential mismatch");
    }
    {
        auto block = branch_not_equal_likely(
            0x0010d200u, 4u, 5u, 0x0010d300u,
            addiu(8u, 8u, 1, 0x0010d204u));
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 9u;
        state.gpr[5].low64 = 9u;
        run_differential(block, state, true, 0u,
                         "BNEL not-taken differential mismatch");
    }
    {
        auto block = branch_equal_likely(
            0x0010d400u, 4u, 5u, 0x0010d500u,
            addiu(4u, 0u, 3, 0x0010d404u));
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 1u;
        state.gpr[5].low64 = 1u;
        run_differential(block, state, true, 0u,
                         "likely predicate-before-delay differential mismatch");
    }
    {
        auto block = branch_equal_likely(
            0x0010d600u, 4u, 5u, 0x0010d700u,
            store128(2u, 3u, 0, 0x0010d604u));
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 1u;
        state.gpr[5].low64 = 2u;
        state.gpr[2].low64 = 0x00005017u;
        state.gpr[3] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
        run_differential(block, state, false, 0u,
                         "annulled Store128 differential mismatch");
    }
    {
        auto block = branch_not_equal_likely(
            0x0010d800u, 4u, 5u, 0x0010d900u,
            store128(2u, 3u, 0, 0x0010d804u));
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 1u;
        state.gpr[5].low64 = 2u;
        state.gpr[2].low64 = 0x00005017u;
        state.gpr[3] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
        run_differential(block, state, false, 1u,
                         "taken failing Store128 differential mismatch");
    }

    std::cout << "r5900_x64_branch_likely_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
