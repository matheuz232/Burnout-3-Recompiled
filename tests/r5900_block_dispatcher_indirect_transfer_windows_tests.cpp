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
    std::cerr << "r5900_block_dispatcher_indirect_transfer_windows_tests: FAIL: "
              << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) fail(message);
}

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset + 0u] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0u] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
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

b3r::runtime::Ps2MemoryMap make_memory(const std::vector<std::uint32_t>& words,
                                       std::uint32_t code_base,
                                       std::uint32_t data_base = 0x00240000u) {
    constexpr std::uint32_t phoff = 52u;
    constexpr std::uint32_t phsize = 32u;
    constexpr std::uint32_t code_offset = 0x100u;
    constexpr std::uint32_t data_offset = 0x500u;
    constexpr std::uint32_t data_file_size = 0x80u;
    constexpr std::uint32_t data_memory_size = 0x100u;
    const auto code_size = static_cast<std::uint32_t>(words.size() * 4u);
    expect(code_offset + code_size <= data_offset,
           "indirect-transfer code must not overlap data segment");

    Bytes bytes(data_offset + data_file_size, 0u);
    bytes[0] = 0x7fu; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
    bytes[4] = 1u; bytes[5] = 1u; bytes[6] = 1u;
    put_u16(bytes, 16u, 2u);
    put_u16(bytes, 18u, 8u);
    put_u32(bytes, 20u, 1u);
    put_u32(bytes, 24u, code_base);
    put_u32(bytes, 28u, phoff);
    put_u16(bytes, 40u, 52u);
    put_u16(bytes, 42u, phsize);
    put_u16(bytes, 44u, 2u);

    put_u32(bytes, phoff + 0u, 1u);
    put_u32(bytes, phoff + 4u, code_offset);
    put_u32(bytes, phoff + 8u, code_base);
    put_u32(bytes, phoff + 12u, code_base);
    put_u32(bytes, phoff + 16u, code_size);
    put_u32(bytes, phoff + 20u, code_size);
    put_u32(bytes, phoff + 24u, 5u);
    put_u32(bytes, phoff + 28u, 0x1000u);

    const auto data_ph = phoff + phsize;
    put_u32(bytes, data_ph + 0u, 1u);
    put_u32(bytes, data_ph + 4u, data_offset);
    put_u32(bytes, data_ph + 8u, data_base);
    put_u32(bytes, data_ph + 12u, data_base);
    put_u32(bytes, data_ph + 16u, data_file_size);
    put_u32(bytes, data_ph + 20u, data_memory_size);
    put_u32(bytes, data_ph + 24u, 6u);
    put_u32(bytes, data_ph + 28u, 0x1000u);

    for (std::size_t i = 0; i < words.size(); ++i) {
        put_u32(bytes, code_offset + i * 4u, words[i]);
    }
    for (std::size_t i = 0; i < data_file_size; ++i) {
        bytes[data_offset + i] = 0xa5u;
    }

    auto parsed = b3r::recompiler::parse_ps2_elf(bytes);
    expect(parsed.ok(), "indirect-transfer ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "indirect-transfer ELF must map");
    return std::move(*built.memory);
}

std::vector<std::uint32_t> jr_fixture(std::uint32_t base) {
    (void)base;
    std::vector<std::uint32_t> words(40u, 0u);
    words[0] = r_type(5u, 0u, 0u, 0u, 0x08u);            // JR r5
    words[1] = i_type(0x09u, 0u, 8u, 0x11u);             // delay
    words[8] = i_type(0x09u, 0u, 20u, 0x22u);            // target A body
    words[9] = i_type(0x05u, 0u, 0u, 0u);                // BNE boundary
    words[10] = 0u;
    words[16] = i_type(0x09u, 0u, 21u, 0x33u);           // target B body
    words[17] = i_type(0x05u, 0u, 0u, 0u);               // BNE boundary
    words[18] = 0u;
    return words;
}
} // namespace

