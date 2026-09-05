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
    std::cerr << "r5900_block_dispatcher_direct_transfer_windows_tests: FAIL: "
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

constexpr std::uint32_t j_type(std::uint8_t op, std::uint32_t target) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           ((target >> 2u) & 0x03ffffffu);
}

b3r::runtime::Ps2MemoryMap make_memory(const std::vector<std::uint32_t>& words,
                                       std::uint32_t code_base,
                                       std::uint32_t data_base = 0x00220000u) {
    constexpr std::uint32_t phoff = 52u;
    constexpr std::uint32_t phsize = 32u;
    constexpr std::uint32_t code_offset = 0x100u;
    constexpr std::uint32_t data_offset = 0x400u;
    constexpr std::uint32_t data_file_size = 0x80u;
    constexpr std::uint32_t data_memory_size = 0x100u;
    const auto code_size = static_cast<std::uint32_t>(words.size() * 4u);
    expect(code_offset + code_size <= data_offset,
           "direct-transfer code must not overlap data segment");

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
    expect(parsed.ok(), "direct-transfer ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "direct-transfer ELF must map");
    return std::move(*built.memory);
}
} // namespace

int main() {
    using namespace b3r::recompiler;
    constexpr std::uint32_t base = 0x00110000u;
    constexpr std::uint32_t target = base + 0x20u;
    const auto bgtz_boundary = i_type(0x07u, 0u, 0u, 0u);

    {
        auto memory = make_memory({
            j_type(0x02u, target),
            i_type(0x09u, 0u, 7u, 5u),
            i_type(0x09u, 0u, 8u, 9u),
            0u, 0u, 0u, 0u, 0u,
            bgtz_boundary, 0u,
        }, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[31] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
        const auto result = dispatcher.run(base, state, 2u);
        expect(result.reason == R5900DispatchStopReason::ControlFlow &&
                   result.next_pc == target,
               "J fixture must reach unsupported target BGTZ");
        expect(result.blocks_executed == 1u && result.instructions_executed == 2u,
               "J fixture must count J plus delay only");
        expect(state.gpr[7].low64 == 5u && state.gpr[8].low64 == 0u,
               "J must execute delay and skip poison fallthrough");
        expect(state.gpr[31].low64 == 0x1111222233334444ull &&
                   state.gpr[31].high64 == 0xaaaabbbbccccddddull,
               "J must preserve r31");
    }

    {
        auto memory = make_memory({
            j_type(0x03u, target),
            i_type(0x09u, 31u, 23u, 0u),
            i_type(0x09u, 0u, 25u, 1u),
            0u, 0u, 0u, 0u, 0u,
            bgtz_boundary, 0u,
        }, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[31] = {0xdeadbeefdeadbeefull, 0x0123456789abcdefull};
        const auto result = dispatcher.run(base, state, 2u);
        expect(result.reason == R5900DispatchStopReason::ControlFlow &&
                   result.next_pc == target,
               "JAL fixture must reach unsupported target BGTZ");
        expect(state.gpr[31].low64 == base + 8u &&
                   state.gpr[31].high64 == 0x0123456789abcdefull,
               "dispatcher JAL link/high64 mismatch");
        expect(state.gpr[23].low64 == base + 8u && state.gpr[25].low64 == 0u,
               "JAL delay must see link and poison must stay untouched");
    }

    {
        auto memory = make_memory({
            j_type(0x02u, target),
            i_type(0x09u, 0u, 7u, 1u),
            0u, 0u, 0u, 0u, 0u, 0u,
            bgtz_boundary, 0u,
        }, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState first_state{};
        const auto first = dispatcher.run(base, first_state, 1u);
        expect(first.cache_misses == 1u && first.cache_hits == 0u,
               "first direct-transfer run must miss cache");
        R5900IrExecutionState second_state{};
        const auto second = dispatcher.run(base, second_state, 1u);
        expect(second.cache_hits == 1u && second.cache_misses == 0u &&
                   second.recompilations == 0u,
               "unchanged direct transfer must hit cache");
        expect(memory.write_u32(base + 4u, i_type(0x09u, 0u, 7u, 2u)),
               "delay mutation must succeed");
        R5900IrExecutionState third_state{};
        const auto third = dispatcher.run(base, third_state, 1u);
        expect(third.recompilations == 1u && third.cache_hits == 0u &&
                   third_state.gpr[7].low64 == 2u,
               "delay mutation must invalidate and recompile cache entry");
    }

    for (const auto op : {std::uint8_t{0x02u}, std::uint8_t{0x03u}}) {
        constexpr std::uint32_t data_base = 0x00220000u;
        constexpr std::uint32_t store_target = data_base + 0x20u;
        auto memory = make_memory({
            j_type(op, base + 0x10u),
            i_type(0x1fu, 2u, 7u, 0u),
            0u, 0u, bgtz_boundary, 0u,
        }, base, data_base);
        const auto before = memory.read_u128(store_target);
        expect(before.has_value(), "SQ delay target must be mapped");
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[2].low64 = store_target;
        state.gpr[7] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::LoweringFailure &&
                   result.next_pc == base + 4u && result.blocks_executed == 0u &&
                   result.instructions_executed == 0u,
               "SQ in J/JAL delay must fail before guest progress");
        expect(dispatcher.cache_size() == 0u,
               "rejected direct-transfer delay must not populate cache");
        const auto after = memory.read_u128(store_target);
        expect(after.has_value() && *after == *before,
               "rejected SQ delay must not mutate guest memory");
    }

    std::cout << "r5900_block_dispatcher_direct_transfer_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}