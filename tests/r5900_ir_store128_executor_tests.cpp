#include "recompiler/r5900_ir_executor.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_store128_executor_tests: FAIL: " << message << '\n';
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

R5900IrInstruction store128(std::uint8_t base,
                            std::uint8_t source,
                            std::int16_t offset,
                            std::uint32_t guest_pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = guest_pc;
    ir.opcode = R5900IrOpcode::Store128;
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

struct WriteRecorder {
    bool allow{true};
    std::uint32_t address{};
    std::uint64_t low64{};
    std::uint64_t high64{};
    std::size_t calls{};
};

bool record_write128(void* user,
                     std::uint32_t address,
                     std::uint64_t low64,
                     std::uint64_t high64) noexcept {
    auto& recorder = *static_cast<WriteRecorder*>(user);
    ++recorder.calls;
    recorder.address = address;
    recorder.low64 = low64;
    recorder.high64 = high64;
    return recorder.allow;
}

R5900IrExecutionContext context_for(R5900IrExecutionState& state,
                                    WriteRecorder& recorder) {
    R5900IrExecutionContext context{};
    context.state = &state;
    context.memory.user = &recorder;
    context.memory.write128 = &record_write128;
    return context;
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x1111222200000008ull;
        state.gpr[3] = {0x0123456789abcdefull, 0xfedcba9876543210ull};
        WriteRecorder recorder{};
        auto context = context_for(state, recorder);

        const auto result = execute_r5900_ir(
            {store128(1u, 3u, -16, 0x00100160u)}, context);
        expect(result.ok(), "Store128 with callback must execute");
        expect(recorder.calls == 1u, "Store128 must issue exactly one callback");
        expect(recorder.address == 0xfffffff0u,
               "Store128 must wrap low32 addition and align down to 16 bytes");
        expect(recorder.low64 == 0x0123456789abcdefull,
               "Store128 must forward source low64 unchanged");
        expect(recorder.high64 == 0xfedcba9876543210ull,
               "Store128 must forward source high64 unchanged");
        expect(context.current_memory_guest_pc == 0x00100160u,
               "Store128 must publish current memory guest PC");
        expect(!context.memory_fault.active,
               "successful Store128 must not report a memory fault");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x000000000000100full;
        state.gpr[0] = {0xffffffffffffffffull, 0xaaaaaaaaaaaaaaaaull};
        WriteRecorder recorder{};
        auto context = context_for(state, recorder);

        const auto result = execute_r5900_ir(
            {store128(1u, 0u, 0, 0x00100164u)}, context);
        expect(result.ok(), "Store128 from GPR0 must execute");
        expect(recorder.address == 0x00001000u,
               "Store128 must clear low four effective-address bits");
        expect(recorder.low64 == 0u && recorder.high64 == 0u,
               "architectural GPR0 source must store 128 zero bits");
        expect(state.gpr[0].low64 == 0u && state.gpr[0].high64 == 0u,
               "reference execution must normalize architectural GPR0");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[2].low64 = 0x0000000000002007ull;
        R5900IrExecutionContext context{};
        context.state = &state;
        context.current_memory_guest_pc = 0xdeadbeefu;
        context.memory_fault.active = true;

        const auto result = execute_r5900_ir(
            {store128(2u, 0u, 0, 0x00100168u)}, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "missing memory callback must be a memory access failure");
        expect(context.memory_fault.active,
               "missing callback must activate fault record");
        expect(context.memory_fault.access == R5900IrMemoryAccessKind::Store,
               "missing callback fault kind must be Store");
        expect(context.memory_fault.guest_pc == 0x00100168u,
               "missing callback fault must retain Store128 guest PC");
        expect(context.memory_fault.address == 0x00002000u,
               "missing callback fault must retain aligned address");
        expect(context.memory_fault.width_bytes == 16u,
               "missing callback fault width must be 16 bytes");
        expect(context.current_memory_guest_pc == 0x00100168u,
               "missing callback must publish current memory guest PC");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 0x0000000000003019ull;
        state.gpr[5] = {0x1111111122222222ull, 0x3333333344444444ull};
        state.gpr[6].low64 = 0x5555555555555555ull;
        WriteRecorder recorder{};
        recorder.allow = false;
        auto context = context_for(state, recorder);

        const auto result = execute_r5900_ir(
            {
                store128(4u, 5u, -1, 0x0010016cu),
                or64(6u, 6u, 0x0full, 0x00100170u),
            },
            context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "callback rejection must be a memory access failure");
        expect(recorder.calls == 1u,
               "failing Store128 must call memory bridge exactly once");
        expect(recorder.address == 0x00003010u,
               "callback rejection must use aligned effective address");
        expect(context.memory_fault.active &&
                   context.memory_fault.guest_pc == 0x0010016cu &&
                   context.memory_fault.address == 0x00003010u &&
                   context.memory_fault.width_bytes == 16u,
               "callback rejection must populate deterministic fault provenance");
        expect(state.gpr[6].low64 == 0x5555555555555555ull,
               "instructions after a failing Store128 must not execute");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[7].low64 = 0x100u;
        state.gpr[8].low64 = 0x200u;
        const auto result = execute_r5900_ir(
            {or64(9u, 7u, 0x20u, 0x00100174u)}, state);
        expect(result.ok(), "state-only memoryless execution must remain supported");
        expect(state.gpr[9].low64 == 0x120u,
               "state-only memoryless execution result mismatch");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[10].low64 = 0x0000000000004001ull;
        state.gpr[11] = {0xabcdef0123456789ull, 0x9988776655443322ull};
        WriteRecorder recorder{};
        auto context = context_for(state, recorder);

        R5900IrBlock block{};
        block.body = {store128(10u, 11u, 15, 0x00100180u)};
        block.terminator.guest_pc = 0x00100184u;
        block.terminator.kind = R5900IrTerminatorKind::Fallthrough;
        block.terminator.fallthrough_pc = 0x00100184u;

        const auto result = execute_r5900_ir_block(block, context);
        expect(result.ok(), "block executor must share Store128 execution context");
        expect(result.next_pc == 0x00100184u,
               "Store128 fallthrough block next PC mismatch");
        expect(recorder.calls == 1u && recorder.address == 0x00004010u,
               "block Store128 must use shared memory bridge and alignment semantics");
        expect(recorder.low64 == 0xabcdef0123456789ull &&
                   recorder.high64 == 0x9988776655443322ull,
               "block Store128 must forward full 128-bit source");
    }

    std::cout << "r5900_ir_store128_executor_tests: PASS\n";
    return EXIT_SUCCESS;
}
