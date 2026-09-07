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
    std::cerr << "r5900_x64_store32_windows_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) fail(message);
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
                           std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::Store32;
    ir.inputs = {gpr(base), gpr(source), immediate(offset)};
    return ir;
}

R5900IrInstruction load64(std::uint8_t destination,
                          std::uint8_t base,
                          std::int16_t offset,
                          std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::Load64;
    ir.destination = R5900IrDestination{destination};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
    ir.inputs = {gpr(base), immediate(offset)};
    return ir;
}

R5900IrInstruction or64(std::uint8_t destination,
                        std::uint8_t source,
                        std::uint64_t mask,
                        std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::Or64;
    ir.destination = R5900IrDestination{destination};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
    ir.inputs = {gpr(source), immediate(static_cast<std::int64_t>(mask))};
    return ir;
}

R5900IrBlock fallthrough(std::vector<R5900IrInstruction> body,
                         std::uint32_t next_pc) {
    R5900IrBlock block{};
    block.body = std::move(body);
    block.terminator.guest_pc = next_pc;
    block.terminator.fallthrough_pc = next_pc;
    return block;
}

struct Recorder {
    bool allow{true};
    std::size_t calls{};
    std::uint32_t address{};
    std::uint32_t value{};
};

bool write32(void* user, std::uint32_t address, std::uint32_t value) noexcept {
    auto& recorder = *static_cast<Recorder*>(user);
    ++recorder.calls;
    recorder.address = address;
    recorder.value = value;
    return recorder.allow;
}

R5900IrExecutionContext context_for(R5900IrExecutionState& state,
                                    Recorder& recorder) {
    R5900IrExecutionContext context{};
    context.state = &state;
    context.memory.user = &recorder;
    context.memory.write32 = &write32;
    return context;
}

void expect_fault(const R5900IrExecutionContext& context,
                  std::uint32_t pc,
                  std::uint32_t address) {
    expect(context.current_memory_guest_pc == pc &&
               context.memory_fault.active &&
               context.memory_fault.access == R5900IrMemoryAccessKind::Store &&
               context.memory_fault.guest_pc == pc &&
               context.memory_fault.address == address &&
               context.memory_fault.width_bytes == 4u,
           "native Store32 fault metadata mismatch");
}

struct Read64Recorder {
    bool allow{true};
    std::size_t calls{};
    std::uint32_t address{};
    std::uint64_t value{0x8877665544332211ull};
};

bool read64(void* user, std::uint32_t address, std::uint64_t* value) noexcept {
    auto& recorder = *static_cast<Read64Recorder*>(user);
    ++recorder.calls;
    recorder.address = address;
    if (!recorder.allow || value == nullptr) {
        return false;
    }
    *value = recorder.value;
    return true;
}

R5900IrExecutionContext load_context_for(R5900IrExecutionState& state,
                                         Read64Recorder& recorder) {
    R5900IrExecutionContext context{};
    context.state = &state;
    context.memory.user = &recorder;
    context.memory.read64 = &read64;
    return context;
}

void expect_load_fault(const R5900IrExecutionContext& context,
                       std::uint32_t pc,
                       std::uint32_t address) {
    expect(context.current_memory_guest_pc == pc &&
               context.memory_fault.active &&
               context.memory_fault.access == R5900IrMemoryAccessKind::Load &&
               context.memory_fault.guest_pc == pc &&
               context.memory_fault.address == address &&
               context.memory_fault.width_bytes == 8u,
           "native Load64 fault metadata mismatch");
}

void expect_gpr_equal(const R5900IrExecutionState& lhs,
                      const R5900IrExecutionState& rhs,
                      std::uint8_t index,
                      const char* message) {
    expect(lhs.gpr[index].low64 == rhs.gpr[index].low64 &&
               lhs.gpr[index].high64 == rhs.gpr[index].high64,
           message);
}

} // namespace

