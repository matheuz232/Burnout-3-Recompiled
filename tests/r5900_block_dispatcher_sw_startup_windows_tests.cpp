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
    std::cerr << "r5900_block_dispatcher_sw_startup_windows_tests: FAIL: "
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

constexpr std::uint32_t i_type(std::uint8_t op,
                               std::uint8_t rs,
                               std::uint8_t rt,
                               std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           imm;
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

constexpr std::uint32_t j_type(std::uint8_t op, std::uint32_t target) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           ((target >> 2u) & 0x03ffffffu);
}

b3r::runtime::Ps2MemoryMap make_memory() {
    constexpr std::uint32_t startup = 0x00114ed0u;
    constexpr std::uint32_t cache_pc = 0x00131000u;
    constexpr std::uint32_t phoff = 52u;

    Bytes bytes(0x240u, 0u);
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
    put_u32(bytes, 24u, startup);
    put_u32(bytes, 28u, phoff);
    put_u16(bytes, 40u, 52u);
    put_u16(bytes, 42u, 32u);
    put_u16(bytes, 44u, 2u);

    struct Segment {
        std::uint32_t offset;
        std::uint32_t pc;
        std::vector<std::uint32_t> words;
    };

    const std::vector<Segment> segments{
        {0x100u,
         startup,
         {i_type(0x09u, 29u, 29u, 0xffb0u),
          i_type(0x09u, 0u, 2u, 1u),
          i_type(0x3fu, 29u, 31u, 0x0040u),
          r_type(29u, 0u, 4u, 0u, 0x2du),
          i_type(0x2bu, 29u, 2u, 0x0028u),
          0x70000000u}},
        {0x160u,
         cache_pc,
         {i_type(0x2bu, 5u, 6u, 0x0004u),
          j_type(0x02u, 0x00131010u),
          0u}},
    };

    for (std::size_t i = 0u; i < segments.size(); ++i) {
        const auto& segment = segments[i];
        const auto ph = phoff + i * 32u;
        const auto size = static_cast<std::uint32_t>(segment.words.size() * 4u);
        put_u32(bytes, ph + 0u, 1u);
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
    expect(parsed.ok(), "synthetic SW startup ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "synthetic SW startup memory must map");
    return std::move(*built.memory);
}

} // namespace

int main() {
    using namespace b3r::recompiler;
    auto memory = make_memory();
    R5900BlockDispatcher dispatcher(memory);

    {
        R5900IrExecutionState state{};
        state.gpr[29].low64 = 0x01fffff0u;
        state.gpr[31].low64 = 0x00115118u;
        state.gpr[4].high64 = 0xa0a0a0a0b0b0b0b0ull;

        expect(memory.write_u64(0x01ffffe0u, 0u),
               "startup RA slot must initialize");
        expect(memory.write_u32(0x01ffffc4u, 0xa1b2c3d4u),
               "startup lower guard must initialize");
        expect(memory.write_u32(0x01ffffc8u, 0xccccccccu),
               "startup SW slot must initialize");
        expect(memory.write_u32(0x01ffffccu, 0xd4c3b2a1u),
               "startup upper guard must initialize");

        const auto result = dispatcher.run(0x00114ed0u, state, 4u);
        expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction,
               "dispatcher must stop at the synthetic post-SW sentinel");
        expect(result.next_pc == 0x00114ee4u &&
                   result.instructions_executed == 5u,
               "startup prefix must cross SW before the synthetic sentinel");
        expect(state.gpr[29].low64 == 0x01ffffa0u,
               "startup SP mismatch after ADDIU");
        expect(state.gpr[2].low64 == 1u,
               "startup v0 mismatch");
        expect(state.gpr[4].low64 == 0x01ffffa0u &&
                   state.gpr[4].high64 == 0xa0a0a0a0b0b0b0b0ull,
               "startup DADDU result/high64 mismatch");
        expect(memory.read_u64(0x01ffffe0u).value_or(0u) == 0x00115118u,
               "startup SD must preserve the return address");
        expect(memory.read_u32(0x01ffffc8u).value_or(0u) == 1u,
               "startup SW must store v0 low32");
        expect(memory.read_u32(0x01ffffc4u).value_or(0u) == 0xa1b2c3d4u &&
                   memory.read_u32(0x01ffffccu).value_or(0u) == 0xd4c3b2a1u,
               "startup SW must preserve adjacent guards");
    }

    constexpr std::uint32_t cache_pc = 0x00131000u;
    constexpr std::uint32_t data_base = 0x00140000u;
    expect(memory.write_u32(data_base + 0u, 0x11112222u),
           "cache lower guard must initialize");
    expect(memory.write_u32(data_base + 4u, 0xccccccccu),
           "cache SW slot must initialize");
    expect(memory.write_u32(data_base + 8u, 0x33334444u),
           "cache upper guard must initialize");

    for (const auto attempt : {0u, 1u}) {
        R5900IrExecutionState state{};
        state.gpr[5].low64 = data_base;
        state.gpr[6].low64 = 0xfeedface11223344ull;
        const auto result = dispatcher.run(cache_pc, state, 1u);

        expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted &&
                   result.next_pc == 0x00131010u &&
                   result.instructions_executed == 3u,
               "SW cache fixture must execute SW/J/NOP");
        expect(memory.read_u32(data_base + 4u).value_or(0u) == 0x11223344u,
               "cached SW must store low32 only");
        expect(memory.read_u32(data_base + 0u).value_or(0u) == 0x11112222u &&
                   memory.read_u32(data_base + 8u).value_or(0u) == 0x33334444u,
               "cached SW must preserve guards");
        expect(result.cache_misses == (attempt == 0u ? 1u : 0u) &&
                   result.cache_hits == (attempt == 0u ? 0u : 1u) &&
                   result.fast_cache_hits == (attempt == 0u ? 0u : 1u) &&
                   result.recompilations == 0u,
               "second SW cache run must fast-replay without recompilation");
    }

    std::cout << "r5900_block_dispatcher_sw_startup_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
