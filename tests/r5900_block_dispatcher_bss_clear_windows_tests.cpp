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

b3r::runtime::Ps2MemoryMap make_memory(std::uint32_t code_base,
                                       std::uint32_t data_base) {
    constexpr std::uint32_t kProgramHeaderOffset = 52u;
    constexpr std::uint32_t kProgramHeaderSize = 32u;
    constexpr std::uint32_t kCodePayloadOffset = 0x100u;
    constexpr std::uint32_t kDataPayloadOffset = 0x200u;
    constexpr std::uint32_t kDataSize = 0x80u;

    const std::vector<std::uint32_t> words = {
        i_type(0x04u, 2u, 3u, 5u),       // BEQ r2,r3,post_loop
        0u,                               // delay
        i_type(0x1fu, 2u, 0u, 0u),       // SQ r0,0(r2)
        i_type(0x09u, 2u, 2u, 0x10u),    // ADDIU r2,r2,16
        j_type(0x02u, code_base),         // J loop
        0u,                               // delay
        r_type(4u, 5u, 9u, 0u, 0x25u),   // OR r9,r4,r5
        i_type(0x09u, 0u, 3u, 0x003cu),  // ADDIU v1,r0,0x3c (SetupThread)
        0x0000000cu,                      // SYSCALL
        i_type(0x0eu, 1u, 1u, 1u),       // XORI: deliberate unsupported boundary
        0x0000000cu,                      // analyzer guard; must never be handled
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
    constexpr std::uint32_t kDataSize = 0x80u;
    constexpr std::uint32_t kClearBegin = kDataBase + 0x10u;
    constexpr std::uint32_t kClearEnd = kDataBase + 0x50u;
    constexpr std::uint32_t kSyscallPc = kCodeBase + 0x20u;
    constexpr std::uint32_t kPostSyscallPc = kSyscallPc + 4u;

    auto memory = make_memory(kCodeBase, kDataBase);
    R5900BlockDispatcher dispatcher(memory);
    R5900IrExecutionState state{};
    state.gpr[2].low64 = kClearBegin;
    state.gpr[3].low64 = kClearEnd;
    state.gpr[4] = {0x004e8670u, 0x1111111111111111ull};
    state.gpr[5] = {0x01ff0000u, 0x2222222222222222ull};
    state.gpr[6] = {0x00010000u, 0x3333333333333333ull};
    state.gpr[7] = {0x01d9ce80u, 0x4444444444444444ull};
    state.gpr[8] = {0x00100220u, 0x5555555555555555ull};
    state.gpr[9] = {0u, 0xaaaaaaaaaaaaaaaaull};

    const auto result = dispatcher.run(kCodeBase, state, 16u);

    expect(result.reason == R5900DispatchStopReason::Trap,
           "BSS-clear loop plus SetupThread setup must stop at the syscall boundary");
    expect(result.next_pc == kSyscallPc,
           "BSS-clear loop plus SetupThread setup must reach the exact syscall PC");
    expect(result.blocks_executed == 10u && result.instructions_executed == 28u,
           "post-loop setup block must preserve historical native accounting");
    expect(result.syscalls_handled == 0u,
           "null host service must not count the trapped syscall as handled");
    expect(result.cache_misses == 3u && result.cache_hits == 7u &&
               result.recompilations == 0u,
           "post-loop setup must preserve cache miss/hit/recompile accounting");
    expect(result.fast_cache_hits == 7u,
           "all repeated loop transfers must use fast cache replay");
    expect(dispatcher.cache_size() == 3u,
           "BSS-clear plus SetupThread setup must retain three native cache entries");
    expect(state.gpr[2].low64 == kClearEnd,
           "BSS pointer must finish at end before trapped SetupThread");
    expect(state.gpr[3].low64 == 0x3cu,
           "post-loop setup must select SetupThread");
    expect(state.gpr[9].low64 == 0x01ff8670u &&
               state.gpr[9].high64 == 0xaaaaaaaaaaaaaaaaull,
           "post-loop OR must commit without corrupting SetupThread ABI registers");

    expect(memory.read_u8(kClearBegin - 1u) == 0xa5u,
           "BSS-clear must preserve byte immediately before range");
    for (std::uint32_t address = kClearBegin; address < kClearEnd; ++address) {
        expect(memory.read_u8(address) == 0u,
               "BSS-clear must zero every byte in selected range");
    }
    expect(memory.read_u8(kClearEnd) == 0xa5u,
           "BSS-clear must preserve byte immediately after range");

    auto handled_memory = make_memory(kCodeBase, kDataBase);
    R5900HostSyscallService host{};
    R5900BlockDispatcherOptions options{};
    options.host_syscalls = &host;
    R5900BlockDispatcher handled_dispatcher(handled_memory, options);

    R5900IrExecutionState handled_state{};
    handled_state.gpr[2].low64 = kClearBegin;
    handled_state.gpr[3].low64 = kClearEnd;
    handled_state.gpr[4] = {0x004e8670u, 0x1111111111111111ull};
    handled_state.gpr[5] = {0x01ff0000u, 0x2222222222222222ull};
    handled_state.gpr[6] = {0x00010000u, 0x3333333333333333ull};
    handled_state.gpr[7] = {0x01d9ce80u, 0x4444444444444444ull};
    handled_state.gpr[8] = {0x00100220u, 0x5555555555555555ull};
    handled_state.gpr[9] = {0u, 0xaaaaaaaaaaaaaaaaull};

    const auto handled = handled_dispatcher.run(kCodeBase, handled_state, 16u);

    expect(handled.reason == R5900DispatchStopReason::UnsupportedInstruction,
           "production SetupThread must continue to deliberate XORI boundary");
    expect(handled.next_pc == kPostSyscallPc,
           "production SetupThread continuation must land at PC+4");
    expect(handled.blocks_executed == 10u && handled.instructions_executed == 28u,
           "SetupThread host handling must not change native BSS/setup accounting");
    expect(handled.syscalls_handled == 1u,
           "exactly one production SetupThread must be handled");
    expect(handled.cache_misses == 3u && handled.cache_hits == 7u &&
               handled.fast_cache_hits == 7u && handled.recompilations == 0u,
           "SetupThread host handling must preserve native cache accounting");
    expect(handled_state.gpr[2].low64 == 0x02000000u,
           "SetupThread must replace prior BSS pointer in v0 with stack top");
    expect(handled_state.gpr[3].low64 == 0x3cu,
           "SetupThread selector setup must commit before host handling");
    expect(handled_state.gpr[9].low64 == 0x01ff8670u &&
               handled_state.gpr[9].high64 == 0xaaaaaaaaaaaaaaaaull,
           "post-loop OR state must commit before host handling");
    expect(host.setup_thread_context().has_value(),
           "production SetupThread must record a context");
    expect(host.setup_thread_context()->gp == 0x004e8670u &&
               host.setup_thread_context()->stack_base == 0x01ff0000u &&
               host.setup_thread_context()->stack_size == 0x00010000u &&
               host.setup_thread_context()->stack_top == 0x02000000u &&
               host.setup_thread_context()->args == 0x01d9ce80u &&
               host.setup_thread_context()->root_func == 0x00100220u,
           "startup-shaped SetupThread context mismatch");
    expect(handled_dispatcher.cache_size() == 3u,
           "handled syscall must not create or compile a syscall cache entry");

    for (std::uint32_t offset = 0u; offset < kDataSize; ++offset) {
        const auto address = kDataBase + offset;
        const auto trapped_byte = memory.read_u8(address);
        const auto handled_byte = handled_memory.read_u8(address);
        expect(trapped_byte.has_value() && handled_byte.has_value(),
               "startup-shaped data region must stay mapped in both runs");
        expect(*handled_byte == *trapped_byte,
               "SetupThread HLE must not change guest memory beyond native BSS effects");
    }

    std::cout << "r5900_block_dispatcher_bss_clear_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
