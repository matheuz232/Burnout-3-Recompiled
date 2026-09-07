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

    std::cout << "r5900_x64_store32_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
