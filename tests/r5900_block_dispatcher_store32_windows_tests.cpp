#include "recompiler/ps2_elf.h"
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
    std::cerr << "r5900_block_dispatcher_store32_windows_tests: FAIL: "
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

constexpr std::uint32_t j_type(std::uint8_t op, std::uint32_t target) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           ((target >> 2u) & 0x03ffffffu);
}

b3r::runtime::Ps2MemoryMap make_memory(
    const std::vector<std::uint32_t>& words,
    std::uint32_t code_base) {
    constexpr std::uint32_t phoff = 52u;
    constexpr std::uint32_t code_offset = 0x100u;
    const auto code_size = static_cast<std::uint32_t>(words.size() * 4u);

    Bytes bytes(0x400u, 0u);
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
    put_u32(bytes, 24u, code_base);
    put_u32(bytes, 28u, phoff);
    put_u16(bytes, 40u, 52u);
    put_u16(bytes, 42u, 32u);
    put_u16(bytes, 44u, 1u);

    put_u32(bytes, phoff + 0u, 1u);
    put_u32(bytes, phoff + 4u, code_offset);
    put_u32(bytes, phoff + 8u, code_base);
    put_u32(bytes, phoff + 12u, code_base);
    put_u32(bytes, phoff + 16u, code_size);
    put_u32(bytes, phoff + 20u, code_size);
    put_u32(bytes, phoff + 24u, 5u);
    put_u32(bytes, phoff + 28u, 4u);

    for (std::size_t i = 0u; i < words.size(); ++i) {
        put_u32(bytes, code_offset + i * 4u, words[i]);
    }

    auto parsed = b3r::recompiler::parse_ps2_elf(bytes);
    expect(parsed.ok(), "dispatcher Store32 ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "dispatcher Store32 memory must map");
    return std::move(*built.memory);
}

} // namespace

