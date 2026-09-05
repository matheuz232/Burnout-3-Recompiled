from pathlib import Path

test = Path("tests/r5900_block_dispatcher_branch_likely_windows_tests.cpp")
test.write_text(r'''#include "recompiler/ps2_elf.h"
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
using namespace b3r::recompiler;
using Bytes = std::vector<std::uint8_t>;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_block_dispatcher_branch_likely_windows_tests: FAIL: "
              << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) fail(message);
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
    const auto payload_size = static_cast<std::uint32_t>(words.size() * 4u);
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

    for (std::size_t i = 0; i < words.size(); ++i) {
        put_u32(bytes,
                static_cast<std::size_t>(kPayloadOffset) + i * 4u,
                words[i]);
    }

    auto parsed = b3r::recompiler::parse_ps2_elf(bytes);
    expect(parsed.ok(), "synthetic likely dispatcher ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "synthetic likely dispatcher memory must map");
    return std::move(*built.memory);
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
    constexpr std::uint32_t base = 0x00112000u;
    const auto beql = i_type(0x14u, 1u, 2u, 2u);
    const auto bnel = i_type(0x15u, 1u, 2u, 2u);
    const auto delay_one = i_type(0x09u, 8u, 8u, 1u);
    const auto delay_two = i_type(0x09u, 8u, 8u, 2u);

    // BEQL at block entry: taken executes delay; same cached block with a
    // changed runtime predicate annuls the delay and falls through.
    {
        auto memory = make_memory({beql, delay_one, 0u, 0u, 0u}, base);
        R5900BlockDispatcher dispatcher(memory);

        R5900IrExecutionState taken{};
        taken.gpr[1].low64 = 7u;
        taken.gpr[2].low64 = 7u;
        const auto first = dispatcher.run(base, taken, 1u);
        expect(first.reason == R5900DispatchStopReason::BlockBudgetExhausted,
               "BEQL at entry must execute as a supported terminator");
        expect(first.next_pc == base + 12u && taken.gpr[8].low64 == 1u,
               "taken BEQL must execute delay and branch to target");
        expect(first.blocks_executed == 1u && first.instructions_executed == 2u,
               "taken BEQL accounting must count terminator plus selected delay word");
        expect(first.cache_misses == 1u && first.cache_hits == 0u &&
                   first.recompilations == 0u && dispatcher.cache_size() == 1u,
               "first BEQL execution must compile one cache entry");

        R5900IrExecutionState not_taken{};
        not_taken.gpr[1].low64 = 7u;
        not_taken.gpr[2].low64 = 8u;
        const auto second = dispatcher.run(base, not_taken, 1u);
        expect(second.reason == R5900DispatchStopReason::BlockBudgetExhausted &&
                   second.next_pc == base + 8u,
               "not-taken BEQL must fall through after annulled delay");
        expect(not_taken.gpr[8].low64 == 0u,
               "not-taken BEQL must produce no delay register effect");
        expect(second.instructions_executed == 2u,
               "annulled BEQL must preserve selected-guest-word accounting");
        expect(second.cache_hits == 1u && second.cache_misses == 0u &&
                   second.recompilations == 0u && dispatcher.cache_size() == 1u,
               "changed BEQL runtime predicate must reuse cached native block");
    }

    // BNEL uses the opposite predicate polarity without swapping architectural
    // targets: unequal is taken and executes delay, equal is not-taken/annulled.
    {
        auto memory = make_memory({bnel, delay_one, 0u, 0u, 0u}, base);
        R5900BlockDispatcher dispatcher(memory);

        R5900IrExecutionState taken{};
        taken.gpr[1].low64 = 3u;
        taken.gpr[2].low64 = 4u;
        const auto first = dispatcher.run(base, taken, 1u);
        expect(first.reason == R5900DispatchStopReason::BlockBudgetExhausted &&
                   first.next_pc == base + 12u && taken.gpr[8].low64 == 1u,
               "taken BNEL must execute delay and branch to target");
        expect(first.instructions_executed == 2u && first.cache_misses == 1u,
               "taken BNEL accounting/cache mismatch");

        R5900IrExecutionState not_taken{};
        not_taken.gpr[1].low64 = 4u;
        not_taken.gpr[2].low64 = 4u;
        const auto second = dispatcher.run(base, not_taken, 1u);
        expect(second.next_pc == base + 8u && not_taken.gpr[8].low64 == 0u,
               "not-taken BNEL must annul delay and fall through");
        expect(second.instructions_executed == 2u && second.cache_hits == 1u,
               "not-taken BNEL must retain guest-word accounting and hit cache");
    }

    // Straight-line body must remain part of the same native block.
    {
        const auto body = i_type(0x09u, 0u, 9u, 5u);
        auto memory = make_memory({body, beql, delay_one, 0u, 0u, 0u}, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 6u;
        state.gpr[2].low64 = 6u;
        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted &&
                   result.next_pc == base + 16u,
               "body+BEQL block must take branch using branch-site target");
        expect(state.gpr[9].low64 == 5u && state.gpr[8].low64 == 1u,
               "body+BEQL block must execute body then taken delay");
        expect(result.blocks_executed == 1u && result.instructions_executed == 3u,
               "body+BEQL accounting must count body, terminator, delay words");
    }

    // Delay mutation must invalidate the fingerprint even though runtime state
    // is the only thing deciding whether that delay executes.
    {
        auto memory = make_memory({beql, delay_one, 0u, 0u, 0u}, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState first_state{};
        first_state.gpr[1].low64 = 1u;
        first_state.gpr[2].low64 = 1u;
        const auto first = dispatcher.run(base, first_state, 1u);
        expect(first.cache_misses == 1u && first_state.gpr[8].low64 == 1u,
               "initial BEQL delay must compile and execute");

        expect(memory.write_u32(base + 4u, delay_two),
               "likely delay mutation must succeed");
        R5900IrExecutionState second_state{};
        second_state.gpr[1].low64 = 1u;
        second_state.gpr[2].low64 = 1u;
        const auto second = dispatcher.run(base, second_state, 1u);
        expect(second.recompilations == 1u && second.cache_hits == 0u &&
                   second.cache_misses == 0u,
               "changed likely delay word must recompile cached native block");
        expect(second_state.gpr[8].low64 == 2u,
               "recompiled likely block must execute mutated delay");
    }

    // BEQL -> BNEL terminator mutation must recompile and change predicate
    // semantics without altering target encoding or delay word.
    {
        auto memory = make_memory({beql, delay_one, 0u, 0u, 0u}, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState first_state{};
        first_state.gpr[1].low64 = 9u;
        first_state.gpr[2].low64 = 9u;
        const auto first = dispatcher.run(base, first_state, 1u);
        expect(first.cache_misses == 1u && first.next_pc == base + 12u &&
                   first_state.gpr[8].low64 == 1u,
               "initial BEQL fixture must be taken");

        expect(memory.write_u32(base, bnel),
               "BEQL-to-BNEL terminator mutation must succeed");
        R5900IrExecutionState second_state{};
        second_state.gpr[1].low64 = 9u;
        second_state.gpr[2].low64 = 9u;
        const auto second = dispatcher.run(base, second_state, 1u);
        expect(second.recompilations == 1u && second.cache_hits == 0u &&
                   second.next_pc == base + 8u,
               "BEQL-to-BNEL mutation must recompile and become not-taken");
        expect(second_state.gpr[8].low64 == 0u,
               "mutated equal BNEL must annul delay");
    }

    // SQ remains outside dispatcher-managed branch-delay scope even when the
    // runtime likely predicate would have annulled it.
    {
        const auto sq_delay = i_type(0x1fu, 3u, 4u, 0u);
        auto memory = make_memory({beql, sq_delay, 0u, 0u, 0u}, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 1u;
        state.gpr[2].low64 = 2u; // BEQL would be not-taken at runtime.
        state.gpr[3].low64 = 0x00400000u;
        state.gpr[4] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
        const auto before = state;
        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::LoweringFailure &&
                   result.next_pc == base + 4u && result.blocks_executed == 0u &&
                   result.instructions_executed == 0u,
               "SQ in likely delay must fail lowering before guest progress");
        expect(result.message.find("BEQ/BNE/BEQL/BNEL") != std::string::npos,
               "likely SQ-delay diagnostic must identify branch family");
        for (std::size_t i = 0; i < state.gpr.size(); ++i) {
            expect(state.gpr[i].low64 == before.gpr[i].low64 &&
                       state.gpr[i].high64 == before.gpr[i].high64,
                   "SQ likely-delay rejection must preserve CPU state");
        }
    }

    std::cout << "r5900_block_dispatcher_branch_likely_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
''', encoding="utf-8")

cmake = Path("CMakeLists.txt")
text = cmake.read_text(encoding="utf-8")
anchor = "    add_executable(r5900_block_dispatcher_startup_windows_tests\n"
addition = """    add_executable(r5900_block_dispatcher_branch_likely_windows_tests\n      tests/r5900_block_dispatcher_branch_likely_windows_tests.cpp\n    )\n    target_link_libraries(r5900_block_dispatcher_branch_likely_windows_tests PRIVATE\n      b3r_recompiler_dispatcher_x64\n    )\n    add_test(NAME r5900_block_dispatcher_branch_likely_windows_tests\n      COMMAND r5900_block_dispatcher_branch_likely_windows_tests)\n\n"""
if anchor not in text:
    raise SystemExit("CMake dispatcher insertion anchor not found")
cmake.write_text(text.replace(anchor, addition + anchor, 1), encoding="utf-8")
