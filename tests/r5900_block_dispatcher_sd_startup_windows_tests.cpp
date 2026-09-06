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
    std::cerr << "r5900_block_dispatcher_sd_startup_windows_tests: FAIL: "
              << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
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

b3r::runtime::Ps2MemoryMap make_startup_memory() {
    // Synthetic instructions generated from public ISA encodings, not ELF bytes.
    constexpr std::uint32_t entry = 0x001001e8u;
    constexpr std::uint32_t callee = 0x00115108u;
    constexpr std::uint32_t phoff = 52u;
    Bytes bytes(0x200u, 0u);
    bytes[0] = 0x7fu; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
    bytes[4] = 1u; bytes[5] = 1u; bytes[6] = 1u;
    put_u16(bytes, 16u, 2u);
    put_u16(bytes, 18u, 8u);
    put_u32(bytes, 20u, 1u);
    put_u32(bytes, 24u, entry);
    put_u32(bytes, 28u, phoff);
    put_u16(bytes, 40u, 52u);
    put_u16(bytes, 42u, 32u);
    put_u16(bytes, 44u, 2u);

    struct CodeSegment {
        std::uint32_t offset;
        std::uint32_t pc;
        std::vector<std::uint32_t> words;
    };
    const std::vector<CodeSegment> segments{
        {0x100u, entry, {j_type(0x03u, callee), 0u}},
        {0x120u, callee, {i_type(0x09u, 29u, 29u, 0xfff0u),
                         i_type(0x3fu, 29u, 31u, 0u),
                         j_type(0x03u, 0x00114ed0u), 0u}},
    };
    for (std::size_t i = 0u; i < segments.size(); ++i) {
        const auto& segment = segments[i];
        const auto ph = phoff + i * 32u;
        const auto size = static_cast<std::uint32_t>(segment.words.size() * 4u);
        put_u32(bytes, ph, 1u);
        put_u32(bytes, ph + 4u, segment.offset);
        put_u32(bytes, ph + 8u, segment.pc);
        put_u32(bytes, ph + 12u, segment.pc);
        put_u32(bytes, ph + 16u, size);
        put_u32(bytes, ph + 20u, size);
        put_u32(bytes, ph + 24u, 5u);
        put_u32(bytes, ph + 28u, 4u);
        for (std::size_t j = 0u; j < segment.words.size(); ++j) {
            put_u32(bytes, segment.offset + j * 4u, segment.words[j]);
        }
    }
    auto parsed = b3r::recompiler::parse_ps2_elf(bytes);
    expect(parsed.ok(), "two-code-segment startup ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "startup memory must load with physical EE stack backing");
    return std::move(*built.memory);
}

} // namespace

int main() {
    using namespace b3r::recompiler;
    auto memory = make_startup_memory();
    expect(memory.regions().size() == 2u &&
               memory.regions()[0].guest_base == 0x001001e8u &&
               memory.regions()[0].size == 8u &&
               memory.regions()[1].guest_base == 0x00115108u &&
               memory.regions()[1].size == 16u,
           "startup metadata must contain only the two synthetic code segments");
    R5900BlockDispatcher dispatcher(memory);
    for (const auto attempt : {0u, 1u}) {
        R5900IrExecutionState state{};
        state.gpr[29].low64 = 0x02000000u;
        state.gpr[31].high64 = 0x7777777777777777ull;
        expect(memory.write_u64(0x01fffff0u, 0u), "stack slot must reset between runs");
        expect(memory.write_u64(0x01fffff8u, 0xfeedfacefeedfaceull),
               "adjacent stack guard must initialize");
        const auto result = dispatcher.run(0x001001e8u, state, 2u);
        expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted &&
                   result.blocks_executed == 2u && result.instructions_executed == 6u,
               "startup must execute JAL/NOP then ADDIU/SD/JAL/NOP in two blocks");
        expect(result.next_pc == 0x00114ed0u,
               "startup must reach the second JAL target after the SD");
        expect(state.gpr[29].low64 == 0x01fffff0u,
               "startup stack pointer must reflect the 16-byte prologue");
        expect(memory.read_u64(0x01fffff0u).value_or(0u) == 0x001001f0u,
               "startup SD must save the entry JAL return address");
        expect(state.gpr[31].low64 == 0x00115118u &&
                   state.gpr[31].high64 == 0x7777777777777777ull,
               "startup final RA must come from the second JAL, preserving high64");
        expect(memory.read_u64(0x01fffff8u).value_or(0u) == 0xfeedfacefeedfaceull,
               "startup SD must leave the adjacent eight stack bytes intact");
        expect(dispatcher.cache_size() == 2u && result.recompilations == 0u &&
                   result.cache_misses == (attempt == 0u ? 2u : 0u) &&
                   result.cache_hits == (attempt == 0u ? 0u : 2u) &&
                   result.fast_cache_hits == (attempt == 0u ? 0u : 2u),
               "startup replay must hit both cached native blocks without recompiling");
    }
    std::cout << "r5900_block_dispatcher_sd_startup_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
