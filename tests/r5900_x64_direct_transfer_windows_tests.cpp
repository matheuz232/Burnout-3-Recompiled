#include "r5900_direct_transfer_test_support.h"
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
    std::cerr << "r5900_x64_direct_transfer_windows_tests: FAIL: " << message << '\n';
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

void expect_states_equal(const R5900IrExecutionState& lhs,
                         const R5900IrExecutionState& rhs,
                         const char* message) {
    for (std::size_t i = 0; i < lhs.gpr.size(); ++i) {
        if (lhs.gpr[i].low64 != rhs.gpr[i].low64 ||
            lhs.gpr[i].high64 != rhs.gpr[i].high64) fail(message);
    }
    if (lhs.hi != rhs.hi || lhs.lo != rhs.lo ||
        lhs.hi1 != rhs.hi1 || lhs.lo1 != rhs.lo1 || lhs.sa != rhs.sa ||
        lhs.fcr31 != rhs.fcr31 || lhs.fp_acc != rhs.fp_acc) fail(message);
    for (std::size_t i = 0; i < lhs.fpr.size(); ++i) {
        if (lhs.fpr[i] != rhs.fpr[i]) fail(message);
    }
}

void expect_faults_equal(const R5900IrMemoryFault& lhs,
                         const R5900IrMemoryFault& rhs,
                         const char* message) {
    expect(lhs.active == rhs.active && lhs.access == rhs.access &&
               lhs.guest_pc == rhs.guest_pc && lhs.address == rhs.address &&
               lhs.width_bytes == rhs.width_bytes,
           message);
}

void run_differential(const R5900IrBlock& block,
                      const R5900IrExecutionState& initial,
                      bool memory_succeeds,
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
           "direct-transfer block must compile natively");
    const auto native_result = compiled.block->execute(native_context);

    expect(reference_result.error == native_result.error &&
               reference_result.next_pc == native_result.next_pc,
           message);
    expect_states_equal(reference_state, native_state, message);
    expect(reference_probe.calls == native_probe.calls &&
               reference_probe.address == native_probe.address &&
               reference_probe.low64 == native_probe.low64 &&
               reference_probe.high64 == native_probe.high64,
           message);
    expect_faults_equal(reference_context.memory_fault,
                        native_context.memory_fault,
                        message);
}

R5900IrExecutionState sentinel_state() {
    R5900IrExecutionState state{};
    for (std::size_t i = 0; i < state.gpr.size(); ++i) {
        state.gpr[i].low64 = 0x0101010101010101ull * static_cast<std::uint64_t>(i + 1u);
        state.gpr[i].high64 = 0xf000000000000000ull | static_cast<std::uint64_t>(i);
    }
    state.gpr[0] = {};
    state.hi = 0x1111222233334444ull;
    state.lo = 0x5555666677778888ull;
    state.hi1 = 0x9999aaaabbbbccccull;
    state.lo1 = 0xddddeeeeffff0001ull;
    state.sa = 0x12u;
    for (std::size_t i = 0; i < state.fpr.size(); ++i) {
        state.fpr[i] = 0x3f000000u + static_cast<std::uint32_t>(i);
    }
    state.fcr31 = 0xa5a5c3c3u;
    state.fp_acc = 0x3f800000u;
    return state;
}
} // namespace

int main() {
    {
        auto state = sentinel_state();
        state.gpr[5].low64 = 0x0010a000u;
        const auto block = indirect_jump(0x00107c00u, 5u,
                                         nop(0x00107c04u));
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok() && compiled.block.has_value(),
               "native indirect JR block must compile");
    }
    {
        auto state = sentinel_state();
        const auto block = direct_jump(0x00108000u, 0x00108100u,
                                       addiu(7u, 7u, 1, 0x00108004u));
        run_differential(block, state, true,
                         "native/reference J+ADDIU differential mismatch");
    }
    {
        auto state = sentinel_state();
        state.gpr[31] = {0xdeadbeefdeadbeefull, 0x0123456789abcdefull};
        const auto block = direct_call(0x00108200u, 0x00108300u,
                                       addiu(23u, 31u, 0, 0x00108204u));
        run_differential(block, state, true,
                         "native/reference JAL link-read differential mismatch");
    }
    {
        auto state = sentinel_state();
        state.gpr[31] = {0x5555666677778888ull, 0xfedcba9876543210ull};
        const auto block = direct_call(0x00108400u, 0x00108500u,
                                       addiu(31u, 0u, 9, 0x00108404u));
        run_differential(block, state, true,
                         "native/reference JAL r31-delay differential mismatch");
    }
    {
        auto state = sentinel_state();
        state.gpr[2].low64 = 0x0000000000004017ull;
        state.gpr[3] = {0x0123456789abcdefull, 0xfedcba9876543210ull};
        const auto block = direct_jump(0x00108600u, 0x00108700u,
                                       store128(2u, 3u, 0, 0x00108604u));
        run_differential(block, state, true,
                         "native/reference J Store128-delay differential mismatch");
    }
    {
        auto state = sentinel_state();
        state.gpr[2].low64 = 0x0000000000005017ull;
        state.gpr[3] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
        state.gpr[31] = {0x9999888877776666ull, 0x123456789abcdef0ull};
        const auto block = direct_call(0x00108800u, 0x00108900u,
                                       store128(2u, 3u, 0, 0x00108804u));
        auto reference_state = state;
        auto native_state = state;
        MemoryProbe reference_probe{};
        MemoryProbe native_probe{};
        reference_probe.succeed = false;
        native_probe.succeed = false;
        auto reference_context = context_for(reference_state, reference_probe);
        auto native_context = context_for(native_state, native_probe);
        const auto reference_result = execute_r5900_ir_block(block, reference_context);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok() && compiled.block.has_value(),
               "failing JAL Store128-delay block must compile");
        const auto native_result = compiled.block->execute(native_context);
        expect(reference_result.error == R5900IrExecutionError::MemoryAccessFailure &&
                   native_result.error == R5900IrExecutionError::MemoryAccessFailure,
               "failing JAL delay must report MemoryAccessFailure");
        expect(reference_state.gpr[31].low64 == 0x00108808u &&
                   native_state.gpr[31].low64 == 0x00108808u,
               "JAL link must commit before failing delay in both paths");
        expect(reference_state.gpr[31].high64 == 0x123456789abcdef0ull &&
                   native_state.gpr[31].high64 == 0x123456789abcdef0ull,
               "failing JAL delay must preserve r31 high64");
        expect_states_equal(reference_state, native_state,
                            "failing JAL delay state differential mismatch");
        expect_faults_equal(reference_context.memory_fault,
                            native_context.memory_fault,
                            "failing JAL delay fault differential mismatch");
    }

    std::cout << "r5900_x64_direct_transfer_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
