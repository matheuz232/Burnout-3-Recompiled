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
    std::cerr << "r5900_block_dispatcher_store64_windows_tests: FAIL: "
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
    put_u32(bytes, phoff + 28u, 0x1000u);

    for (std::size_t i = 0u; i < words.size(); ++i) {
        put_u32(bytes, code_offset + i * 4u, words[i]);
    }
    auto parsed = b3r::recompiler::parse_ps2_elf(bytes);
    expect(parsed.ok(), "dispatcher Store64 ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "dispatcher Store64 memory must map");
    return std::move(*built.memory);
}

} // namespace

int main() {
    using namespace b3r::recompiler;
    constexpr std::uint32_t code_base = 0x00115108u;
    const auto addiu_sp = i_type(0x09u, 29u, 29u, 0xfff0u);
    const auto sd_ra_sp = i_type(0x3fu, 29u, 31u, 0u);
    const auto jal = j_type(0x03u, 0x00114ed0u);

    {
        auto memory = make_memory({addiu_sp, sd_ra_sp, jal, 0u}, code_base);
        expect(memory.regions().size() == 1u,
               "stack backing must not require a fake data PT_LOAD");
        expect(memory.write_u64(0x01fffff8u, 0x1122334455667788ull),
               "stack guard must initialize");
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[29] = {0x02000000u, 0x2929292929292929ull};
        state.gpr[31] = {0x001001f0u, 0xababababababababull};
        const auto result = dispatcher.run(code_base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted,
               "ADDIU+SD+JAL must consume one native block budget");
        expect(result.next_pc == 0x00114ed0u &&
                   result.blocks_executed == 1u && result.instructions_executed == 4u,
               "successful prologue must count ADDIU, SD, JAL and delay NOP");
        expect(state.gpr[29].low64 == 0x01fffff0u &&
                   state.gpr[29].high64 == 0x2929292929292929ull,
               "stack prologue must decrement SP by 16 and preserve high64");
        expect(memory.read_u64(0x01fffff0u).value_or(0u) == 0x001001f0u,
               "SD must store old RA before JAL updates it");
        expect(memory.read_u64(0x01fffff8u).value_or(0u) == 0x1122334455667788ull,
               "SD must not write source high64 or adjacent stack bytes");
        expect(state.gpr[31].low64 == 0x00115118u &&
                   state.gpr[31].high64 == 0xababababababababull,
               "following JAL must update RA low64 only");
        expect(result.cache_misses == 1u && dispatcher.cache_size() == 1u,
               "successful prologue must compile one cache entry");
    }

    for (const auto address : {0x01fffff4u, 0x02000000u}) {
        auto memory = make_memory({sd_ra_sp, jal, 0u}, code_base);
        expect(memory.write_u128(0x01fffff0u, {0xaabbccdd00112233ull, 0x445566778899aabbull}),
               "fault guard must initialize");
        const auto before = memory.read_u128(0x01fffff0u);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[29].low64 = address;
        state.gpr[31] = {0x001001f0u, 0x3131313131313131ull};
        const auto result = dispatcher.run(code_base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::MemoryAccessFailure &&
                   result.next_pc == code_base,
               "misaligned or unmapped SD must report exact fault PC");
        expect(result.blocks_executed == 0u && result.instructions_executed == 0u,
               "faulting entry SD must complete no guest instruction");
        expect(state.gpr[31].low64 == 0x001001f0u &&
                   state.gpr[31].high64 == 0x3131313131313131ull &&
                   state.gpr[29].low64 == address,
               "failed SD must not execute the later JAL or change SP");
        expect(memory.read_u128(0x01fffff0u) == before,
               "failed SD must not mutate any adjacent stack byte");
        expect(result.message.find("store width 8 bytes") != std::string::npos &&
                   result.message.find(address == 0x01fffff4u ? "0x01fffff4" : "0x02000000") != std::string::npos,
               "SD diagnostic must include exact address and width 8");
    }

    // Cold fault, corrected fast-cache replay, then another fault on that cache.
    {
        auto memory = make_memory({addiu_sp, sd_ra_sp, jal, 0u}, code_base);
        R5900BlockDispatcher dispatcher(memory);
        for (const auto attempt : {0u, 1u, 2u}) {
            R5900IrExecutionState state{};
            state.gpr[29].low64 = attempt == 1u ? 0x02000000u : 0x02000004u;
            state.gpr[31].low64 = attempt == 1u ? 0x1122334455667788ull : 0x001001f0u;
            const auto before = memory.read_u128(0x01fffff0u);
            const auto result = dispatcher.run(code_base, state, 1u);
            if (attempt == 1u) {
                expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted &&
                           result.next_pc == 0x00114ed0u &&
                           result.blocks_executed == 1u && result.instructions_executed == 4u,
                       "corrected cached SD must execute the complete prologue");
                expect(memory.read_u64(0x01fffff0u).value_or(0u) == 0x1122334455667788ull &&
                           state.gpr[31].low64 == 0x00115118u,
                       "cached SD must read current source before JAL links");
            } else {
                expect(result.reason == R5900DispatchStopReason::MemoryAccessFailure &&
                           result.next_pc == 0x0011510cu && result.blocks_executed == 0u &&
                           result.instructions_executed == 1u,
                       "cold and cached faults must count only the completed ADDIU prefix");
                expect(state.gpr[29].low64 == 0x01fffff4u &&
                           state.gpr[31].low64 == 0x001001f0u &&
                           memory.read_u128(0x01fffff0u) == before,
                       "fault must preserve prefix effect while leaving RA and memory unchanged");
            }
            expect(result.cache_misses == (attempt == 0u ? 1u : 0u) &&
                       result.cache_hits == (attempt == 0u ? 0u : 1u) &&
                       result.fast_cache_hits == (attempt == 0u ? 0u : 1u) &&
                       result.recompilations == 0u && dispatcher.cache_size() == 1u,
                   "memory faults must retain reusable native code without recompilation");
        }
    }

    // SD + LD + JAL must execute as one native block and replay through fast cache.
    {
        const auto ld_saved = i_type(0x37u, 29u, 5u, 0u);
        auto memory = make_memory({sd_ra_sp, ld_saved, jal, 0u}, code_base);
        R5900BlockDispatcher dispatcher(memory);
        for (const auto value : {0x001001f0u, 0x001001f8u}) {
            R5900IrExecutionState state{};
            state.gpr[29].low64 = 0x01fffff0u;
            state.gpr[31].low64 = value;
            state.gpr[5] = {0xaaaaaaaaaaaaaaaaull, 0x5555666677778888ull};
            const auto result = dispatcher.run(code_base, state, 1u);
            expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted &&
                       result.next_pc == 0x00114ed0u && result.blocks_executed == 1u &&
                       result.instructions_executed == 4u,
                   "SD+LD+JAL must execute as one native block");
            expect(memory.read_u64(0x01fffff0u).value_or(0u) == value &&
                       state.gpr[5].low64 == value &&
                       state.gpr[5].high64 == 0x5555666677778888ull,
                   "dispatcher LD must read current guest RAM into low64 only");
            expect(state.gpr[31].low64 == 0x00115118u,
                   "JAL after LD must still publish its architectural link");
            expect(result.cache_misses == (value == 0x001001f0u ? 1u : 0u) &&
                       result.cache_hits == (value == 0x001001f0u ? 0u : 1u) &&
                       result.fast_cache_hits == (value == 0x001001f0u ? 0u : 1u),
                   "supported LD block must replay through fast cache");
        }
    }

    // LD faults must report load semantics and preserve the destination.
    {
        const auto ld_saved = i_type(0x37u, 29u, 5u, 0u);
        for (const auto address : {0x01fffff4u, 0x02000000u}) {
            auto memory = make_memory({ld_saved, jal, 0u}, code_base);
            R5900BlockDispatcher dispatcher(memory);
            R5900IrExecutionState state{};
            state.gpr[29].low64 = address;
            state.gpr[5] = {0x123456789abcdef0ull, 0x0fedcba987654321ull};
            const auto before = state.gpr[5];
            const auto result = dispatcher.run(code_base, state, 1u);
            expect(result.reason == R5900DispatchStopReason::MemoryAccessFailure &&
                       result.next_pc == code_base && result.blocks_executed == 0u &&
                       result.instructions_executed == 0u,
                   "misaligned or unmapped LD must stop at its exact PC");
            expect(state.gpr[5].low64 == before.low64 &&
                       state.gpr[5].high64 == before.high64,
                   "failed dispatcher LD must preserve destination transactionally");
            expect(result.message.find("load width 8 bytes") != std::string::npos &&
                       result.message.find(address == 0x01fffff4u ? "0x01fffff4" : "0x02000000") != std::string::npos,
                   "LD diagnostic must include load kind, exact address and width 8");
        }
    }

    std::cout << "r5900_block_dispatcher_store64_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