int main() {
    using namespace b3r::recompiler;
    constexpr std::uint32_t base = 0x00120000u;
    constexpr std::uint32_t target_a = base + 0x20u;
    constexpr std::uint32_t target_b = base + 0x40u;

    {
        auto memory = make_memory(jr_fixture(base), base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[5] = {target_a, 0xfeedfacefeedfaceull};
        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted &&
                   result.next_pc == target_a,
               "entry JR must execute and return dynamic target");
        expect(result.blocks_executed == 1u && result.instructions_executed == 2u,
               "entry JR must count terminator plus delay");
        expect(state.gpr[8].low64 == 0x11u,
               "JR delay must execute exactly once");
    }

    {
        auto words = jr_fixture(base);
        words[0] = i_type(0x09u, 0u, 7u, 5u);             // body
        words[1] = r_type(5u, 0u, 0u, 0u, 0x08u);        // JR r5
        words[2] = i_type(0x09u, 0u, 8u, 6u);            // delay
        auto memory = make_memory(words, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[5].low64 = target_a;
        const auto result = dispatcher.run(base, state, 1u);
        expect(result.next_pc == target_a && result.blocks_executed == 1u &&
                   result.instructions_executed == 3u,
               "body+JR must execute as one block");
        expect(state.gpr[7].low64 == 5u && state.gpr[8].low64 == 6u,
               "body and JR delay effects mismatch");
    }

    {
        std::vector<std::uint32_t> words(40u, 0u);
        words[0] = r_type(5u, 0u, 5u, 0u, 0x09u);        // JALR r5,r5
        words[1] = i_type(0x09u, 5u, 6u, 0u);            // delay sees link
        words[8] = i_type(0x05u, 0u, 0u, 0u);            // BNE target boundary
        words[9] = 0u;
        auto memory = make_memory(words, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[5] = {target_a, 0x0123456789abcdefull};
        const auto result = dispatcher.run(base, state, 1u);
        expect(result.next_pc == target_a && result.blocks_executed == 1u &&
                   result.instructions_executed == 2u,
               "JALR rd==rs must jump using pre-link target");
        expect(state.gpr[5].low64 == base + 8u &&
                   state.gpr[5].high64 == 0x0123456789abcdefull,
               "dispatcher JALR link/high64 mismatch");
        expect(state.gpr[6].low64 == base + 8u,
               "JALR delay must observe new link");
    }

    {
        auto memory = make_memory(jr_fixture(base), base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState first_state{};
        first_state.gpr[5].low64 = target_a;
        const auto first = dispatcher.run(base, first_state, 1u);
        expect(first.cache_misses == 1u && first.cache_hits == 0u &&
                   first.next_pc == target_a,
               "first JR run must compile and miss cache");

        R5900IrExecutionState second_state{};
        second_state.gpr[5].low64 = target_b;
        const auto second = dispatcher.run(base, second_state, 1u);
        expect(second.cache_hits == 1u && second.recompilations == 0u &&
                   second.next_pc == target_b,
               "cached JR must use changed runtime target without recompilation");

        expect(memory.write_u32(base + 8u, i_type(0x09u, 0u, 30u, 9u)),
               "post-delay mutation must succeed");
        R5900IrExecutionState third_state{};
        third_state.gpr[5].low64 = target_a;
        const auto third = dispatcher.run(base, third_state, 1u);
        expect(third.cache_hits == 1u && third.recompilations == 0u,
               "word at terminator+8 must be outside cached indirect span");

        expect(memory.write_u32(base + 4u, i_type(0x09u, 0u, 8u, 0x44u)),
               "JR delay mutation must succeed");
        R5900IrExecutionState fourth_state{};
        fourth_state.gpr[5].low64 = target_a;
        const auto fourth = dispatcher.run(base, fourth_state, 1u);
        expect(fourth.recompilations == 1u && fourth_state.gpr[8].low64 == 0x44u,
               "JR delay mutation must recompile cache entry");

        expect(memory.write_u32(base, r_type(6u, 0u, 0u, 0u, 0x08u)),
               "JR terminator mutation must succeed");
        R5900IrExecutionState fifth_state{};
        fifth_state.gpr[5].low64 = target_a;
        fifth_state.gpr[6].low64 = target_b;
        const auto fifth = dispatcher.run(base, fifth_state, 1u);
        expect(fifth.recompilations == 1u && fifth.next_pc == target_b,
               "JR terminator mutation must recompile and use new source GPR");
    }

    for (bool jalr : {false, true}) {
        constexpr std::uint32_t data_base = 0x00240000u;
        constexpr std::uint32_t store_target = data_base + 0x20u;
        std::vector<std::uint32_t> words(8u, 0u);
        words[0] = jalr
            ? r_type(5u, 0u, 9u, 0u, 0x09u)
            : r_type(5u, 0u, 0u, 0u, 0x08u);
        words[1] = i_type(0x1fu, 2u, 7u, 0u);            // SQ delay
        auto memory = make_memory(words, base, data_base);
        const auto before = memory.read_u128(store_target);
        expect(before.has_value(), "SQ delay target must be mapped");
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[5].low64 = target_a;
        state.gpr[2].low64 = store_target;
        state.gpr[7] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::LoweringFailure &&
                   result.next_pc == base + 4u &&
                   result.blocks_executed == 0u &&
                   result.instructions_executed == 0u,
               "SQ in JR/JALR delay must fail before guest progress");
        expect(dispatcher.cache_size() == 0u,
               "rejected indirect delay must not populate cache");
        const auto after = memory.read_u128(store_target);
        expect(after.has_value() && *after == *before,
               "rejected SQ indirect delay must not mutate memory");
    }

    for (const std::uint32_t bad_target : {base + 2u, 0x00f00000u}) {
        auto memory = make_memory(jr_fixture(base), base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[5].low64 = bad_target;
        const auto result = dispatcher.run(base, state, 2u);
        expect(result.reason == R5900DispatchStopReason::AnalysisFailure &&
                   result.next_pc == bad_target,
               "invalid indirect target must fail on following analysis");
        expect(result.blocks_executed == 1u && result.instructions_executed == 2u,
               "completed JR block must stay counted before target analysis failure");
        expect(state.gpr[8].low64 == 0x11u,
               "JR delay effects must remain committed before target failure");
    }

    std::cout << "r5900_block_dispatcher_indirect_transfer_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
