#include "recompiler/r5900_ir_executor.h"
#include "recompiler/windows/r5900_x64_backend.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_x64_store64_windows_tests: FAIL: " << message << '\n';
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

struct Write64Recorder {
    bool allow{true};
    std::size_t calls32{};
    std::uint32_t address32{};
    std::uint32_t value32{};
    std::uint32_t stored32{0xaabbccddu};
    std::uint32_t address{};
    std::uint64_t value{};
    std::size_t calls{};
    std::uint64_t stored{0xaabbccddeeff0011ull};
    std::size_t calls128{};
    std::uint32_t address128{};
    std::uint64_t low128{};
    std::uint64_t high128{};
};

bool record_write32(void* user, std::uint32_t address,
                    std::uint32_t value) noexcept {
    auto& recorder = *static_cast<Write64Recorder*>(user);
    ++recorder.calls32;
    recorder.address32 = address;
    recorder.value32 = value;
    if (!recorder.allow) {
        return false;
    }
    recorder.stored32 = value;
    return true;
}

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

bool record_write128(void* user, std::uint32_t address,
                     std::uint64_t low, std::uint64_t high) noexcept {
    auto& recorder = *static_cast<Write64Recorder*>(user);
    ++recorder.calls128;
    recorder.address128 = address;
    recorder.low128 = low;
    recorder.high128 = high;
    return true;
}

R5900IrExecutionContext context_for(R5900IrExecutionState& state,
                                    Write64Recorder& recorder) {
    R5900IrExecutionContext context{};
    context.state = &state;
    context.memory.user = &recorder;
    context.memory.write32 = &record_write32;
    context.memory.write64 = &record_write64;
    context.memory.write128 = &record_write128;
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

R5900IrBlock fallthrough_block(std::vector<R5900IrInstruction> body,
                               std::uint32_t next_pc) {
    R5900IrBlock block{};
    block.body = std::move(body);
    block.terminator.guest_pc = next_pc;
    block.terminator.fallthrough_pc = next_pc;
    return block;
}

R5900IrBlock direct_call(std::vector<R5900IrInstruction> body,
                         std::uint32_t pc) {
    auto block = fallthrough_block(std::move(body), pc);
    block.terminator.kind = R5900IrTerminatorKind::DirectCall;
    block.terminator.fallthrough_pc = 0u;
    block.terminator.target_pc = 0x00114ed0u;
    block.terminator.link_pc = pc + 8u;
    R5900IrInstruction nop{};
    nop.guest_pc = pc + 4u;
    block.terminator.delay_slot = {nop};
    return block;
}

} // namespace

