#include "recompiler/r5900_decoder.h"
#include "recompiler/windows/r5900_x64_backend.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_x64_backend_windows_tests: FAIL: " << message << '\n';
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

R5900IrInstruction write_ir(R5900IrOpcode opcode,
                            std::uint8_t destination,
                            R5900IrOperand lhs,
                            R5900IrOperand rhs,
                            std::uint32_t guest_pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = guest_pc;
    ir.opcode = opcode;
    ir.destination = R5900IrRegister{destination};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
    ir.inputs = {lhs, rhs};
    return ir;
}

R5900IrInstruction store128_ir(std::uint8_t base,
                               std::uint8_t source,
                               std::int16_t offset,
                               std::uint32_t guest_pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = guest_pc;
    ir.opcode = R5900IrOpcode::Store128;
    ir.inputs = {gpr(base), gpr(source), immediate(offset)};
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

void expect_states_equal(const R5900IrExecutionState& expected,
                         const R5900IrExecutionState& actual,
                         const char* message) {
    for (std::size_t index = 0; index < expected.gpr.size(); ++index) {
        if (expected.gpr[index].low64 != actual.gpr[index].low64 ||
            expected.gpr[index].high64 != actual.gpr[index].high64) {
            fail(message);
        }
    }
}

constexpr std::uint32_t r_type(std::uint8_t rs,
                               std::uint8_t rt,
                               std::uint8_t rd,
                               std::uint8_t sa,
                               std::uint8_t funct) {
    return (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) |
           funct;
}

constexpr std::uint32_t i_type(std::uint8_t op,
                               std::uint8_t rs,
                               std::uint8_t rt,
                               std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           imm;
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    static_assert(std::is_move_constructible_v<R5900X64CompiledBlock>);
    static_assert(std::is_move_assignable_v<R5900X64CompiledBlock>);
    static_assert(!std::is_copy_constructible_v<R5900X64CompiledBlock>);
    static_assert(!std::is_copy_assignable_v<R5900X64CompiledBlock>);

    {
        R5900IrExecutionState state{};
        state.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
        state.gpr[1] = {0x1122334455667788ull, 0x8877665544332211ull};
        auto compiled = compile_r5900_ir_x64({});
        expect(compiled.ok() && compiled.block.has_value(), "empty program must compile");
        compiled.block->execute(state);
        expect(state.gpr[0].low64 == 0 && state.gpr[0].high64 == 0,
               "native block must normalize GPR0");
        expect(state.gpr[1].low64 == 0x1122334455667788ull &&
                   state.gpr[1].high64 == 0x8877665544332211ull,
               "empty block must preserve other GPRs");
    }

    {
        R5900IrInstruction nop{};
        nop.guest_pc = 0x00103000u;
        nop.opcode = R5900IrOpcode::Nop;
        R5900IrExecutionState state{};
        state.gpr[7] = {0x0123456789abcdefull, 0xfedcba9876543210ull};
        auto compiled = compile_r5900_ir_x64({nop});
        expect(compiled.ok(), "Nop must compile");
        compiled.block->execute(state);
        expect(state.gpr[7].low64 == 0x0123456789abcdefull &&
                   state.gpr[7].high64 == 0xfedcba9876543210ull,
               "Nop must preserve GPR state");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[9].low64 = 5u;
        state.gpr[10].low64 = 7u;
        state.gpr[8].high64 = 0x1111222233334444ull;
        const auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 8, gpr(9), gpr(10), 0x00103100u);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok(), "ADDU-style IR must compile");
        compiled.block->execute(state);
        expect(state.gpr[8].low64 == 12u, "native word add must produce positive result");
        expect(state.gpr[8].high64 == 0x1111222233334444ull, "word add must preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x7fffffffull;
        state.gpr[2].low64 = 1u;
        const auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 3, gpr(1), gpr(2), 0x00103104u);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok(), "negative word result must compile");
        compiled.block->execute(state);
        expect(state.gpr[3].low64 == 0xffffffff80000000ull,
               "word add must sign-extend negative result");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0xffffffffull;
        const auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 2, gpr(1), immediate(1), 0x00103108u);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok(), "wrapping word add must compile");
        compiled.block->execute(state);
        expect(state.gpr[2].low64 == 0u, "word add must wrap modulo 2^32");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[29] = {0x1000u, 0xaaaabbbbccccddddull};
        const auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 29, gpr(29), immediate(-16), 0x0010310cu);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok(), "ADDIU-style aliasing must compile");
        compiled.block->execute(state);
        expect(state.gpr[29].low64 == 0x0ff0u, "ADDIU-style aliasing must execute");
        expect(state.gpr[29].high64 == 0xaaaabbbbccccddddull, "ADDIU must preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 0x1234567800000000ull;
        state.gpr[5].high64 = 0x5555666677778888ull;
        const auto ir = write_ir(R5900IrOpcode::Or64, 5, gpr(4), immediate(0xff00), 0x00103200u);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok(), "ORI-style IR must compile");
        compiled.block->execute(state);
        expect(state.gpr[5].low64 == 0x123456780000ff00ull, "Or64 must operate on full low64");
        expect(state.gpr[5].high64 == 0x5555666677778888ull, "Or64 must preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[6] = {0xffu, 0x123456789abcdef0ull};
        const auto ir = write_ir(
            R5900IrOpcode::Or64,
            6,
            gpr(6),
            immediate(static_cast<std::int64_t>(0x8000000100000000ull)),
            0x00103204u);
        auto compiled = compile_r5900_ir_x64({ir});
        expect(compiled.ok(), "full imm64 Or64 must compile");
        compiled.block->execute(state);
        expect(state.gpr[6].low64 == 0x80000001000000ffull,
               "Or64 must preserve full immediate bit pattern");
        expect(state.gpr[6].high64 == 0x123456789abcdef0ull, "aliased Or64 must preserve high64");
    }

    {
        auto compiled = compile_r5900_ir_x64({});
        expect(compiled.ok(), "move-ownership source must compile");
        R5900X64CompiledBlock moved(std::move(*compiled.block));
        expect(moved.valid(), "move construction must transfer executable ownership");
        expect(!compiled.block->valid(), "move construction must invalidate source block");
        R5900X64CompiledBlock assigned;
        assigned = std::move(moved);
        expect(assigned.valid(), "move assignment must transfer executable ownership");
        expect(!moved.valid(), "move assignment must invalidate source block");
        R5900IrExecutionState state{};
        state.gpr[0] = {1u, 2u};
        assigned.execute(state);
        expect(state.gpr[0].low64 == 0u && state.gpr[0].high64 == 0u,
               "moved compiled block must remain callable");
    }

    {
        const auto valid = write_ir(R5900IrOpcode::Or64, 1, immediate(1), immediate(2), 0x00103300u);
        auto bad_source = valid;
        bad_source.inputs[0] = gpr(32);
        const auto source_result = compile_r5900_ir_x64({bad_source});
        expect(source_result.error == R5900X64CompileError::InvalidRegister && !source_result.block.has_value(),
               "backend must reject invalid source GPR without a block");

        auto bad_destination = valid;
        bad_destination.destination = R5900IrRegister{32};
        expect(compile_r5900_ir_x64({bad_destination}).error == R5900X64CompileError::InvalidRegister,
               "backend must reject invalid destination GPR");

        auto missing_destination = valid;
        missing_destination.destination.reset();
        expect(compile_r5900_ir_x64({missing_destination}).error == R5900X64CompileError::MalformedInstruction,
               "backend must reject missing destination");

        auto wrong_count = valid;
        wrong_count.inputs.pop_back();
        expect(compile_r5900_ir_x64({wrong_count}).error == R5900X64CompileError::MalformedInstruction,
               "backend must reject wrong operand count");

        auto wrong_mode = valid;
        wrong_mode.write_mode = R5900IrGprWriteMode::None;
        expect(compile_r5900_ir_x64({wrong_mode}).error == R5900X64CompileError::MalformedInstruction,
               "backend must reject wrong write mode");

        R5900IrInstruction bad_nop{};
        bad_nop.guest_pc = 0x00103304u;
        bad_nop.opcode = R5900IrOpcode::Nop;
        bad_nop.inputs = {immediate(1)};
        expect(compile_r5900_ir_x64({bad_nop}).error == R5900X64CompileError::MalformedInstruction,
               "backend must reject malformed Nop");

        auto unknown = valid;
        unknown.opcode = static_cast<R5900IrOpcode>(0xff);
        const auto unknown_result = compile_r5900_ir_x64({unknown});
        expect(unknown_result.error == R5900X64CompileError::UnsupportedOpcode && !unknown_result.block.has_value(),
               "backend must reject unknown opcode without a block");
        expect(unknown_result.message.find("IR instruction 0") != std::string::npos,
               "backend validation diagnostic must include instruction index");
    }

    {
        R5900IrExecutionState initial{};
        for (std::size_t index = 0; index < initial.gpr.size(); ++index) {
            initial.gpr[index].low64 = 0x0101010101010101ull * static_cast<std::uint64_t>(index + 1u);
            initial.gpr[index].high64 = 0xf000000000000000ull | static_cast<std::uint64_t>(index);
        }
        initial.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
        initial.gpr[29].low64 = 0x1000u;

        R5900IrInstruction nop{};
        nop.guest_pc = 0x00103400u;
        nop.opcode = R5900IrOpcode::Nop;
        const std::vector<R5900IrInstruction> program = {
            nop,
            write_ir(R5900IrOpcode::AddWordSignExtend, 8, gpr(9), gpr(10), 0x00103404u),
            write_ir(R5900IrOpcode::AddWordSignExtend, 29, gpr(29), immediate(-16), 0x00103408u),
            write_ir(R5900IrOpcode::Or64, 5, gpr(4), immediate(0xff00), 0x0010340cu),
            write_ir(R5900IrOpcode::Or64, 0, immediate(1), immediate(2), 0x00103410u),
        };

        auto expected = initial;
        auto actual = initial;
        expect(execute_r5900_ir(program, expected).ok(), "reference executor must accept differential program");
        auto compiled = compile_r5900_ir_x64(program);
        expect(compiled.ok(), "backend must compile differential program");
        compiled.block->execute(actual);
        expect_states_equal(expected, actual,
                            "native backend must match reference executor for all 32 GPR low/high halves");
    }

    {
        R5900IrExecutionState initial{};
        initial.gpr[9].low64 = 5u;
        initial.gpr[10].low64 = 7u;
        initial.gpr[8].high64 = 0x1111222233334444ull;
        initial.gpr[29] = {0x1000u, 0xaaaabbbbccccddddull};
        initial.gpr[4].low64 = 0x1234567800000000ull;
        initial.gpr[5].high64 = 0x5555666677778888ull;

        const std::uint32_t words[] = {
            0u,
            r_type(9, 10, 8, 0, 0x21),
            i_type(0x09, 29, 29, 0xfff0),
            i_type(0x0d, 4, 5, 0xff00),
        };

        std::vector<R5900IrInstruction> program;
        std::uint32_t pc = 0x00104000u;
        for (const auto word : words) {
            const auto lowered = lower_r5900_instruction(decode_r5900(word), pc);
            expect(lowered.ok(), "synthetic guest instruction must lower");
            expect(lowered.instructions.size() == 1u, "each synthetic instruction must lower to one IR op");
            program.push_back(lowered.instructions.front());
            pc += 4u;
        }

        auto expected = initial;
        auto actual = initial;
        expect(execute_r5900_ir(program, expected).ok(), "reference synthetic pipeline must execute");
        auto compiled = compile_r5900_ir_x64(program);
        expect(compiled.ok(), "decoder/lowering output must compile natively");
        compiled.block->execute(actual);
        expect_states_equal(expected, actual,
                            "decoder -> IR -> x64 native state must match reference executor bit-for-bit");
    }

    {
        const auto block = fallthrough_block(
            {store128_ir(1u, 3u, -16, 0x00105000u)}, 0x00105004u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "Store128 IR must compile natively");

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
        expect(native_result.ok() && reference_result.ok(),
               "Store128 native/reference execution must succeed");
        expect(native_result.next_pc == reference_result.next_pc &&
                   native_result.next_pc == 0x00105004u,
               "Store128 native next PC must match reference");
        expect(native_recorder.calls == 1u && reference_recorder.calls == 1u,
               "Store128 native/reference must each call memory once");
        expect(native_recorder.address == reference_recorder.address &&
                   native_recorder.address == 0xfffffff0u,
               "Store128 native wrap/alignment must match reference");
        expect(native_recorder.low64 == 0x0123456789abcdefull &&
                   native_recorder.high64 == 0xfedcba9876543210ull,
               "Store128 native path must forward full 128-bit source");
    }

    {
        const auto block = fallthrough_block(
            {store128_ir(2u, 0u, 0, 0x00105010u)}, 0x00105014u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "Store128 GPR0 IR must compile natively");

        R5900IrExecutionState state{};
        state.gpr[2].low64 = 0x3003u;
        state.gpr[0] = {0xffffffffffffffffull, 0xaaaaaaaaaaaaaaaaull};
        WriteRecorder recorder{};
        auto context = context_for(state, recorder);
        const auto result = compiled.block->execute(context);
        expect(result.ok(), "Store128 GPR0 native execution must succeed");
        expect(recorder.address == 0x3000u,
               "Store128 native path must align address down");
        expect(recorder.low64 == 0u && recorder.high64 == 0u,
               "Store128 native GPR0 source must be architectural zero");
    }

    {
        const auto block = fallthrough_block(
            {store128_ir(2u, 4u, 0, 0x00105020u)}, 0x00105024u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "failing Store128 IR must still compile");

        R5900IrExecutionState state{};
        state.gpr[2].low64 = 0x4017u;
        state.gpr[4] = {1u, 2u};
        WriteRecorder recorder{};
        recorder.allow = false;
        auto context = context_for(state, recorder);
        const auto result = compiled.block->execute(context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "Store128 native callback failure must propagate");
        expect(result.next_pc == 0u,
               "Store128 native failure must not return normal next PC");
        expect(context.memory_fault.active &&
                   context.memory_fault.guest_pc == 0x00105020u &&
                   context.memory_fault.address == 0x4010u &&
                   context.memory_fault.width_bytes == 16u,
               "Store128 native failure must preserve deterministic fault provenance");
    }

    {
        const auto block = fallthrough_block(
            {store128_ir(2u, 0u, 0, 0x00105030u)}, 0x00105034u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "Store128 missing-callback block must compile");

        R5900IrExecutionState state{};
        state.gpr[2].low64 = 0x5009u;
        R5900IrExecutionContext context{};
        context.state = &state;
        const auto result = compiled.block->execute(context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "Store128 native missing callback must fail deterministically");
        expect(context.memory_fault.active &&
                   context.memory_fault.guest_pc == 0x00105030u &&
                   context.memory_fault.address == 0x5000u,
               "Store128 native missing callback must populate fault provenance");
    }

    {
        const auto block = fallthrough_block(
            {
                store128_ir(2u, 3u, 0, 0x00105040u),
                write_ir(R5900IrOpcode::Or64, 6u, gpr(6u), immediate(0x0f), 0x00105044u),
            },
            0x00105048u);
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok(), "Store128 failure-prefix block must compile");

        R5900IrExecutionState state{};
        state.gpr[2].low64 = 0x6000u;
        state.gpr[3] = {3u, 4u};
        state.gpr[6].low64 = 0x50u;
        WriteRecorder recorder{};
        recorder.allow = false;
        auto context = context_for(state, recorder);
        const auto result = compiled.block->execute(context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "Store128 failure must stop native block");
        expect(state.gpr[6].low64 == 0x50u,
               "native instructions after failed Store128 must not execute");
    }

    std::cout << "r5900_x64_backend_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
