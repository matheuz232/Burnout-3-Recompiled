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
    std::cerr << "r5900_block_dispatcher_store128_windows_tests: FAIL: "
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
    std::uint32_t code_base,
    std::uint32_t data_base) {
    constexpr std::uint32_t phoff = 52u;
    constexpr std::uint32_t code_offset = 0x100u;
    constexpr std::uint32_t data_offset = 0x300u;
    constexpr std::uint32_t data_file_size = 0x80u;
    constexpr std::uint32_t data_memory_size = 0x100u;
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
    put_u16(bytes, 44u, 2u);

    put_u32(bytes, phoff + 0u, 1u);
    put_u32(bytes, phoff + 4u, code_offset);
    put_u32(bytes, phoff + 8u, code_base);
    put_u32(bytes, phoff + 12u, code_base);
    put_u32(bytes, phoff + 16u, code_size);
    put_u32(bytes, phoff + 20u, code_size);
    put_u32(bytes, phoff + 24u, 5u);
    put_u32(bytes, phoff + 28u, 0x1000u);

    const auto data_ph = phoff + 32u;
    put_u32(bytes, data_ph + 0u, 1u);
    put_u32(bytes, data_ph + 4u, data_offset);
    put_u32(bytes, data_ph + 8u, data_base);
    put_u32(bytes, data_ph + 12u, data_base);
    put_u32(bytes, data_ph + 16u, data_file_size);
    put_u32(bytes, data_ph + 20u, data_memory_size);
    put_u32(bytes, data_ph + 24u, 6u);
    put_u32(bytes, data_ph + 28u, 0x1000u);

    for (std::size_t i = 0u; i < words.size(); ++i) {
        put_u32(bytes, code_offset + i * 4u, words[i]);
    }
    for (std::size_t i = 0u; i < data_file_size; ++i) {
        bytes[data_offset + i] = 0xa5u;
    }

    auto parsed = b3r::recompiler::parse_ps2_elf(bytes);
    expect(parsed.ok(), "dispatcher Store128 ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "dispatcher Store128 memory must map");
    return std::move(*built.memory);
}

void expect_stored_value(const b3r::runtime::Ps2MemoryMap& memory,
                         std::uint32_t address,
                         std::uint64_t low64,
                         std::uint64_t high64) {
    const auto value = memory.read_u128(address);
    expect(value.has_value(), "stored 128-bit value must remain readable");
    expect((*value)[0] == low64 && (*value)[1] == high64,
           "dispatcher Store128 data mismatch");
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    constexpr std::uint32_t code_base = 0x00110000u;
    constexpr std::uint32_t data_base = 0x00220000u;
    constexpr std::uint32_t target = data_base + 0x20u;
    constexpr std::uint32_t sentinel = code_base + 4u;
    const auto sq_r7_r2 = i_type(0x1fu, 2u, 7u, 0u);
    const auto jump = j_type(0x02u, code_base + 0x40u);

    {
        auto memory = make_memory({sq_r7_r2, jump, 0u}, code_base, data_base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[2].low64 = target + 7u;
        state.gpr[7] = {0x0123456789abcdefull, 0xfedcba9876543210ull};

        const auto result = dispatcher.run(code_base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::ControlFlow,
               "successful SQ body must stop at following J boundary");
        expect(result.next_pc == sentinel,
               "successful SQ body boundary PC mismatch");
        expect(result.blocks_executed == 1u && result.instructions_executed == 1u,
               "successful SQ body must complete one block/instruction");
        expect(result.cache_misses == 1u && result.cache_hits == 0u,
               "first SQ execution must compile exactly one cache entry");
        expect_stored_value(memory,
                            target,
                            0x0123456789abcdefull,
                            0xfedcba9876543210ull);
    }

    {
        auto memory = make_memory({sq_r7_r2, jump, 0u}, code_base, data_base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[2].low64 = 0x00330007u;
        state.gpr[7] = {1u, 2u};

        const auto result = dispatcher.run(code_base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::MemoryAccessFailure,
               "unmapped SQ must report runtime memory failure");
        expect(result.next_pc == code_base,
               "unmapped SQ must report faulting guest PC");
        expect(result.blocks_executed == 0u && result.instructions_executed == 0u,
               "faulting entry SQ must complete no guest progress");
        expect(result.message.find("runtime-memory") != std::string::npos &&
                   result.message.find("0x00110000") != std::string::npos &&
                   result.message.find("0x00330000") != std::string::npos &&
                   result.message.find("16") != std::string::npos,
               "runtime memory diagnostic must include stage, PCs/address and width");
    }

    {
        const auto addiu_r5 = i_type(0x09u, 0u, 5u, 9u);
        auto memory = make_memory(
            {addiu_r5, sq_r7_r2, jump, 0u}, code_base, data_base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[2].low64 = 0x00330000u;
        state.gpr[7] = {3u, 4u};

        const auto result = dispatcher.run(code_base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::MemoryAccessFailure,
               "prefix SQ failure must report memory failure");
        expect(result.next_pc == code_base + 4u,
               "prefix SQ failure must report SQ PC");
        expect(result.blocks_executed == 0u && result.instructions_executed == 1u,
               "prefix before faulting SQ must remain counted");
        expect(state.gpr[5].low64 == 9u,
               "prefix CPU mutation must remain committed after SQ fault");
    }

    {
        auto memory = make_memory({sq_r7_r2, jump, 0u}, code_base, data_base);
        R5900BlockDispatcher dispatcher(memory);

        R5900IrExecutionState faulting{};
        faulting.gpr[2].low64 = 0x00330000u;
        faulting.gpr[7] = {5u, 6u};
        const auto first = dispatcher.run(code_base, faulting, 1u);
        expect(first.reason == R5900DispatchStopReason::MemoryAccessFailure,
               "cache fixture first execution must fault");
        expect(first.cache_misses == 1u && dispatcher.cache_size() == 1u,
               "runtime memory fault must retain successfully compiled cache entry");

        R5900IrExecutionState corrected{};
        corrected.gpr[2].low64 = target;
        corrected.gpr[7] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
        const auto second = dispatcher.run(code_base, corrected, 1u);
        expect(second.reason == R5900DispatchStopReason::ControlFlow,
               "corrected runtime address must let cached SQ execute");
        expect(second.cache_hits == 1u && second.recompilations == 0u,
               "runtime fault must not invalidate native code cache");
        expect_stored_value(memory,
                            target,
                            0x1111222233334444ull,
                            0xaaaabbbbccccddddull);
    }

    {
        const auto beq = i_type(0x04u, 1u, 1u, 2u);
        auto memory = make_memory({beq, sq_r7_r2, 0u, 0u}, code_base, data_base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 1u;
        state.gpr[2].low64 = target;
        state.gpr[7] = {7u, 8u};

        const auto result = dispatcher.run(code_base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::LoweringFailure,
               "SQ in BEQ delay slot must remain explicitly outside dispatcher v0");
        expect(result.blocks_executed == 0u && result.instructions_executed == 0u,
               "rejected memory delay slot must execute nothing");
        expect(result.message.find("delay") != std::string::npos &&
                   result.message.find("SQ") != std::string::npos,
               "delay-slot rejection diagnostic must identify SQ scope boundary");
    }

    std::cout << "r5900_block_dispatcher_store128_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
