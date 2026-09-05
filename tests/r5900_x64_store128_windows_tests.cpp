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
    std::cerr << "r5900_x64_store128_windows_tests: FAIL: " << message << '\n';
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
                            std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::Store128;
    ir.inputs = {gpr(base), gpr(source), immediate(offset)};
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

R5900IrInstruction addiu(std::uint8_t destination,
                         std::uint8_t source,
                         std::int16_t value,
                         std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::AddWordSignExtend;
    ir.destination = R5900IrDestination{destination};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
    ir.inputs = {gpr(source), immediate(value)};
    return ir;
}

R5900IrBlock fallthrough_block(std::vector<R5900IrInstruction> body,
                               std::uint32_t next_pc) {
    R5900IrBlock block{};
    block.body = std::move(body);
    block.terminator.guest_pc = next_pc;
    block.terminator.kind = R5900IrTerminatorKind::Fallthrough;
    block.terminator.fallthrough_pc = next_pc;
    return block;
}

R5900IrBlock direct_jump(std::uint32_t pc,
                         std::uint32_t target_pc,
                         R5900IrInstruction delay) {
    R5900IrBlock block{};
    block.terminator.guest_pc = pc;
    block.terminator.kind = R5900IrTerminatorKind::DirectJump;
    block.terminator.target_pc = target_pc;
    block.terminator.delay_slot = {delay};
    return block;
}

