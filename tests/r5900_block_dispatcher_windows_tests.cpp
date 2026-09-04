#include "recompiler/ps2_elf.h"
#include "recompiler/windows/r5900_block_dispatcher.h"
#include "runtime/ps2_memory_map.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
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
            i_type(0x0c, 1, 2, 0x00ff),
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
               "supported prefix must stop before ANDI");
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
               "unsupported ANDI must not execute");
    }

    {
        const auto andi = i_type(0x0c, 1, 2, 0x00ff);
        auto memory = make_memory({andi}, base);
        R5900BlockDispatcherOptions options{};
        options.block_options.max_instructions = 1u;
        R5900BlockDispatcher dispatcher(memory, options);
        R5900IrExecutionState state{};
        state.gpr[2] = {0x1234u, 0x5678u};

        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction,
               "ANDI at entry must be an unsupported boundary");
        expect(result.next_pc == base && result.blocks_executed == 0u &&
                   result.instructions_executed == 0u,
               "unsupported entry must execute nothing");
        expect(state.gpr[2].low64 == 0x1234u && state.gpr[2].high64 == 0x5678u,
               "unsupported entry must preserve state");
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

    std::cout << "r5900_block_dispatcher_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
