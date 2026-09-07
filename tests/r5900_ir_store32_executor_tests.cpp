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
           "Load64 fault metadata mismatch");
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

    // Load64 success must use low32(base)+signed16 modulo 32 bits and preserve high64.
    for (const auto test : {
             AddressCase{0x9999000001ffffa0ull, 0x40, 0x01ffffe0u},
             AddressCase{0x0000000000000008ull, -16, 0xfffffff8u},
             AddressCase{0x00000000fffffff8ull, 16, 0x00000008u}}) {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = test.base;
        state.gpr[31] = {0x1111111111111111ull, 0xaabbccddeeff0011ull};
        Read64Recorder recorder{};
        auto context = load_context_for(state, recorder);
        context.memory_fault.active = true;
        const auto result = execute_r5900_ir(
            {load64(31u, 1u, test.offset, 0x00114f08u)}, context);
        expect(result.ok() && recorder.calls == 1u &&
                   recorder.address == test.expected &&
                   state.gpr[31].low64 == recorder.value &&
                   state.gpr[31].high64 == 0xaabbccddeeff0011ull,
               "Load64 success/address/high64 semantics mismatch");
        expect(!context.memory_fault.active &&
                   context.current_memory_guest_pc == 0x00114f08u,
               "Load64 success must publish PC and clear stale fault");
    }

    // A load into GPR0 remains observable but discards the result.
    {
        R5900IrExecutionState state{};
        state.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
        Read64Recorder recorder{};
        auto context = load_context_for(state, recorder);
        const auto result = execute_r5900_ir(
            {load64(0u, 0u, 8, 0x00114f0cu)}, context);
        expect(result.ok() && recorder.calls == 1u && recorder.address == 8u &&
                   state.gpr[0].low64 == 0u && state.gpr[0].high64 == 0u,
               "Load64 to GPR0 must read memory then discard the value");
    }

    // Misalignment must fault before memory is called and preserve destination.
    for (const auto address : {0x1001u, 0x1004u, 0x1007u}) {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = address;
        state.gpr[5] = {0x123456789abcdef0ull, 0x0fedcba987654321ull};
        const auto before = state.gpr[5];
        Read64Recorder recorder{};
        auto context = load_context_for(state, recorder);
        const auto result = execute_r5900_ir(
            {load64(5u, 1u, 0, 0x00114f10u)}, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure &&
                   recorder.calls == 0u &&
                   state.gpr[5].low64 == before.low64 &&
                   state.gpr[5].high64 == before.high64,
               "misaligned Load64 must fault transactionally before callback");
        expect_load_fault(context, 0x00114f10u, address);
    }

    // Missing bridge pieces fail safely with exact provenance.
    for (const auto missing_user : {false, true}) {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x2008u;
        state.gpr[5] = {0x1111222233334444ull, 0x5555666677778888ull};
        const auto before = state.gpr[5];
        Read64Recorder recorder{};
        auto context = load_context_for(state, recorder);
        if (missing_user) context.memory.user = nullptr;
        else context.memory.read64 = nullptr;
        const auto result = execute_r5900_ir(
            {load64(5u, 1u, 0, 0x00114f14u)}, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure &&
                   recorder.calls == 0u &&
                   state.gpr[5].low64 == before.low64 &&
                   state.gpr[5].high64 == before.high64,
               "missing Load64 bridge must fail without destination mutation");
        expect_load_fault(context, 0x00114f14u, 0x2008u);
    }

    // Callback rejection must not commit the destination or execute later IR.
    {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x3018u;
        state.gpr[5] = {0x0102030405060708ull, 0x1112131415161718ull};
        state.gpr[6].low64 = 0x100u;
        const auto before5 = state.gpr[5];
        const auto before6 = state.gpr[6];
        Read64Recorder recorder{};
        recorder.allow = false;
        auto context = load_context_for(state, recorder);
        const auto result = execute_r5900_ir(
            {load64(5u, 1u, 0, 0x00114f18u),
             or64(6u, 6u, 0xffu, 0x00114f1cu)}, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure &&
                   recorder.calls == 1u &&
                   state.gpr[5].low64 == before5.low64 &&
                   state.gpr[5].high64 == before5.high64 &&
                   state.gpr[6].low64 == before6.low64 &&
                   state.gpr[6].high64 == before6.high64,
               "rejected Load64 must be transactional and stop later IR");
        expect_load_fault(context, 0x00114f18u, 0x3018u);
    }

    std::cout << "r5900_ir_store32_executor_tests: PASS\n";
    return EXIT_SUCCESS;
}