R5900IrBlock direct_call(std::uint32_t pc,
                         std::uint32_t target_pc,
                         R5900IrInstruction delay) {
    auto block = direct_jump(pc, target_pc, delay);
    block.terminator.kind = R5900IrTerminatorKind::DirectCall;
    block.terminator.link_pc = pc + 8u;
    return block;
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

void expect_gprs_equal(const R5900IrExecutionState& expected,
                       const R5900IrExecutionState& actual,
                       const char* message) {
    for (std::size_t index = 0; index < expected.gpr.size(); ++index) {
        if (expected.gpr[index].low64 != actual.gpr[index].low64 ||
            expected.gpr[index].high64 != actual.gpr[index].high64) {
            fail(message);
        }
    }
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    {
        const auto block = fallthrough_block(
            {store128(1u, 3u, -16, 0x00101000u)}, 0x00101004u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "native Store128 block must compile");

        R5900IrExecutionState native_state{};
        native_state.gpr[1].low64 = 0x9999000000000008ull;
        native_state.gpr[3] = {0x0123456789abcdefull, 0xfedcba9876543210ull};
        R5900IrExecutionState reference_state = native_state;
        WriteRecorder native_recorder{};
        WriteRecorder reference_recorder{};
        auto native_context = context_for(native_state, native_recorder);
        auto reference_context = context_for(reference_state, reference_recorder);

        const auto native_result = compiled.block->execute(native_context);
        const auto reference_result = execute_r5900_ir_block(block, reference_context);
        expect(native_result.ok(), "native Store128 must execute successfully");
        expect(reference_result.ok(), "reference Store128 must execute successfully");
        expect(native_result.next_pc == reference_result.next_pc &&
                   native_result.next_pc == 0x00101004u,
               "native Store128 next PC must match reference fallthrough");
        expect(native_recorder.calls == 1u && reference_recorder.calls == 1u,
               "native/reference Store128 must each issue one write");
        expect(native_recorder.address == reference_recorder.address &&
                   native_recorder.address == 0xfffffff0u,
               "native Store128 wrap/alignment must match reference");
        expect(native_recorder.low64 == reference_recorder.low64 &&
                   native_recorder.high64 == reference_recorder.high64 &&
                   native_recorder.low64 == 0x0123456789abcdefull &&
                   native_recorder.high64 == 0xfedcba9876543210ull,
               "native Store128 must forward full 128-bit source");
    }

    {
        const auto block = fallthrough_block(
            {store128(1u, 0u, 0, 0x00101010u)}, 0x00101014u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "native Store128 GPR0 block must compile");

        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x100fu;
        state.gpr[0] = {0xffffffffffffffffull, 0xaaaaaaaaaaaaaaaaull};
        WriteRecorder recorder{};
        auto context = context_for(state, recorder);
        const auto result = compiled.block->execute(context);
        expect(result.ok(), "native Store128 GPR0 source must execute");
        expect(recorder.address == 0x1000u,
               "native Store128 must align address down");
        expect(recorder.low64 == 0u && recorder.high64 == 0u,
               "native Store128 must observe architectural GPR0 zero");
    }

    {
        const auto block = fallthrough_block(
            {store128(2u, 4u, 0, 0x00101020u)}, 0x00101024u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "native failing Store128 block must compile");

        R5900IrExecutionState state{};
        state.gpr[2].low64 = 0x2017u;
        state.gpr[4] = {1u, 2u};
        WriteRecorder recorder{};
        recorder.allow = false;
        auto context = context_for(state, recorder);
        const auto result = compiled.block->execute(context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "native callback rejection must propagate MemoryAccessFailure");
        expect(result.next_pc == 0u,
               "native failed Store128 must not return a normal next PC");
        expect(recorder.calls == 1u,
               "native failed Store128 must call bridge exactly once");
        expect(context.memory_fault.active &&
                   context.memory_fault.guest_pc == 0x00101020u &&
                   context.memory_fault.address == 0x2010u &&
                   context.memory_fault.width_bytes == 16u,
               "native callback rejection must preserve fault provenance");
    }

    {
        const auto block = fallthrough_block(
            {store128(2u, 0u, 0, 0x00101030u)}, 0x00101034u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "native missing-callback Store128 block must compile");

        R5900IrExecutionState state{};
        state.gpr[2].low64 = 0x3003u;
        R5900IrExecutionContext context{};
        context.state = &state;
        const auto result = compiled.block->execute(context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "native missing callback must propagate MemoryAccessFailure");
        expect(context.memory_fault.active &&
                   context.memory_fault.guest_pc == 0x00101030u &&
                   context.memory_fault.address == 0x3000u,
               "native missing callback must populate fault record");
    }

    {
        const auto block = fallthrough_block(
            {
                store128(2u, 3u, 0, 0x00101040u),
                or64(6u, 6u, 0x0fu, 0x00101044u),
            },
            0x00101048u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "native Store128 prefix-failure block must compile");

        R5900IrExecutionState state{};
        state.gpr[2].low64 = 0x4000u;
        state.gpr[3] = {3u, 4u};
        state.gpr[6].low64 = 0x50u;
        WriteRecorder recorder{};
        recorder.allow = false;
        auto context = context_for(state, recorder);
        const auto result = compiled.block->execute(context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "native failed Store128 must stop block execution");
        expect(state.gpr[6].low64 == 0x50u,
               "native instructions after failed Store128 must not execute");
    }

    {
        const auto block = fallthrough_block(
            {or64(8u, 7u, 0x20u, 0x00101050u)}, 0x00101054u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "legacy arithmetic block must compile");

        R5900IrExecutionState native_state{};
        native_state.gpr[7].low64 = 0x100u;
        R5900IrExecutionState reference_state = native_state;
        R5900IrExecutionContext native_context{};
        native_context.state = &native_state;
        R5900IrExecutionContext reference_context{};
        reference_context.state = &reference_state;

        const auto native_result = compiled.block->execute(native_context);
        const auto reference_result = execute_r5900_ir_block(block, reference_context);
        expect(native_result.ok() && reference_result.ok(),
               "memoryless native/reference contexts must execute");
        expect(native_result.next_pc == reference_result.next_pc,
               "memoryless native next PC must remain differential-equal");
        expect(native_state.gpr[8].low64 == reference_state.gpr[8].low64 &&
                   native_state.gpr[8].low64 == 0x120u,
               "memoryless native state must remain differential-equal");
    }

    {
        const auto block = direct_jump(0x00108000u,
                                       0x00108100u,
                                       addiu(7u, 7u, 1, 0x00108004u));
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "native direct J block must compile");

        R5900IrExecutionState initial{};
        initial.gpr[7] = {5u, 0x7777777777777777ull};
        initial.gpr[31] = {0x1122334455667788ull, 0x8877665544332211ull};
        auto native_state = initial;
        auto reference_state = initial;
        R5900IrExecutionContext native_context{};
        native_context.state = &native_state;
        R5900IrExecutionContext reference_context{};
        reference_context.state = &reference_state;

        const auto native_result = compiled.block->execute(native_context);
        const auto reference_result = execute_r5900_ir_block(block, reference_context);
        expect(native_result.ok() && reference_result.ok(),
               "native/reference direct J must execute");
        expect(native_result.next_pc == reference_result.next_pc &&
                   native_result.next_pc == 0x00108100u,
               "native direct J next PC must match reference");
        expect_gprs_equal(reference_state, native_state,
                          "native direct J state must match reference");
    }

    {
        const auto block = direct_call(0x00108200u,
                                       0x00108300u,
                                       addiu(23u, 31u, 0, 0x00108204u));
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "native direct JAL block must compile");

        R5900IrExecutionState initial{};
        initial.gpr[31] = {0xdeadbeefdeadbeefull, 0x0123456789abcdefull};
        auto native_state = initial;
        auto reference_state = initial;
        R5900IrExecutionContext native_context{};
        native_context.state = &native_state;
        R5900IrExecutionContext reference_context{};
        reference_context.state = &reference_state;

        const auto native_result = compiled.block->execute(native_context);
        const auto reference_result = execute_r5900_ir_block(block, reference_context);
        expect(native_result.ok() && reference_result.ok(),
               "native/reference direct JAL must execute");
        expect(native_result.next_pc == reference_result.next_pc &&
                   native_result.next_pc == 0x00108300u,
               "native JAL target must match reference");
        expect(native_state.gpr[31].low64 == 0x00108208u &&
                   native_state.gpr[31].high64 == 0x0123456789abcdefull,
               "native JAL must write low64 link and preserve high64");
        expect(native_state.gpr[23].low64 == 0x00108208u,
               "native JAL delay must observe new link");
        expect_gprs_equal(reference_state, native_state,
                          "native direct JAL state must match reference");
    }

    {
        const auto block = direct_call(0x00108400u,
                                       0x00108500u,
                                       addiu(31u, 0u, 9, 0x00108404u));
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "native JAL r31-delay block must compile");

        R5900IrExecutionState initial{};
        initial.gpr[31] = {0x1111u, 0xfedcba9876543210ull};
        auto native_state = initial;
        auto reference_state = initial;
        R5900IrExecutionContext native_context{};
        native_context.state = &native_state;
        R5900IrExecutionContext reference_context{};
        reference_context.state = &reference_state;

        const auto native_result = compiled.block->execute(native_context);
        const auto reference_result = execute_r5900_ir_block(block, reference_context);
        expect(native_result.ok() && reference_result.ok(),
               "native/reference JAL r31-delay must execute");
        expect(native_state.gpr[31].low64 == 9u &&
                   native_state.gpr[31].high64 == 0xfedcba9876543210ull,
               "native delay write must win after JAL link");
        expect_gprs_equal(reference_state, native_state,
                          "native JAL r31-delay state must match reference");
    }

    {
        auto block = direct_jump(0x00108600u,
                                 0x00108700u,
                                 store128(2u, 3u, 0, 0x00108604u));
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "helper-frame direct J block must compile");

        R5900IrExecutionState native_state{};
        native_state.gpr[2].low64 = 0x600fu;
        native_state.gpr[3] = {0x1111222233334444ull, 0x5555666677778888ull};
        auto reference_state = native_state;
        WriteRecorder native_recorder{};
        WriteRecorder reference_recorder{};
        auto native_context = context_for(native_state, native_recorder);
        auto reference_context = context_for(reference_state, reference_recorder);

        const auto native_result = compiled.block->execute(native_context);
        const auto reference_result = execute_r5900_ir_block(block, reference_context);
        expect(native_result.ok() && reference_result.ok(),
               "helper-frame direct J native/reference must execute");
        expect(native_result.next_pc == 0x00108700u &&
                   native_result.next_pc == reference_result.next_pc,
               "helper-frame direct J target must match reference");
        expect(native_recorder.calls == 1u && reference_recorder.calls == 1u &&
                   native_recorder.address == reference_recorder.address &&
                   native_recorder.address == 0x6000u,
               "helper-frame direct J delay Store128 must match reference");
    }

    std::cout << "r5900_x64_store128_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
