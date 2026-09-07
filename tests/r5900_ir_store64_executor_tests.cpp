#include "recompiler/r5900_ir_executor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_store64_executor_tests: FAIL: " << message << '\n';
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

R5900IrInstruction store32(std::uint8_t base,
                           std::uint8_t source,
                           std::int16_t offset,
                           std::uint32_t guest_pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = guest_pc;
    ir.opcode = R5900IrOpcode::Store32;
    ir.inputs = {gpr(base), gpr(source), immediate(offset)};
    return ir;
}

R5900IrInstruction store64(std::uint8_t base,
                            std::uint8_t source,
                            std::int16_t offset,
                            std::uint32_t guest_pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = guest_pc;
    ir.opcode = R5900IrOpcode::Store64;
    ir.inputs = {gpr(base), gpr(source), immediate(offset)};
    return ir;
}

R5900IrInstruction or64(std::uint8_t destination,
                        std::uint8_t source,
                        std::uint64_t mask,
                        std::uint32_t guest_pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = guest_pc;
    ir.opcode = R5900IrOpcode::Or64;
    ir.destination = R5900IrDestination{destination};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
    ir.inputs = {gpr(source), immediate(static_cast<std::int64_t>(mask))};
    return ir;
}

struct Write32Recorder {
    bool allow{true};
    std::uint32_t address{};
    std::uint32_t value{};
    std::size_t calls{};
    std::array<std::uint8_t, 12> bytes{
        0xa0u, 0xa1u, 0xa2u, 0xa3u,
        0xb0u, 0xb1u, 0xb2u, 0xb3u,
        0xc0u, 0xc1u, 0xc2u, 0xc3u};
};

bool record_write32(void* user, std::uint32_t address,
                    std::uint32_t value) noexcept {
    auto& recorder = *static_cast<Write32Recorder*>(user);
    ++recorder.calls;
    recorder.address = address;
    recorder.value = value;
    if (!recorder.allow) {
        return false;
    }
    recorder.bytes[4] = static_cast<std::uint8_t>(value & 0xffu);
    recorder.bytes[5] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    recorder.bytes[6] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    recorder.bytes[7] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
    return true;
}

R5900IrExecutionContext context_for32(R5900IrExecutionState& state,
                                      Write32Recorder& recorder) {
    R5900IrExecutionContext context{};
    context.state = &state;
    context.memory.user = &recorder;
    context.memory.write32 = &record_write32;
    return context;
}

struct Write64Recorder {
    bool allow{true};
    std::uint32_t address{};
    std::uint64_t value{};
    std::size_t calls{};
    std::uint64_t stored{0xaabbccddeeff0011ull};
};

bool record_write64(void* user, std::uint32_t address,
                    std::uint64_t value) noexcept {
    auto& recorder = *static_cast<Write64Recorder*>(user);
    ++recorder.calls;
    recorder.address = address;
    recorder.value = value;
    if (!recorder.allow) {
        return false;
    }
    recorder.stored = value;
    return true;
}

R5900IrExecutionContext context_for(R5900IrExecutionState& state,
                                    Write64Recorder& recorder) {
    R5900IrExecutionContext context{};
    context.state = &state;
    context.memory.user = &recorder;
    context.memory.write64 = &record_write64;
    return context;
}

R5900IrExecutionState seeded_state() {
    R5900IrExecutionState state{};
    for (std::size_t i = 1u; i < state.gpr.size(); ++i) {
        state.gpr[i] = {0x1234000000000000ull + i, 0xabcd000000000000ull + i};
    }
    state.hi = 11u; state.lo = 22u; state.hi1 = 33u; state.lo1 = 44u;
    state.sa = 55u; state.fcr31 = 66u; state.fp_acc = 77u;
    for (std::size_t i = 0u; i < state.fpr.size(); ++i) {
        state.fpr[i] = 0x3f000000u + static_cast<std::uint32_t>(i);
    }
    return state;
}

void expect_same_state(const R5900IrExecutionState& before,
                       const R5900IrExecutionState& after) {
    for (std::size_t i = 0u; i < before.gpr.size(); ++i) {
        expect(before.gpr[i].low64 == after.gpr[i].low64 &&
                   before.gpr[i].high64 == after.gpr[i].high64,
               "store must preserve every GPR");
    }
    expect(before.hi == after.hi && before.lo == after.lo &&
               before.hi1 == after.hi1 && before.lo1 == after.lo1 &&
               before.sa == after.sa && before.fpr == after.fpr &&
               before.fcr31 == after.fcr31 && before.fp_acc == after.fp_acc,
           "store must preserve all special and FPU state");
}

void expect_fault32(const R5900IrExecutionContext& context,
                    std::uint32_t pc, std::uint32_t address) {
    expect(context.current_memory_guest_pc == pc &&
               context.memory_fault.active &&
               context.memory_fault.access == R5900IrMemoryAccessKind::Store &&
               context.memory_fault.guest_pc == pc &&
               context.memory_fault.address == address &&
               context.memory_fault.width_bytes == 4u,
           "Store32 fault must retain exact PC, address and width 4");
}