int main() {
    // Store32 native/reference success, signed offsets, low32-only and wrap.
    struct AddressCase32 {
        std::uint64_t base;
        std::int16_t offset;
        std::uint32_t address;
    };
    for (const auto test : {
             AddressCase32{0x9999000001ffffa0ull, 0x28, 0x01ffffc8u},
             AddressCase32{0x1111222200000004ull, -8, 0xfffffffcu},
             AddressCase32{0xfffffffcu, 8, 0x00000004u},
             AddressCase32{0x1001u, 3, 0x1004u}}) {
        const auto block = fallthrough_block(
            {store32(1u, 2u, test.offset, 0x00114ee0u)}, 0x00114ee4u);
        auto compiled = compile_r5900_ir_x64(block);
        if (!compiled.ok()) std::cerr << compiled.message << '\n';
        expect(compiled.ok(), "native Store32 block must compile");
        auto native_state = seeded_state();
        native_state.gpr[1].low64 = test.base;
        native_state.gpr[2] = {0x1122334455667788ull, 0x8877665544332211ull};
        const auto before = native_state;
        auto reference_state = native_state;
        Write64Recorder native_memory{}, reference_memory{};
        auto native_context = context_for(native_state, native_memory);
        auto reference_context = context_for(reference_state, reference_memory);
        native_context.memory_fault.active = true;
        const auto native_result = compiled.block->execute(native_context);
        const auto reference_result = execute_r5900_ir_block(block, reference_context);
        expect(native_result.ok() && reference_result.ok() &&
                   native_result.next_pc == reference_result.next_pc &&
                   native_result.next_pc == 0x00114ee4u,
               "native/reference Store32 must reach the same fallthrough");
        expect(native_memory.calls32 == 1u && reference_memory.calls32 == 1u &&
                   native_memory.address32 == test.address &&
                   native_memory.address32 == reference_memory.address32 &&
                   native_memory.stored32 == 0x55667788u &&
                   native_memory.stored32 == reference_memory.stored32,
               "native Store32 must match reference address and low32 write");
        expect(!native_context.memory_fault.active &&
                   native_context.current_memory_guest_pc == 0x00114ee0u,
               "native Store32 success must publish PC and clear stale fault");
        expect_same_state(before, native_state);
        expect_same_state(reference_state, native_state);
    }

    // Store32 failure must stop before following arithmetic/call.
    for (const auto failure : {0u, 1u, 2u, 3u}) {
        auto block = direct_call(
            {store32(1u, 2u, 0, 0x00114ef0u),
             or64(6u, 6u, 0xffu, 0x00114ef4u)}, 0x00114ef8u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "faulting Store32 block must compile");
        auto native_state = seeded_state();
        const auto address = failure == 0u ? 0x1002u : 0x1004u;
        native_state.gpr[1].low64 = address;
        const auto before = native_state;
        auto reference_state = native_state;
        Write64Recorder native_memory{}, reference_memory{};
        auto native_context = context_for(native_state, native_memory);
        auto reference_context = context_for(reference_state, reference_memory);
        if (failure == 1u) {
            native_context.memory.write32 = nullptr;
            reference_context.memory.write32 = nullptr;
        } else if (failure == 2u) {
            native_context.memory.user = nullptr;
            reference_context.memory.user = nullptr;
        } else if (failure == 3u) {
            native_memory.allow = false;
            reference_memory.allow = false;
        }
        const auto native_result = compiled.block->execute(native_context);
        const auto reference_result = execute_r5900_ir_block(block, reference_context);
        expect(native_result.error == R5900IrExecutionError::MemoryAccessFailure &&
                   native_result.error == reference_result.error &&
                   native_result.next_pc == 0u,
               "native/reference Store32 failure must terminate the block");
        expect_fault32(native_context, 0x00114ef0u, address);
        expect_fault32(reference_context, 0x00114ef0u, address);
        expect(native_memory.calls32 == (failure == 3u ? 1u : 0u) &&
                   native_memory.calls32 == reference_memory.calls32 &&
                   native_memory.stored32 == 0xaabbccddu &&
                   native_memory.stored32 == reference_memory.stored32,
               "native failed Store32 must preserve memory/callback ordering");
        expect_same_state(before, native_state);
        expect_same_state(reference_state, native_state);
    }

    struct AddressCase {
        std::uint64_t base;
        std::int16_t offset;
        std::uint32_t address;
    };
    for (const auto test : {
             AddressCase{0x01fffff0u, 0, 0x01fffff0u},
             AddressCase{0x9999000000000008ull, -16, 0xfffffff8u},
             AddressCase{0xfffffff8u, 16, 8u},
             AddressCase{0x1010u, -8, 0x1008u},
             AddressCase{0x1001u, 7, 0x1008u}}) {
        const auto block = fallthrough_block(
            {store64(29u, 31u, test.offset, 0x0011510cu)}, 0x00115110u);
        auto compiled = compile_r5900_ir_x64(block);
        if (!compiled.ok()) std::cerr << compiled.message << '\n';
        expect(compiled.ok(), "native Store64 block must compile");
        auto native_state = seeded_state();
        native_state.gpr[29].low64 = test.base;
        native_state.gpr[31].low64 = 0x001001f0u;
        const auto before = native_state;
        auto reference_state = native_state;
        Write64Recorder native_memory{}, reference_memory{};
        auto native_context = context_for(native_state, native_memory);
        auto reference_context = context_for(reference_state, reference_memory);
        native_context.memory_fault.active = true;
        const auto native_result = compiled.block->execute(native_context);
        const auto reference_result = execute_r5900_ir_block(block, reference_context);
        expect(native_result.ok() && reference_result.ok() &&
                   native_result.next_pc == 0x00115110u &&
                   native_result.next_pc == reference_result.next_pc,
               "native/reference Store64 must reach the same fallthrough");
        expect(native_memory.calls == 1u && reference_memory.calls == 1u &&
                   native_memory.address == test.address &&
                   native_memory.address == reference_memory.address &&
                   native_memory.stored == 0x001001f0u &&
                   native_memory.stored == reference_memory.stored,
               "native Store64 must match reference address and low64 write");
        expect(!native_context.memory_fault.active &&
                   native_context.current_memory_guest_pc == 0x0011510cu,
               "native success must clear stale faults and publish the store PC");
        expect_same_state(before, native_state);
        expect_same_state(reference_state, native_state);
    }

    // Every failure must stop before both the following OR and the JAL link.
    for (const auto failure : {0u, 1u, 2u, 3u}) {
        auto block = direct_call(
            {store64(1u, 2u, 0, 0x00115120u),
             or64(6u, 6u, 0xffu, 0x00115124u)}, 0x00115128u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "faulting Store64 block must compile");
        auto native_state = seeded_state();
        const auto address = failure == 0u ? 0x1003u : 0x1008u;
        native_state.gpr[1].low64 = address;
        const auto before = native_state;
        auto reference_state = native_state;
        Write64Recorder native_memory{}, reference_memory{};
        auto native_context = context_for(native_state, native_memory);
        auto reference_context = context_for(reference_state, reference_memory);
        if (failure == 1u) {
            native_context.memory.write64 = nullptr;
            reference_context.memory.write64 = nullptr;
        } else if (failure == 2u) {
            native_context.memory.user = nullptr;
            reference_context.memory.user = nullptr;
        } else if (failure == 3u) {
            native_memory.allow = false;
            reference_memory.allow = false;
        }
        const auto native_result = compiled.block->execute(native_context);
        const auto reference_result = execute_r5900_ir_block(block, reference_context);
        expect(native_result.error == R5900IrExecutionError::MemoryAccessFailure &&
                   native_result.error == reference_result.error &&
                   native_result.next_pc == 0u,
               "native/reference Store64 failure must terminate the block");
        expect_fault(native_context, 0x00115120u, address);
        expect_fault(reference_context, 0x00115120u, address);
        expect(native_memory.calls == (failure == 3u ? 1u : 0u) &&
                   native_memory.calls == reference_memory.calls &&
                   native_memory.stored == 0xaabbccddeeff0011ull &&
                   native_memory.stored == reference_memory.stored,
               "native failed Store64 must preserve memory and callback ordering");
        expect_same_state(before, native_state);
        expect_same_state(reference_state, native_state);
    }

    for (const bool wide_first : {false, true}) {
        auto wide = store64(3u, 4u, 0, 0x00115204u);
        wide.opcode = R5900IrOpcode::Store128;
        auto narrow = store64(1u, 2u, -8, 0x00115200u);
        if (wide_first) { wide.guest_pc = 0x00115200u; narrow.guest_pc = 0x00115204u; }
        auto block = fallthrough_block(
            wide_first ? std::vector<R5900IrInstruction>{wide, narrow}
                       : std::vector<R5900IrInstruction>{narrow, wide},
            0x0011520cu);
        block.body.push_back(or64(6u, 6u, 0xffu, 0x00115208u));
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "mixed Store64/Store128 helper-frame block must compile");
        auto native_state = seeded_state();
        native_state.gpr[1].low64 = 0x1008u;
        native_state.gpr[2].low64 = 0x1122334455667788ull;
        native_state.gpr[3].low64 = 0x200fu;
        native_state.gpr[4] = {0x0123456789abcdefull, 0xfedcba9876543210ull};
        auto reference_state = native_state;
        Write64Recorder native_memory{}, reference_memory{};
        auto native_context = context_for(native_state, native_memory);
        auto reference_context = context_for(reference_state, reference_memory);
        const auto native_result = compiled.block->execute(native_context);
        const auto reference_result = execute_r5900_ir_block(block, reference_context);
        expect(native_result.ok() && reference_result.ok() &&
                   native_result.next_pc == reference_result.next_pc,
               "mixed stores must execute through the shared helper frame");
        expect(native_memory.calls == 1u && native_memory.calls128 == 1u &&
                   reference_memory.calls == 1u && reference_memory.calls128 == 1u &&
                   native_memory.address == 0x1000u &&
                   native_memory.stored == 0x1122334455667788ull &&
                   native_memory.address128 == 0x2000u &&
                   native_memory.low128 == 0x0123456789abcdefull &&
                   native_memory.high128 == 0xfedcba9876543210ull,
               "mixed helpers must retain Store64 and Store128 width/address semantics");
        expect(native_state.gpr[6].low64 == 0x12340000000000ffull,
               "state pointer must survive both helper calls for following arithmetic");
        expect_same_state(reference_state, native_state);
    }

    {
        const auto block = fallthrough_block(
            {store64(0u, 0u, 8, 0x00115210u)}, 0x00115214u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "native GPR0 Store64 must compile");
        R5900IrExecutionState state{};
        state.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
        Write64Recorder memory{};
        auto context = context_for(state, memory);
        expect(compiled.block->execute(context).ok() &&
                   memory.address == 8u && memory.stored == 0u,
               "native Store64 must normalize architectural GPR0 base and value");
    }

    std::cout << "r5900_x64_store64_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
