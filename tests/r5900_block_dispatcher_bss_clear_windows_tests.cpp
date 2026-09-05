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
    std::cerr << "r5900_block_dispatcher_bss_clear_windows_tests: FAIL: " << message << '\n';
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

void put_program_header(Bytes& bytes,
                        std::size_t offset,
                        std::uint32_t file_offset,
                        std::uint32_t guest_address,
                        std::uint32_t file_size,
                        std::uint32_t memory_size,
                        std::uint32_t flags) {
    put_u32(bytes, offset + 0u, 1u);
    put_u32(bytes, offset + 4u, file_offset);
    put_u32(bytes, offset + 8u, guest_address);
    put_u32(bytes, offset + 12u, guest_address);
    put_u32(bytes, offset + 16u, file_size);
    put_u32(bytes, offset + 20u, memory_size);
    put_u32(bytes, offset + 24u, flags);
    put_u32(bytes, offset + 28u, 0x1000u);
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

b3r::runtime::Ps2MemoryMap make_memory(std::uint32_t code_base,
                                       std::uint32_t data_base) {
    constexpr std::uint32_t kProgramHeaderOffset = 52u;
    constexpr std::uint32_t kProgramHeaderSize = 32u;
    constexpr std::uint32_t kCodePayloadOffset = 0x100u;
    constexpr std::uint32_t kDataPayloadOffset = 0x200u;
    constexpr std::uint32_t kDataSize = 0x80u;

    const std::vector<std::uint32_t> words = {
        i_type(0x04u, 2u, 3u, 7u),       // BEQ r2,r3,exit
        0u,                               // delay
        i_type(0x1fu, 2u, 0u, 0u),       // SQ r0,0(r2)
        i_type(0x09u, 2u, 2u, 0x10u),    // ADDIU r2,r2,16
        j_type(0x02u, code_base),         // J loop
        0u,                               // delay
        0u,
        0u,
        0x0000000cu,                      // SYSCALL
    };
    const auto code_size = static_cast<std::uint32_t>(words.size() * 4u);

    Bytes bytes(static_cast<std::size_t>(kDataPayloadOffset + kDataSize), 0u);
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
    put_u32(bytes, 28u, kProgramHeaderOffset);
    put_u16(bytes, 40u, 52u);
    put_u16(bytes, 42u, kProgramHeaderSize);
    put_u16(bytes, 44u, 2u);

    put_program_header(bytes, kProgramHeaderOffset, kCodePayloadOffset,
                       code_base, code_size, code_size, 5u);
    put_program_header(bytes, kProgramHeaderOffset + kProgramHeaderSize,
                       kDataPayloadOffset, data_base, kDataSize, kDataSize, 6u);

    for (std::size_t index = 0; index < words.size(); ++index) {
        put_u32(bytes, static_cast<std::size_t>(kCodePayloadOffset) + index * 4u,
                words[index]);
    }
    for (std::size_t index = 0; index < kDataSize; ++index) {
        bytes[static_cast<std::size_t>(kDataPayloadOffset) + index] = 0xa5u;
    }

    const auto parsed = b3r::recompiler::parse_ps2_elf(bytes);
    expect(parsed.ok(), "synthetic BSS-clear ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "synthetic BSS-clear ELF must map");
    return std::move(*built.memory);
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    constexpr std::uint32_t kCodeBase = 0x00100000u;
    constexpr std::uint32_t kDataBase = 0x00200000u;
    constexpr std::uint32_t kClearBegin = kDataBase + 0x10u;
    constexpr std::uint32_t kClearEnd = kDataBase + 0x50u;
    constexpr std::uint32_t kSyscallPc = kCodeBase + 0x20u;

    auto memory = make_memory(kCodeBase, kDataBase);
    R5900BlockDispatcher dispatcher(memory);
    R5900IrExecutionState state{};
    state.gpr[2].low64 = kClearBegin;
    state.gpr[3].low64 = kClearEnd;

    const auto result = dispatcher.run(kCodeBase, state, 16u);

    expect(result.reason == R5900DispatchStopReason::Trap,
           "BSS-clear loop must stop at the syscall boundary");
    expect(result.next_pc == kSyscallPc,
           "BSS-clear loop must reach the exact syscall PC");
    expect(result.blocks_executed == 9u && result.instructions_executed == 26u,
           "four-quadword loop must preserve selected-word accounting");
    expect(result.cache_misses == 2u && result.cache_hits == 7u &&
               result.recompilations == 0u,
           "BSS-clear loop must compile two blocks and reuse them seven times");
    expect(result.fast_cache_hits == 7u,
           "all repeated loop transfers must bypass analyzer/lowering through fast cache replay");
    expect(dispatcher.cache_size() == 2u,
           "BSS-clear loop must retain exactly two native cache entries");
    expect(state.gpr[2].low64 == kClearEnd,
           "BSS-clear pointer must finish exactly at end address");

    expect(memory.read_u8(kClearBegin - 1u) == 0xa5u,
           "BSS-clear must preserve byte immediately before range");
    for (std::uint32_t address = kClearBegin; address < kClearEnd; ++address) {
        expect(memory.read_u8(address) == 0u,
               "BSS-clear must zero every byte in selected range");
    }
    expect(memory.read_u8(kClearEnd) == 0xa5u,
           "BSS-clear must preserve byte immediately after range");

    std::cout << "r5900_block_dispatcher_bss_clear_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