void expect_fault(const R5900IrExecutionContext& context,
                  std::uint32_t pc, std::uint32_t address) {
    expect(context.current_memory_guest_pc == pc &&
               context.memory_fault.active &&
               context.memory_fault.access == R5900IrMemoryAccessKind::Store &&
               context.memory_fault.guest_pc == pc &&
               context.memory_fault.address == address &&
               context.memory_fault.width_bytes == 8u,
           "Store64 fault must retain exact PC, unmasked address and width 8");
}

} // namespace

int main() {
    // Store32 success: exact low32 write, exact width, no CPU mutation.
    {
        auto state = seeded_state();
        state.gpr[29] = {0x9999000001ffffa0ull, 0x2929292929292929ull};
        state.gpr[2] = {0xdeadbeef00000001ull, 0xfefefefefefefefeull};
        const auto before = state;
        Write32Recorder recorder{};
        auto context = context_for32(state, recorder);
        context.memory_fault.active = true;
        const auto result = execute_r5900_ir(
            {store32(29u, 2u, 0x28, 0x00114ee0u)}, context);
        expect(result.ok(), "aligned Store32 must execute");
        expect(recorder.calls == 1u && recorder.address == 0x01ffffc8u &&
                   recorder.value == 1u,
               "Store32 must use low32 base and source");
        expect(recorder.bytes[3] == 0xa3u &&
                   recorder.bytes[4] == 0x01u && recorder.bytes[5] == 0x00u &&
                   recorder.bytes[6] == 0x00u && recorder.bytes[7] == 0x00u &&
                   recorder.bytes[8] == 0xc0u,
               "Store32 must mutate exactly four little-endian bytes");
        expect(context.current_memory_guest_pc == 0x00114ee0u &&
                   !context.memory_fault.active,
               "Store32 success must clear stale fault and publish current PC");
        expect_same_state(before, state);
    }

    struct AddressCase32 {
        std::uint64_t base;
        std::int16_t offset;
        std::uint32_t address;
    };
    for (const auto test : {
             AddressCase32{0x1111222200000004ull, -8, 0xfffffffcu},
             AddressCase32{0xfffffffcu, 8, 0x00000004u},
             AddressCase32{0x1010u, -4, 0x100cu},
             AddressCase32{0x1001u, 3, 0x1004u}}) {
        auto state = seeded_state();
        state.gpr[1].low64 = test.base;
        state.gpr[2] = {0x1122334455667788ull, 0x8877665544332211ull};
        const auto before = state;
        Write32Recorder recorder{};
        auto context = context_for32(state, recorder);
        const auto result = execute_r5900_ir(
            {store32(1u, 2u, test.offset, 0x00114ef0u)}, context);
        expect(result.ok() && recorder.calls == 1u &&
                   recorder.address == test.address &&
                   recorder.value == 0x55667788u,
               "Store32 address must wrap modulo32 and source must be low32 only");
        expect_same_state(before, state);
    }

    for (const auto address : {0x1001u, 0x1002u, 0x1003u}) {
        auto state = seeded_state();
        state.gpr[1].low64 = address;
        const auto before = state;
        Write32Recorder recorder{};
        auto context = context_for32(state, recorder);
        const auto result = execute_r5900_ir(
            {store32(1u, 2u, 0, 0x00114ef4u),
             or64(6u, 6u, 0xffu, 0x00114ef8u)}, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "misaligned Store32 must fail");
        expect(recorder.calls == 0u,
               "misaligned Store32 must not call guest memory");
        expect_fault32(context, 0x00114ef4u, address);
        expect_same_state(before, state);
    }

    for (const auto missing_user : {false, true}) {
        auto state = seeded_state();
        state.gpr[1].low64 = 0x1004u;
        const auto before = state;
        Write32Recorder recorder{};
        auto context = context_for32(state, recorder);
        if (missing_user) context.memory.user = nullptr;
        else context.memory.write32 = nullptr;
        const auto result = execute_r5900_ir(
            {store32(1u, 2u, 0, 0x00114efcu)}, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "missing user or write32 must fail safely");
        expect(recorder.calls == 0u,
               "unavailable Store32 bridge must not be called");
        expect_fault32(context, 0x00114efcu, 0x1004u);
        expect_same_state(before, state);
    }

    {
        auto state = seeded_state();
        state.gpr[1].low64 = 0x3018u;
        const auto before = state;
        Write32Recorder recorder{};
        recorder.allow = false;
        auto context = context_for32(state, recorder);
        const auto old_bytes = recorder.bytes;
        const auto result = execute_r5900_ir(
            {store32(1u, 2u, 0, 0x00114f00u),
             or64(6u, 6u, 0xffu, 0x00114f04u)}, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure &&
                   recorder.calls == 1u,
               "rejected Store32 must stop after one write attempt");
        expect(recorder.bytes == old_bytes,
               "rejected Store32 must preserve guest memory");
        expect_fault32(context, 0x00114f00u, 0x3018u);
        expect_same_state(before, state);
    }

    {
        R5900IrExecutionState state{};
        state.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
        Write32Recorder recorder{};
        auto context = context_for32(state, recorder);
        R5900IrBlock block{};
        block.body = {store32(0u, 0u, 4, 0x00114f08u)};
        block.terminator.guest_pc = 0x00114f0cu;
        block.terminator.fallthrough_pc = 0x00114f0cu;
        const auto result = execute_r5900_ir_block(block, context);
        expect(result.ok() && result.next_pc == 0x00114f0cu &&
                   recorder.address == 4u && recorder.value == 0u,
               "Store32 must honor architectural GPR0 base and value");
    }

    // Existing Store64 regression suite remains intact.
    {
        auto state = seeded_state();
        state.gpr[29].low64 = 0x01fffff0u;
        state.gpr[31].low64 = 0x001001f0u;
        const auto before = state;
        Write64Recorder recorder{};
        auto context = context_for(state, recorder);
        context.memory_fault.active = true;
        const auto result = execute_r5900_ir(
            {store64(29u, 31u, 0, 0x0011510cu)}, context);
        expect(result.ok(), "aligned stack Store64 must execute");
        expect(recorder.calls == 1u && recorder.address == 0x01fffff0u &&
                   recorder.value == 0x001001f0u && recorder.stored == 0x001001f0u,
               "Store64 must write exactly the return address low64");
        expect(context.current_memory_guest_pc == 0x0011510cu &&
                   !context.memory_fault.active,
               "success must clear stale fault and publish current PC");
        expect_same_state(before, state);
    }

    struct AddressCase {
        std::uint64_t base;
        std::int16_t offset;
        std::uint32_t address;
    };
    for (const auto test : {
             AddressCase{0x1111222200000008ull, -16, 0xfffffff8u},
             AddressCase{0xfffffff8u, 16, 0x00000008u},
             AddressCase{0x1010u, -8, 0x1008u},
             AddressCase{0x1001u, 7, 0x1008u}}) {
        auto state = seeded_state();
        state.gpr[1].low64 = test.base;
        state.gpr[2].low64 = 0x1122334455667788ull;
        const auto before = state;
        Write64Recorder recorder{};
        auto context = context_for(state, recorder);
        const auto result = execute_r5900_ir(
            {store64(1u, 2u, test.offset, 0x00115120u)}, context);
        expect(result.ok() && recorder.calls == 1u &&
                   recorder.address == test.address &&
                   recorder.stored == 0x1122334455667788ull,
               "Store64 must use low32 base plus signed16 offset modulo 32 bits");
        expect_same_state(before, state);
    }

    for (const auto address : {0x1003u, 0x01fffff4u}) {
        auto state = seeded_state();
        state.gpr[1].low64 = address;
        const auto before = state;
        Write64Recorder recorder{};
        auto context = context_for(state, recorder);
        const auto result = execute_r5900_ir(
            {store64(1u, 2u, 0, 0x00115124u),
             or64(6u, 6u, 0xffu, 0x00115128u)}, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "misaligned Store64 must fail");
        expect(recorder.calls == 0u && recorder.stored == 0xaabbccddeeff0011ull,
               "misaligned Store64 must not call or mutate memory");
        expect_fault(context, 0x00115124u, address);
        expect_same_state(before, state);
    }

    for (const auto missing_user : {false, true}) {
        auto state = seeded_state();
        state.gpr[1].low64 = 0x1008u;
        const auto before = state;
        Write64Recorder recorder{};
        auto context = context_for(state, recorder);
        if (missing_user) context.memory.user = nullptr;
        else context.memory.write64 = nullptr;
        const auto result = execute_r5900_ir(
            {store64(1u, 2u, 0, 0x00115128u)}, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "missing user or write64 must fail safely");
        expect(recorder.calls == 0u, "unavailable bridge must not be called");
        expect_fault(context, 0x00115128u, 0x1008u);
        expect_same_state(before, state);
    }

    {
        auto state = seeded_state();
        state.gpr[1].low64 = 0x3019u;
        const auto before = state;
        Write64Recorder recorder{};
        recorder.allow = false;
        auto context = context_for(state, recorder);
        const auto result = execute_r5900_ir(
            {store64(1u, 2u, -1, 0x0011512cu),
             or64(6u, 6u, 0xffu, 0x00115130u)}, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure &&
                   recorder.calls == 1u,
               "callback rejection must stop execution after one write attempt");
        expect(recorder.stored == 0xaabbccddeeff0011ull,
               "rejected store must preserve memory");
        expect_fault(context, 0x0011512cu, 0x3018u);
        expect_same_state(before, state);
    }

    {
        R5900IrExecutionState state{};
        state.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
        Write64Recorder recorder{};
        auto context = context_for(state, recorder);
        R5900IrBlock block{};
        block.body = {store64(0u, 0u, 8, 0x00115130u)};
        block.terminator.guest_pc = 0x00115134u;
        block.terminator.fallthrough_pc = 0x00115134u;
        const auto result = execute_r5900_ir_block(block, context);
        expect(result.ok() && result.next_pc == 0x00115134u &&
                   recorder.address == 8u && recorder.stored == 0u,
               "block execution must honor architectural GPR0 for base and value");
    }

    std::cout << "r5900_ir_store64_executor_tests: PASS\n";
    return EXIT_SUCCESS;
}
