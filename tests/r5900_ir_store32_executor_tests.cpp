#include "recompiler/r5900_ir_executor.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_store32_executor_tests: FAIL: " << message << '\n';
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
           "Store32 fault metadata mismatch");
}

} // namespace

int main() {
    {
        R5900IrExecutionState state{};
        state.gpr[29] = {0x9999000001ffffa0ull, 0x1111222233334444ull};
        state.gpr[2] = {0xdeadbeef00000001ull, 0xaaaabbbbccccddddull};
        const auto before = state;
        Recorder recorder{};
        auto context = context_for(state, recorder);
        context.memory_fault.active = true;
        const auto result = execute_r5900_ir(
            {store32(29u, 2u, 0x28, 0x00114ee0u)}, context);
        expect(result.ok() && recorder.calls == 1u &&
                   recorder.address == 0x01ffffc8u && recorder.value == 1u,
               "Store32 must use low32 base/source and signed offset");
        expect(!context.memory_fault.active &&
                   context.current_memory_guest_pc == 0x00114ee0u,
               "Store32 success must clear stale fault and publish PC");
        expect(state.gpr[29].low64 == before.gpr[29].low64 &&
                   state.gpr[29].high64 == before.gpr[29].high64 &&
                   state.gpr[2].low64 == before.gpr[2].low64 &&
                   state.gpr[2].high64 == before.gpr[2].high64,
               "Store32 must preserve source/base GPRs");
    }

    struct AddressCase {
        std::uint64_t base;
        std::int16_t offset;
        std::uint32_t expected;
    };
    for (const auto test : {
             AddressCase{0x1111222200000004ull, -8, 0xfffffffcu},
             AddressCase{0xfffffffcu, 8, 0x00000004u},
             AddressCase{0x1001u, 3, 0x00001004u}}) {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = test.base;
        state.gpr[2].low64 = 0x1122334455667788ull;
        Recorder recorder{};
        auto context = context_for(state, recorder);
        const auto result = execute_r5900_ir(
            {store32(1u, 2u, test.offset, 0x00114ef0u)}, context);
        expect(result.ok() && recorder.calls == 1u &&
                   recorder.address == test.expected &&
                   recorder.value == 0x55667788u,
               "Store32 wrap/low32 semantics mismatch");
    }

    for (const auto address : {0x1001u, 0x1002u, 0x1003u}) {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = address;
        Recorder recorder{};
        auto context = context_for(state, recorder);
        const auto result = execute_r5900_ir(
            {store32(1u, 2u, 0, 0x00114ef4u)}, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure &&
                   recorder.calls == 0u,
               "misaligned Store32 must fail before callback");
        expect_fault(context, 0x00114ef4u, address);
    }

    {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x1004u;
        Recorder recorder{};
        auto context = context_for(state, recorder);
        context.memory.write32 = nullptr;
        const auto result = execute_r5900_ir(
            {store32(1u, 2u, 0, 0x00114ef8u)}, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure &&
                   recorder.calls == 0u,
               "missing Store32 callback must fail safely");
        expect_fault(context, 0x00114ef8u, 0x1004u);
    }

    {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x1004u;
        Recorder recorder{};
        recorder.allow = false;
        auto context = context_for(state, recorder);
        const auto result = execute_r5900_ir(
            {store32(1u, 2u, 0, 0x00114efcu)}, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure &&
                   recorder.calls == 1u,
               "rejected Store32 callback must fail after one attempt");
        expect_fault(context, 0x00114efcu, 0x1004u);
    }

    {
        R5900IrExecutionState state{};
        state.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
        Recorder recorder{};
        auto context = context_for(state, recorder);
        R5900IrBlock block{};
        block.body = {store32(0u, 0u, 4, 0x00114f00u)};
        block.terminator.guest_pc = 0x00114f04u;
        block.terminator.fallthrough_pc = 0x00114f04u;
        const auto result = execute_r5900_ir_block(block, context);
        expect(result.ok() && recorder.address == 4u && recorder.value == 0u,
               "Store32 must honor architectural GPR0");
    }

    std::cout << "r5900_ir_store32_executor_tests: PASS\n";
    return EXIT_SUCCESS;
}
