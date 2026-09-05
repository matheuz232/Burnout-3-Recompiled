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
    std::cerr << "r5900_x64_indirect_transfer_windows_tests: FAIL: "
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
           "indirect-transfer block must compile natively");
    const auto native_result = compiled.block->execute(native_context);

    expect(reference_result.error == native_result.error,
           "reference/native indirect error mismatch");
    if (reference_result.ok()) {
        expect(reference_result.next_pc == native_result.next_pc,
               "reference/native indirect next-PC mismatch");
    }
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
} // namespace

int main() {
    {
        auto state = sentinel_state();
        state.gpr[5] = {0xabcdef0012345678ull, 0xfeedfacefeedfaceull};
        const auto block = indirect_jump(
            0x0010a000u, 5u, addiu(5u, 0u, 9, 0x0010a004u));
        run_differential(block, state, true,
                         "JR snapshot/high64 differential mismatch");
    }

    {
        auto state = sentinel_state();
        state.gpr[5].low64 = 0x0010b000u;
        state.gpr[9] = {0xdeadbeefdeadbeefull, 0x0123456789abcdefull};
        const auto block = indirect_call(
            0x0010a200u, 5u, 9u, addiu(6u, 9u, 0, 0x0010a204u));
        run_differential(block, state, true,
                         "JALR ordinary link-read differential mismatch");
    }

    {
        auto state = sentinel_state();
        state.gpr[5] = {0x0010c000u, 0xfedcba9876543210ull};
        const auto block = indirect_call(
            0x0010a400u, 5u, 5u, addiu(6u, 5u, 0, 0x0010a404u));
        run_differential(block, state, true,
                         "JALR rd==rs differential mismatch");
    }

    {
        auto state = sentinel_state();
        state.gpr[7].low64 = 0x0010d000u;
        const auto block = indirect_call(
            0x0010a600u, 7u, 0u, nop(0x0010a604u));
        run_differential(block, state, true,
                         "JALR rd==0 differential mismatch");
    }

    {
        auto state = sentinel_state();
        state.gpr[8].low64 = 0x0010e000u;
        state.gpr[9] = {0x7777u, 0x9999aaaabbbbccccull};
        auto block = indirect_call(
            0x0010a800u, 8u, 9u, addiu(9u, 0u, 3, 0x0010a804u));
        block.body = {addiu(24u, 0u, 0x55, 0x0010a7fcu)};
        run_differential(block, state, true,
                         "JALR body/link-overwrite differential mismatch");
    }

    {
        auto state = sentinel_state();
        state.gpr[5].low64 = 0x0010f000u;
        state.gpr[2].low64 = 0x00004017u;
        state.gpr[3] = {0x0123456789abcdefull, 0xfedcba9876543210ull};
        const auto block = indirect_jump(
            0x0010aa00u, 5u, store128(2u, 3u, 0, 0x0010aa04u));
        run_differential(block, state, true,
                         "JR Store128-delay differential mismatch");
    }

    {
        auto state = sentinel_state();
        state.gpr[5].low64 = 0x00110000u;
        state.gpr[2].low64 = 0x00005017u;
        state.gpr[3] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
        state.gpr[9] = {0x9999888877776666ull, 0x123456789abcdef0ull};
        const auto block = indirect_call(
            0x0010ac00u, 5u, 9u, store128(2u, 3u, 0, 0x0010ac04u));

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
               "failing JALR Store128-delay block must compile");
        const auto native_result = compiled.block->execute(native_context);

        expect(reference_result.error == R5900IrExecutionError::MemoryAccessFailure &&
                   native_result.error == R5900IrExecutionError::MemoryAccessFailure,
               "failing JALR delay must report MemoryAccessFailure");
        expect(reference_state.gpr[9].low64 == 0x0010ac08u &&
                   native_state.gpr[9].low64 == 0x0010ac08u,
               "JALR link must commit before failing delay");
        expect(reference_state.gpr[9].high64 == 0x123456789abcdef0ull &&
                   native_state.gpr[9].high64 == 0x123456789abcdef0ull,
               "failing JALR delay must preserve link high64");
        expect_states_equal(reference_state, native_state,
                            "failing JALR delay state mismatch");
        expect_faults_equal(reference_context.memory_fault,
                            native_context.memory_fault,
                            "failing JALR delay fault mismatch");
    }

    std::cout << "r5900_x64_indirect_transfer_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
