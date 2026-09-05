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
    const auto jr7 = r_type(7u, 0u, 0u, 0u, 0x08u);
    const auto jalr7 = r_type(7u, 0u, 7u, 0u, 0x09u);
    constexpr std::uint32_t syscall = 0x0cu;

    for (const auto op : {jr7, jalr7}) {
        auto memory = make_memory({op, i_type(0x09u, 7u, 8u, 1u),
                                   0u, 0u, 0u, 0u, 0u, 0u, syscall}, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[7] = {0xabcdef0000000000ull | target, 0x12345678u};
        const auto result = dispatcher.run(base, state, 3u);
        expect(result.reason == R5900DispatchStopReason::Trap && result.next_pc == target,
               "indirect transfer must reach target trap");
        expect(result.blocks_executed == 1u && result.instructions_executed == 2u,
               "indirect transfer and one delay must count");
        expect(state.gpr[8].low64 == (op == jr7 ? target + 1u : base + 9u),
               "JALR aliased link must be visible in delay, JR must preserve source");
        expect(state.gpr[7].high64 == 0x12345678u, "link must preserve upper half");
    }

    {
        auto memory = make_memory({jr7, i_type(0x09u, 0u, 7u, 9u), syscall,
                                   0u, 0u, 0u, 0u, 0u, syscall}, base);
        R5900BlockDispatcher dispatcher(memory);
        for (auto next : {target, base + 8u}) {
            R5900IrExecutionState state{};
            state.gpr[7].low64 = next;
            const auto result = dispatcher.run(base, state, 3u);
            expect(result.reason == R5900DispatchStopReason::Trap && result.next_pc == next,
                   "cache reuse must read fresh runtime target before delay changes it");
            expect(state.gpr[7].low64 == 9u && result.instructions_executed == 2u,
                   "cached indirect delay must run exactly once");
            expect(next == target ? result.cache_misses == 1u : result.cache_hits == 1u,
                   "target change must reuse native code without recompile");
        }
        expect(memory.write_u32(base, r_type(9u, 0u, 0u, 0u, 0x08u)),
               "source mutation must be writable");
        R5900IrExecutionState state{};
        state.gpr[9].low64 = target;
        const auto changed = dispatcher.run(base, state, 1u);
        expect(changed.recompilations == 1u && changed.next_pc == target,
               "changed JR source encoding must invalidate cached block");
        expect(memory.write_u32(base + 4u, i_type(0x09u, 0u, 7u, 10u)),
               "delay mutation must be writable");
        const auto delay_changed = dispatcher.run(base, state, 1u);
        expect(delay_changed.recompilations == 1u && state.gpr[7].low64 == 10u,
               "changed indirect delay word must invalidate cached block");
    }

    for (auto next : {base + 1u, 0xdead0000u, 0x00220000u}) {
        auto memory = make_memory({jr7, i_type(0x09u, 0u, 8u, 99u)}, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[7].low64 = next;
        const auto result = dispatcher.run(base, state, 2u);
        expect(result.reason == R5900DispatchStopReason::AnalysisFailure && result.next_pc == next,
               "unaligned/unmapped/non-executable target must fail guest fetch");
        expect(result.blocks_executed == 1u && result.instructions_executed == 2u &&
                   state.gpr[8].low64 == 99u,
               "failed target fetch must preserve completed jump and delay");
    }

    {
        auto memory = make_memory({jr7, i_type(0x09u, 8u, 8u, 1u)}, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[7].low64 = base;
        const auto result = dispatcher.run(base, state, 4u);
        expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted &&
                   result.blocks_executed == 4u && result.instructions_executed == 8u &&
                   result.cache_hits == 3u && state.gpr[8].low64 == 4u,
               "indirect self-loop must respect budget and execute delay once per iteration");
    }

    for (const auto op : {jr7, jalr7}) {
        for (auto delay : {i_type(0x1fu, 2u, 8u, 0u), jr7, syscall}) {
            auto memory = make_memory({i_type(0x09u, 0u, 6u, 1u), op, delay}, base);
            R5900BlockDispatcher dispatcher(memory);
            R5900IrExecutionState state{};
            state.gpr[7].low64 = target;
            const auto result = dispatcher.run(base, state, 1u);
            expect(result.reason == R5900DispatchStopReason::LoweringFailure &&
                       result.next_pc == base + 8u && result.instructions_executed == 0u &&
                       dispatcher.cache_size() == 0u && state.gpr[6].low64 == 0u &&
                       state.gpr[7].low64 == target,
                   "unsupported indirect delay must reject whole block before body/link effects");
        }
        auto memory = make_memory({op}, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        expect(dispatcher.run(base, state, 1u).reason == R5900DispatchStopReason::AnalysisFailure,
               "unmapped delay must fail before execution");
    }

    for (const auto op : {jr7, jalr7, j_type(0x03u, target), i_type(0x04u, 0u, 0u, 0u)}) {
        auto memory = make_memory({i_type(0x09u, 0u, 6u, 1u),
                                   i_type(0x0eu, 0u, 5u, 9u), // unsupported XORI
                                   op, i_type(0x09u, 0u, 8u, 99u)}, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[7].low64 = target;
        const auto result = dispatcher.run(base, state, 2u);
        expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction &&
                   result.next_pc == base + 4u && result.instructions_executed == 1u &&
                   state.gpr[6].low64 == 1u && state.gpr[8].low64 == 0u &&
                   state.gpr[31].low64 == 0u,
               "unsupported body must stop selection before later transfer and delay");
    }

    {
        auto memory = make_memory({i_type(0x1fu, 2u, 8u, 0u), jalr7,
                                   i_type(0x09u, 7u, 9u, 0u)}, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[2].low64 = 0x00220020u;
        state.gpr[7].low64 = target;
        state.gpr[8] = {0x1234u, 0x5678u};
        const auto result = dispatcher.run(base, state, 1u);
        const auto stored = memory.read_u128(0x00220020u);
        expect(result.next_pc == target && result.instructions_executed == 3u &&
                   state.gpr[9].low64 == base + 12u && stored.has_value() &&
                   (*stored)[0] == 0x1234u && (*stored)[1] == 0x5678u,
               "successful body SQ helper must preserve indirect target and link semantics");
    }
    std::cout << "r5900_block_dispatcher_indirect_transfer_windows_tests: PASS\n";
}