int main() {
    struct AddressCase {
        std::uint64_t base;
        std::int16_t offset;
        std::uint32_t expected;
    };

    for (const auto test : {
             AddressCase{0x9999000001ffffa0ull, 0x28, 0x01ffffc8u},
             AddressCase{0x1111222200000004ull, -8, 0xfffffffcu},
             AddressCase{0xfffffffcu, 8, 0x00000004u},
             AddressCase{0x1001u, 3, 0x00001004u}}) {
        const auto block = fallthrough(
            {store32(1u, 2u, test.offset, 0x00114ee0u)}, 0x00114ee4u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "native Store32 block must compile");

        R5900IrExecutionState native_state{};
        native_state.gpr[1].low64 = test.base;
        native_state.gpr[2] = {0x1122334455667788ull, 0x8877665544332211ull};
        auto reference_state = native_state;
        Recorder native_recorder{}, reference_recorder{};
        auto native_context = context_for(native_state, native_recorder);
        auto reference_context = context_for(reference_state, reference_recorder);
        native_context.memory_fault.active = true;

        const auto native_result = compiled.block->execute(native_context);
        const auto reference_result = execute_r5900_ir_block(block, reference_context);
        expect(native_result.ok() && reference_result.ok() &&
                   native_result.next_pc == 0x00114ee4u &&
                   native_result.next_pc == reference_result.next_pc,
               "native/reference Store32 next PC mismatch");
        expect(native_recorder.calls == 1u && reference_recorder.calls == 1u &&
                   native_recorder.address == test.expected &&
                   native_recorder.address == reference_recorder.address &&
                   native_recorder.value == 0x55667788u &&
                   native_recorder.value == reference_recorder.value,
               "native/reference Store32 address/value mismatch");
        expect(!native_context.memory_fault.active &&
                   native_context.current_memory_guest_pc == 0x00114ee0u,
               "native Store32 success must clear stale fault and publish PC");
        expect(native_state.gpr[1].low64 == reference_state.gpr[1].low64 &&
                   native_state.gpr[1].high64 == reference_state.gpr[1].high64 &&
                   native_state.gpr[2].low64 == reference_state.gpr[2].low64 &&
                   native_state.gpr[2].high64 == reference_state.gpr[2].high64,
               "native/reference Store32 CPU preservation mismatch");
    }

    for (const auto address : {0x1001u, 0x1002u, 0x1003u}) {
        const auto block = fallthrough(
            {store32(1u, 2u, 0, 0x00114ef0u)}, 0x00114ef4u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "misaligned native Store32 must compile");
        R5900IrExecutionState state{};
        state.gpr[1].low64 = address;
        Recorder recorder{};
        auto context = context_for(state, recorder);
        const auto result = compiled.block->execute(context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure &&
                   recorder.calls == 0u,
               "misaligned native Store32 must fail before callback");
        expect_fault(context, 0x00114ef0u, address);
    }

    {
        const auto block = fallthrough(
            {store32(1u, 2u, 0, 0x00114ef8u)}, 0x00114efcu);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "callback-rejection Store32 must compile");
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x1004u;
        state.gpr[2].low64 = 0x1122334455667788ull;
        Recorder recorder{};
        recorder.allow = false;
        auto context = context_for(state, recorder);
        const auto result = compiled.block->execute(context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure &&
                   recorder.calls == 1u && recorder.value == 0x55667788u,
               "native Store32 callback rejection mismatch");
        expect_fault(context, 0x00114ef8u, 0x1004u);
    }

    {
        const auto block = fallthrough(
            {store32(0u, 0u, 4, 0x00114f00u)}, 0x00114f04u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "native GPR0 Store32 must compile");
        R5900IrExecutionState state{};
        state.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
        Recorder recorder{};
        auto context = context_for(state, recorder);
        const auto result = compiled.block->execute(context);
        expect(result.ok() && recorder.address == 4u && recorder.value == 0u,
               "native Store32 must normalize architectural GPR0");
    }

    // Load64 native/reference success: exact wrapped address, low64 commit and high64 preservation.
    for (const auto test : {
             AddressCase{0x9999000001ffffa0ull, 0x40, 0x01ffffe0u},
             AddressCase{0x0000000000000008ull, -16, 0xfffffff8u},
             AddressCase{0x00000000fffffff8ull, 16, 0x00000008u}}) {
        const auto block = fallthrough(
            {load64(31u, 1u, test.offset, 0x00114f08u)}, 0x00114f0cu);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "native Load64 block must compile");

        R5900IrExecutionState native_state{};
        native_state.gpr[1].low64 = test.base;
        native_state.gpr[31] = {0x1111111111111111ull, 0xaabbccddeeff0011ull};
        auto reference_state = native_state;
        Read64Recorder native_recorder{}, reference_recorder{};
        auto native_context = load_context_for(native_state, native_recorder);
        auto reference_context = load_context_for(reference_state, reference_recorder);
        native_context.memory_fault.active = true;

        const auto native_result = compiled.block->execute(native_context);
        const auto reference_result = execute_r5900_ir_block(block, reference_context);
        expect(native_result.ok() && reference_result.ok() &&
                   native_result.next_pc == reference_result.next_pc &&
                   native_result.next_pc == 0x00114f0cu,
               "native/reference Load64 next PC mismatch");
        expect(native_recorder.calls == 1u && reference_recorder.calls == 1u &&
                   native_recorder.address == test.expected &&
                   native_recorder.address == reference_recorder.address,
               "native/reference Load64 effective address mismatch");
        expect_gpr_equal(native_state, reference_state, 31u,
                         "native/reference Load64 destination mismatch");
        expect(native_state.gpr[31].low64 == native_recorder.value &&
                   native_state.gpr[31].high64 == 0xaabbccddeeff0011ull,
               "native Load64 must replace low64 and preserve high64");
        expect(!native_context.memory_fault.active &&
                   native_context.current_memory_guest_pc == 0x00114f08u,
               "native Load64 success must clear stale fault and publish PC");
    }

    // Misaligned native Load64 must fault before callback and not mutate destination.
    for (const auto address : {0x1001u, 0x1004u, 0x1007u}) {
        const auto block = fallthrough(
            {load64(5u, 1u, 0, 0x00114f10u)}, 0x00114f14u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "misaligned native Load64 must compile");
        R5900IrExecutionState state{};
        state.gpr[1].low64 = address;
        state.gpr[5] = {0x123456789abcdef0ull, 0x0fedcba987654321ull};
        const auto before = state.gpr[5];
        Read64Recorder recorder{};
        auto context = load_context_for(state, recorder);
        const auto result = compiled.block->execute(context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure &&
                   recorder.calls == 0u &&
                   state.gpr[5].low64 == before.low64 &&
                   state.gpr[5].high64 == before.high64,
               "misaligned native Load64 must fail transactionally");
        expect_load_fault(context, 0x00114f10u, address);
    }

    // Native callback rejection must preserve destination and stop later IR.
    {
        const auto block = fallthrough(
            {load64(5u, 1u, 0, 0x00114f18u),
             or64(6u, 6u, 0xffu, 0x00114f1cu)}, 0x00114f20u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "callback-rejection Load64 block must compile");
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x3018u;
        state.gpr[5] = {0x0102030405060708ull, 0x1112131415161718ull};
        state.gpr[6] = {0x100u, 0x9988776655443322ull};
        const auto before5 = state.gpr[5];
        const auto before6 = state.gpr[6];
        Read64Recorder recorder{};
        recorder.allow = false;
        auto context = load_context_for(state, recorder);
        const auto result = compiled.block->execute(context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure &&
                   recorder.calls == 1u &&
                   state.gpr[5].low64 == before5.low64 &&
                   state.gpr[5].high64 == before5.high64 &&
                   state.gpr[6].low64 == before6.low64 &&
                   state.gpr[6].high64 == before6.high64,
               "rejected native Load64 must be transactional and stop later IR");
        expect_load_fault(context, 0x00114f18u, 0x3018u);
    }

    // Missing read bridge must fail safely without callback/destination mutation.
    {
        const auto block = fallthrough(
            {load64(5u, 1u, 0, 0x00114f24u)}, 0x00114f28u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "missing-callback Load64 block must compile");
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x2008u;
        state.gpr[5] = {0x1011121314151617ull, 0x18191a1b1c1d1e1full};
        const auto before = state.gpr[5];
        Read64Recorder recorder{};
        auto context = load_context_for(state, recorder);
        context.memory.read64 = nullptr;
        const auto result = compiled.block->execute(context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure &&
                   recorder.calls == 0u &&
                   state.gpr[5].low64 == before.low64 &&
                   state.gpr[5].high64 == before.high64,
               "missing native Load64 callback must fail safely");
        expect_load_fault(context, 0x00114f24u, 0x2008u);
    }

    // LD $zero remains observable natively but discards the loaded value.
    {
        const auto block = fallthrough(
            {load64(0u, 0u, 8, 0x00114f2cu)}, 0x00114f30u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "native Load64 to GPR0 must compile");
        R5900IrExecutionState state{};
        state.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
        Read64Recorder recorder{};
        auto context = load_context_for(state, recorder);
        const auto result = compiled.block->execute(context);
        expect(result.ok() && recorder.calls == 1u && recorder.address == 8u &&
                   state.gpr[0].low64 == 0u && state.gpr[0].high64 == 0u,
               "native Load64 to GPR0 must read then discard result");
    }

    std::cout << "r5900_x64_store32_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