int main() {
    using namespace b3r::recompiler;
    constexpr std::uint32_t code_base = 0x00132000u;
    constexpr std::uint32_t data_base = 0x00140000u;
    const auto sw = i_type(0x2bu, 29u, 2u, 0x0004u);
    const auto jump = j_type(0x02u, 0x00132010u);

    {
        auto memory = make_memory({sw, jump, 0u}, code_base);
        expect(memory.write_u32(data_base + 0u, 0xa1b2c3d4u),
               "lower Store32 guard must initialize");
        expect(memory.write_u32(data_base + 4u, 0xccccccccu),
               "Store32 slot must initialize");
        expect(memory.write_u32(data_base + 8u, 0xd4c3b2a1u),
               "upper Store32 guard must initialize");

        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[29].low64 = data_base;
        state.gpr[2].low64 = 0xfeedface11223344ull;
        const auto result = dispatcher.run(code_base, state, 1u);

        expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted,
               "SW/J/NOP must consume one native block budget");
        expect(result.next_pc == 0x00132010u &&
                   result.blocks_executed == 1u &&
                   result.instructions_executed == 3u,
               "SW/J/NOP accounting or target mismatch");
        expect(memory.read_u32(data_base + 4u).value_or(0u) == 0x11223344u,
               "SW must store source low32 at base+4");
        expect(memory.read_u32(data_base + 0u).value_or(0u) == 0xa1b2c3d4u &&
                   memory.read_u32(data_base + 8u).value_or(0u) == 0xd4c3b2a1u,
               "SW must preserve adjacent four-byte guards");
        expect(result.cache_misses == 1u && dispatcher.cache_size() == 1u,
               "successful SW block must create one cache entry");
    }

    for (const auto address : {0x00001002u, 0x02000000u}) {
        auto memory = make_memory({sw, jump, 0u}, code_base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[29].low64 = static_cast<std::uint64_t>(address - 4u);
        state.gpr[2].low64 = 0x11223344u;
        const auto result = dispatcher.run(code_base, state, 1u);

        expect(result.reason == R5900DispatchStopReason::MemoryAccessFailure &&
                   result.next_pc == code_base,
               "entry SW fault must report exact store PC");
        expect(result.blocks_executed == 0u && result.instructions_executed == 0u,
               "faulting entry SW must complete no guest instruction");
        expect(result.message.find("store width 4 bytes") != std::string::npos,
               "SW fault diagnostic must report width 4");
    }

    {
        const auto addiu = i_type(0x09u, 29u, 29u, 0xfffeu);
        const auto sw_zero = i_type(0x2bu, 29u, 2u, 0u);
        auto memory = make_memory(
            {addiu, sw_zero, j_type(0x02u, 0x00132020u), 0u}, code_base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[29].low64 = 0x00001004u;
        state.gpr[2].low64 = 0x55667788u;
        const auto result = dispatcher.run(code_base, state, 1u);

        expect(result.reason == R5900DispatchStopReason::MemoryAccessFailure &&
                   result.next_pc == code_base + 4u &&
                   result.instructions_executed == 1u,
               "SW prefix fault must count only completed ADDIU");
        expect(state.gpr[29].low64 == 0x00001002u,
               "ADDIU prefix effect must remain visible on SW fault");
    }

    {
        const auto sw_zero = i_type(0x2bu, 29u, 2u, 0u);
        auto memory = make_memory(
            {sw_zero, j_type(0x02u, 0x00132020u), 0u}, code_base);
        R5900BlockDispatcher dispatcher(memory);
        for (const auto attempt : {0u, 1u, 2u}) {
            R5900IrExecutionState state{};
            state.gpr[29].low64 = attempt == 1u ? 0x00001004u : 0x00001002u;
            state.gpr[2].low64 = 0xaabbccdd11223344ull;
            const auto result = dispatcher.run(code_base, state, 1u);

            if (attempt == 1u) {
                expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted &&
                           result.next_pc == 0x00132020u &&
                           result.instructions_executed == 3u,
                       "corrected cached SW must execute SW/J/NOP");
                expect(memory.read_u32(0x00001004u).value_or(0u) == 0x11223344u,
                       "cached SW must store the current low32 source");
            } else {
                expect(result.reason == R5900DispatchStopReason::MemoryAccessFailure &&
                           result.next_pc == code_base &&
                           result.instructions_executed == 0u,
                       "cold and cached SW faults must stop at the store");
            }

            expect(result.cache_misses == (attempt == 0u ? 1u : 0u) &&
                       result.cache_hits == (attempt == 0u ? 0u : 1u) &&
                       result.fast_cache_hits == (attempt == 0u ? 0u : 1u) &&
                       result.recompilations == 0u && dispatcher.cache_size() == 1u,
                   "SW faults must retain reusable native code without recompilation");
        }
    }

    {
        const auto sw_zero = i_type(0x2bu, 29u, 2u, 0u);
        const auto ld = i_type(0x37u, 29u, 3u, 0u);
        auto memory = make_memory(
            {sw_zero, ld, j_type(0x02u, 0x00132030u), 0u}, code_base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[29].low64 = data_base;
        state.gpr[2].low64 = 0xdeadbeef55667788ull;
        state.gpr[3].high64 = 0x3333333333333333ull;
        const auto result = dispatcher.run(code_base, state, 1u);

        expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted &&
                   result.next_pc == 0x00132030u &&
                   result.blocks_executed == 1u &&
                   result.instructions_executed == 4u,
               "SW+LD+J/NOP must execute as one supported native block");
        expect(memory.read_u32(data_base).value_or(0u) == 0x55667788u,
               "SW before LD must store current low32 source");
        expect(state.gpr[3].low64 == 0x0000000055667788ull &&
                   state.gpr[3].high64 == 0x3333333333333333ull,
               "LD after SW must observe guest RAM and preserve destination high64");
    }

    std::cout << "r5900_block_dispatcher_store32_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
