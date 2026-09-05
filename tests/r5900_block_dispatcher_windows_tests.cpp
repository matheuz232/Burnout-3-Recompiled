#include "recompiler/ps2_elf.h"
#include "recompiler/r5900_ir.h"
#include "recompiler/windows/r5900_block_dispatcher.h"
#include "runtime/ps2_memory_map.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_block_dispatcher_windows_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void expect_states_equal(const b3r::recompiler::R5900IrExecutionState& expected,
                         const b3r::recompiler::R5900IrExecutionState& actual,
                         const char* message) {
    for (std::size_t index = 0; index < expected.gpr.size(); ++index) {
        if (expected.gpr[index].low64 != actual.gpr[index].low64 ||
            expected.gpr[index].high64 != actual.gpr[index].high64) {
            fail(message);
        }
    }
}

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

b3r::runtime::Ps2MemoryMap make_memory(const std::vector<std::uint32_t>& words,
                                       std::uint32_t base,
                                       std::uint32_t flags = 5u) {
    constexpr std::uint32_t kProgramHeaderOffset = 52u;
    constexpr std::uint32_t kPayloadOffset = 0x100u;
    const std::uint32_t payload_size = static_cast<std::uint32_t>(words.size() * 4u);
    Bytes bytes(static_cast<std::size_t>(kPayloadOffset + payload_size + 0x40u), 0u);

    bytes[0] = 0x7fu;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 1u;
    bytes[5] = 1u;
    bytes[6] = 1u;
    put_u16(bytes, 16u, 2u);
    put_u16(bytes, 18u, 8u);
    put_u32(bytes, 20u, 1u);
    put_u32(bytes, 24u, base);
    put_u32(bytes, 28u, kProgramHeaderOffset);
    put_u16(bytes, 40u, 52u);
    put_u16(bytes, 42u, 32u);
    put_u16(bytes, 44u, 1u);

    put_u32(bytes, kProgramHeaderOffset + 0u, 1u);
    put_u32(bytes, kProgramHeaderOffset + 4u, kPayloadOffset);
    put_u32(bytes, kProgramHeaderOffset + 8u, base);
    put_u32(bytes, kProgramHeaderOffset + 12u, base);
    put_u32(bytes, kProgramHeaderOffset + 16u, payload_size);
    put_u32(bytes, kProgramHeaderOffset + 20u, payload_size);
    put_u32(bytes, kProgramHeaderOffset + 24u, flags);
    put_u32(bytes, kProgramHeaderOffset + 28u, 0x1000u);

    for (std::size_t index = 0; index < words.size(); ++index) {
        put_u32(bytes, static_cast<std::size_t>(kPayloadOffset) + index * 4u, words[index]);
    }

    auto parsed = b3r::recompiler::parse_ps2_elf(bytes);
    expect(parsed.ok(), "synthetic dispatcher ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "synthetic dispatcher memory must map");
    return std::move(*built.memory);
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

constexpr std::uint32_t j_type(std::uint8_t op, std::uint32_t target) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           ((target >> 2u) & 0x03ffffffu);
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    constexpr std::uint32_t base = 0x00100000u;

    {
        auto memory = make_memory({0u}, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[1] = {0x1122334455667788ull, 0x8877665544332211ull};

        const auto zero_budget = dispatcher.run(base, state, 0u);
        expect(zero_budget.reason == R5900DispatchStopReason::InvalidBlockBudget,
               "zero block budget must reject explicitly");
        expect(zero_budget.next_pc == base,
               "zero block budget must retain start PC");
        expect(zero_budget.blocks_executed == 0u && zero_budget.instructions_executed == 0u,
               "zero block budget must execute nothing");
        expect(zero_budget.cache_hits == 0u && zero_budget.cache_misses == 0u &&
                   zero_budget.recompilations == 0u,
               "zero block budget must not touch cache accounting");
        expect(zero_budget.message.find("budget") != std::string::npos &&
                   zero_budget.message.find("0x00100000") != std::string::npos,
               "budget failure diagnostic must include stage and guest PC");
        expect(state.gpr[1].low64 == 0x1122334455667788ull &&
                   state.gpr[1].high64 == 0x8877665544332211ull,
               "zero block budget must not mutate state");

        const auto unaligned = dispatcher.run(base + 2u, state, 1u);
        expect(unaligned.reason == R5900DispatchStopReason::AnalysisFailure,
               "unaligned PC must map analyzer failure");
        expect(unaligned.next_pc == base + 2u,
               "analysis failure must report failing PC");
        expect(unaligned.blocks_executed == 0u,
               "analysis failure must not consume budget");
    }

    {
        const std::vector<std::uint32_t> words = {
            0u,
            r_type(9, 10, 8, 0, 0x21),
            i_type(0x09, 29, 29, 0xfff0),
            i_type(0x0d, 4, 5, 0xff00),
            i_type(0x0e, 1, 2, 0x00ff),
        };
        auto memory = make_memory(words, base);
        R5900BlockDispatcherOptions options{};
        options.block_options.max_instructions = words.size();
        R5900BlockDispatcher dispatcher(memory, options);

        R5900IrExecutionState state{};
        state.gpr[9].low64 = 5u;
        state.gpr[10].low64 = 7u;
        state.gpr[8].high64 = 0x1111222233334444ull;
        state.gpr[29] = {0x1000u, 0xaaaabbbbccccddddull};
        state.gpr[4].low64 = 0x1234567800000000ull;
        state.gpr[5].high64 = 0x5555666677778888ull;

        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction,
               "supported prefix must stop before XORI");
        expect(result.next_pc == base + 16u,
               "unsupported boundary must report first unexecuted PC");
        expect(result.blocks_executed == 1u && result.instructions_executed == 4u,
               "only supported prefix must count as executed");
        expect(state.gpr[8].low64 == 12u &&
                   state.gpr[8].high64 == 0x1111222233334444ull,
               "ADDU prefix instruction must execute natively");
        expect(state.gpr[29].low64 == 0x0ff0u &&
                   state.gpr[29].high64 == 0xaaaabbbbccccddddull,
               "ADDIU prefix instruction must preserve high64");
        expect(state.gpr[5].low64 == 0x123456780000ff00ull &&
                   state.gpr[5].high64 == 0x5555666677778888ull,
               "ORI prefix instruction must execute on low64 only");
        expect(state.gpr[2].low64 == 0u,
               "unsupported XORI must not execute");
    }

    {
        const auto andi = i_type(0x0c, 1, 2, 0x00ff);
        auto memory = make_memory({andi}, base);
        R5900BlockDispatcherOptions options{};
        options.block_options.max_instructions = 1u;
        R5900BlockDispatcher dispatcher(memory, options);
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x1234u;
        state.gpr[2] = {0xdeadbeefu, 0x5678u};

        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted,
               "ANDI at entry must execute as a supported startup instruction");
        expect(result.next_pc == base + 4u && result.blocks_executed == 1u &&
                   result.instructions_executed == 1u,
               "ANDI entry must execute exactly one guest instruction");
        expect(state.gpr[2].low64 == 0x34u && state.gpr[2].high64 == 0x5678u,
               "ANDI must update low64 while preserving destination high64");
    }

    {
        auto memory = make_memory({0x0000000cu}, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x55u;

        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::Trap,
               "SYSCALL at entry must stop as trap");
        expect(result.next_pc == base && result.blocks_executed == 0u &&
                   result.instructions_executed == 0u,
               "trap entry must execute nothing");
        expect(state.gpr[1].low64 == 0x55u,
               "trap entry must preserve state");
    }

    {
        const auto beq = i_type(0x04, 1, 2, 1u);
        const auto delay = i_type(0x09, 0, 3, 9u);
        auto memory = make_memory({beq, delay}, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};

        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::ControlFlow,
               "BEQ at entry must stop as control flow");
        expect(result.next_pc == base && result.blocks_executed == 0u &&
                   result.instructions_executed == 0u,
               "control-flow entry must execute nothing");
        expect(state.gpr[3].low64 == 0u,
               "control-flow entry must not execute delay slot");
    }

    {
        const auto prefix = i_type(0x09, 0, 1, 7u);
        const auto delay = i_type(0x09, 0, 2, 9u);
        const std::vector<std::uint32_t> terminators = {
            i_type(0x04, 1, 3, 1u),
            j_type(0x02, base + 0x20u),
            j_type(0x03, base + 0x20u),
            r_type(31, 0, 0, 0, 0x08),
        };

        for (const auto terminator : terminators) {
            auto memory = make_memory({prefix, terminator, delay}, base);
            R5900BlockDispatcher dispatcher(memory);
            R5900IrExecutionState state{};

            const auto result = dispatcher.run(base, state, 1u);
            expect(result.reason == R5900DispatchStopReason::ControlFlow,
                   "supported prefix must stop before control-flow terminator");
            expect(result.next_pc == base + 4u,
                   "control-flow boundary PC must be exact");
            expect(result.blocks_executed == 1u && result.instructions_executed == 1u,
                   "only straight-line prefix must execute");
            expect(state.gpr[1].low64 == 7u,
                   "straight-line prefix must execute before control-flow stop");
            expect(state.gpr[2].low64 == 0u,
                   "delay slot must not execute in dispatcher v0");
        }
    }

    {
        const std::vector<std::uint32_t> words = {
            i_type(0x09, 0, 1, 1u),
            i_type(0x09, 1, 1, 1u),
            i_type(0x09, 1, 1, 1u),
            i_type(0x09, 1, 1, 1u),
            i_type(0x09, 1, 1, 1u),
            i_type(0x0e, 1, 2, 0x00ff),
        };
        R5900BlockDispatcherOptions options{};
        options.block_options.max_instructions = 2u;

        {
            auto memory = make_memory(words, base);
            R5900BlockDispatcher dispatcher(memory, options);
            R5900IrExecutionState state{};
            const auto result = dispatcher.run(base, state, 1u);
            expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted,
                   "one-block budget must stop at sequential chunk boundary");
            expect(result.next_pc == base + 8u && result.blocks_executed == 1u &&
                       result.instructions_executed == 2u,
                   "one-block budget must execute exactly two guest instructions");
            expect(state.gpr[1].low64 == 2u,
                   "first analyzer chunk must execute before budget stop");
        }

        {
            auto memory = make_memory(words, base);
            R5900BlockDispatcher dispatcher(memory, options);
            R5900IrExecutionState state{};
            const auto result = dispatcher.run(base, state, 2u);
            expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted,
                   "two-block budget must stop after second sequential chunk");
            expect(result.next_pc == base + 16u && result.blocks_executed == 2u &&
                       result.instructions_executed == 4u,
                   "two-block budget must accumulate exact progress");
            expect(state.gpr[1].low64 == 4u,
                   "two analyzer chunks must execute sequentially");
        }

        {
            auto memory = make_memory(words, base);
            R5900BlockDispatcher dispatcher(memory, options);
            R5900IrExecutionState state{};
            const auto result = dispatcher.run(base, state, 3u);
            expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction,
                   "known unsupported boundary must beat exhausted budget");
            expect(result.next_pc == base + 20u && result.blocks_executed == 3u &&
                       result.instructions_executed == 5u,
                   "third chunk must execute only its supported prefix");
            expect(state.gpr[1].low64 == 5u,
                   "all five supported ADDIU instructions must execute");
        }
    }

    {
        const std::vector<std::uint32_t> words = {
            i_type(0x09, 0, 1, 3u),
            i_type(0x0d, 1, 2, 0x20u),
        };
        auto memory = make_memory(words, base);
        R5900BlockDispatcherOptions options{};
        options.block_options.max_instructions = 2u;
        R5900BlockDispatcher dispatcher(memory, options);

        R5900IrExecutionState first_state{};
        const auto first = dispatcher.run(base, first_state, 1u);
        expect(first.cache_misses == 1u && first.cache_hits == 0u,
               "first native candidate must be a cache miss");
        expect(dispatcher.cache_size() == 1u,
               "successful compile must populate one cache entry");

        R5900IrExecutionState second_state{};
        const auto second = dispatcher.run(base, second_state, 1u);
        expect(second.cache_hits == 1u && second.cache_misses == 0u,
               "identical native candidate must hit cache");
        expect(dispatcher.cache_size() == 1u,
               "cache hit must not duplicate native entry");
        expect(first_state.gpr[2].low64 == second_state.gpr[2].low64,
               "cached native execution must preserve semantics");

        dispatcher.clear_cache();
        expect(dispatcher.cache_size() == 0u,
               "clear_cache must destroy all native entries");
    }

    {
        R5900BlockDispatcherOptions options{};
        options.block_options.max_instructions = 1u;
        const auto addiu_one = i_type(0x09, 0, 1, 1u);
        const auto addiu_seven = i_type(0x09, 0, 1, 7u);
        const auto xori = i_type(0x0e, 1, 2, 0xffu);
        auto memory = make_memory({addiu_one}, base);
        R5900BlockDispatcher dispatcher(memory, options);

        R5900IrExecutionState first_state{};
        const auto first = dispatcher.run(base, first_state, 1u);
        expect(first_state.gpr[1].low64 == 1u,
               "initial native block must execute original guest word");
        expect(first.cache_misses == 1u && first.recompilations == 0u,
               "initial compile must be a plain cache miss");
        expect(dispatcher.cache_size() == 1u,
               "initial compile must create one cache entry");

        expect(memory.write_u32(base, addiu_seven),
               "guest code mutation must succeed");
        R5900IrExecutionState second_state{};
        const auto second = dispatcher.run(base, second_state, 1u);
        expect(second_state.gpr[1].low64 == 7u,
               "stale native code must never execute");
        expect(second.recompilations == 1u,
               "changed guest code must count as recompilation");
        expect(second.cache_hits == 0u && second.cache_misses == 0u,
               "stale lookup is neither a hit nor a plain miss");
        expect(dispatcher.cache_size() == 1u,
               "successful stale replacement keeps one entry per start PC");

        expect(memory.write_u32(base, xori),
               "mutation to unsupported instruction must succeed");
        R5900IrExecutionState third_state{};
        third_state.gpr[1].low64 = 0x55u;
        const auto third = dispatcher.run(base, third_state, 1u);
        expect(third.reason == R5900DispatchStopReason::UnsupportedInstruction,
               "unsupported mutated code must stop before cache lookup");
        expect(third.blocks_executed == 0u && third.instructions_executed == 0u,
               "unsupported mutated code must execute nothing");
        expect(third.cache_hits == 0u && third.cache_misses == 0u &&
                   third.recompilations == 0u,
               "unsupported mutated code must not touch cache accounting");
        expect(third_state.gpr[1].low64 == 0x55u,
               "old cached native block must not execute for unsupported code");
    }

    {
        const std::vector<std::uint32_t> words = {
            0u,
            r_type(9, 10, 8, 0, 0x21),
            i_type(0x09, 29, 29, 0xfff0),
            i_type(0x0d, 4, 5, 0xff00),
        };

        R5900IrExecutionState initial{};
        for (std::size_t index = 0; index < initial.gpr.size(); ++index) {
            initial.gpr[index].low64 =
                0x0101010101010101ull * static_cast<std::uint64_t>(index + 1u);
            initial.gpr[index].high64 =
                0xf000000000000000ull | static_cast<std::uint64_t>(index);
        }
        initial.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
        initial.gpr[29].low64 = 0x1000u;

        std::vector<R5900IrInstruction> reference_ir{};
        for (std::size_t index = 0; index < words.size(); ++index) {
            const auto pc = base + static_cast<std::uint32_t>(index * 4u);
            const auto lowered = lower_r5900_instruction(decode_r5900(words[index]), pc);
            expect(lowered.ok(), "differential fixture must lower in reference path");
            reference_ir.insert(reference_ir.end(),
                                lowered.instructions.begin(),
                                lowered.instructions.end());
        }

        auto expected = initial;
        auto actual = initial;
        expect(execute_r5900_ir(reference_ir, expected).ok(),
               "reference executor must accept dispatcher differential IR");

        auto memory = make_memory(words, base);
        R5900BlockDispatcherOptions options{};
        options.block_options.max_instructions = words.size();
        R5900BlockDispatcher dispatcher(memory, options);
        const auto result = dispatcher.run(base, actual, 1u);
        expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted &&
                   result.blocks_executed == 1u && result.instructions_executed == words.size(),
               "differential block must execute exactly once");
        expect_states_equal(expected, actual,
                            "dispatcher native state must match reference executor for all GPR halves");
    }

    {
        auto memory = make_memory({0u}, base, 6u);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::AnalysisFailure &&
                   result.next_pc == base && result.blocks_executed == 0u,
               "non-executable instruction fetch must fail analysis without progress");
        expect(result.message.find("analysis") != std::string::npos &&
                   result.message.find("0x00100000") != std::string::npos,
               "non-executable diagnostic must include stage and guest PC");
    }

    {
        auto memory = make_memory({0u}, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        const auto result = dispatcher.run(base + 0x1000u, state, 1u);
        expect(result.reason == R5900DispatchStopReason::AnalysisFailure &&
                   result.next_pc == base + 0x1000u && result.blocks_executed == 0u,
               "unmapped instruction fetch must fail without progress");
        expect(result.message.find("analysis") != std::string::npos &&
                   result.message.find("0x00101000") != std::string::npos,
               "unmapped diagnostic must include exact guest PC");
    }

    {
        auto memory = make_memory({0u}, base);
        R5900BlockDispatcherOptions options{};
        options.block_options.max_instructions = 0u;
        R5900BlockDispatcher dispatcher(memory, options);
        R5900IrExecutionState state{};
        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::AnalysisFailure &&
                   result.blocks_executed == 0u && result.next_pc == base,
               "invalid analyzer instruction limit must map to analysis failure");
        expect(result.message.find("analysis") != std::string::npos &&
                   result.message.find("0x00100000") != std::string::npos,
               "analyzer option failure must include stage and PC");
    }

    {
        const auto addiu_one = i_type(0x09, 0, 1, 1u);
        auto memory = make_memory({addiu_one}, base);
        R5900BlockDispatcherOptions options{};
        options.block_options.max_instructions = 1u;
        R5900BlockDispatcher dispatcher(memory, options);
        R5900IrExecutionState state{};
        const auto result = dispatcher.run(base, state, 2u);
        expect(result.reason == R5900DispatchStopReason::AnalysisFailure,
               "later unmapped analysis must fail after earlier progress");
        expect(result.blocks_executed == 1u && result.instructions_executed == 1u,
               "earlier native progress must remain committed");
        expect(state.gpr[1].low64 == 1u,
               "earlier state mutation must remain committed");
        expect(result.next_pc == base + 4u,
               "later analysis failure must report first unprocessed PC");
    }

    std::cout << "r5900_block_dispatcher_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
