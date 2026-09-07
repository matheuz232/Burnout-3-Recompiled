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
    std::cerr << "r5900_block_dispatcher_syscall_windows_tests: FAIL: " << message << '\n';
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

constexpr std::uint32_t i_type(
    std::uint8_t op,
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
    std::uint32_t base) {
    constexpr std::uint32_t kProgramHeaderOffset = 52u;
    constexpr std::uint32_t kPayloadOffset = 0x100u;
    const auto payload_size = static_cast<std::uint32_t>(words.size() * 4u);
    Bytes bytes(static_cast<std::size_t>(kPayloadOffset + payload_size + 0x40u), 0u);

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
    put_u32(bytes, 24u, base);
    put_u32(bytes, 28u, kProgramHeaderOffset);
    put_u16(bytes, 40u, 52u);
    put_u16(bytes, 42u, 32u);
    put_u16(bytes, 44u, 1u);

    put_u32(bytes, kProgramHeaderOffset + 0u, 1u);
    put_u32(bytes, kProgramHeaderOffset + 4u, kPayloadOffset);
    put_u32(bytes, kProgramHeaderOffset + 8u, base);
    put_u32(bytes, kProgramHeaderOffset + 12u, base);
    put_u32(bytes, kProgramHeaderOffset + 16u, payload_size);
    put_u32(bytes, kProgramHeaderOffset + 20u, payload_size);
    put_u32(bytes, kProgramHeaderOffset + 24u, 5u);
    put_u32(bytes, kProgramHeaderOffset + 28u, 0x1000u);

    for (std::size_t index = 0; index < words.size(); ++index) {
        put_u32(bytes,
                static_cast<std::size_t>(kPayloadOffset) + index * 4u,
                words[index]);
    }

    const auto parsed = b3r::recompiler::parse_ps2_elf(bytes);
    expect(parsed.ok(), "synthetic syscall ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "synthetic syscall ELF must map");
    return std::move(*built.memory);
}

class FakeHostSyscallService final : public b3r::recompiler::IR5900HostSyscallService {
public:
    explicit FakeHostSyscallService(b3r::recompiler::R5900HostSyscallStatus status)
        : status_(status) {}

    b3r::recompiler::R5900HostSyscallResult handle(
        const b3r::recompiler::R5900HostSyscallRequest& request,
        b3r::recompiler::R5900IrExecutionState& state,
        b3r::runtime::Ps2MemoryMap&) override {
        ++calls;
        last_request = request;
        if (status_ == b3r::recompiler::R5900HostSyscallStatus::Handled) {
            state.gpr[2].low64 = 0x12345678u;
            return {status_, "handled by fake"};
        }
        return {status_, status_ == b3r::recompiler::R5900HostSyscallStatus::Unsupported
                            ? "unsupported by fake"
                            : "fault from fake"};
    }

    std::size_t calls{};
    b3r::recompiler::R5900HostSyscallRequest last_request{};

private:
    b3r::recompiler::R5900HostSyscallStatus status_;
};

} // namespace

int main() {
    using namespace b3r::recompiler;

    constexpr std::uint32_t base = 0x00100000u;
    constexpr std::uint32_t kSyscall = 0x0000000cu;
    constexpr std::uint32_t kUnsupportedXori =
        (0x0eu << 26u) | (1u << 21u) | (1u << 16u) | 1u;
    constexpr std::uint32_t kUnsupportedLd =
        (0x37u << 26u) | (29u << 21u) | (31u << 16u);
    expect(decode_r5900(kUnsupportedLd).instruction == R5900Instruction::Ld,
           "unsupported-boundary fixture must encode LD");

    {
        auto memory = make_memory({kSyscall}, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::Trap,
               "null syscall service must preserve Trap");
        expect(result.next_pc == base && result.syscalls_handled == 0u,
               "null service must not advance or count syscall");
    }

    {
        auto memory = make_memory({kSyscall, kUnsupportedXori}, base);
        FakeHostSyscallService service(R5900HostSyscallStatus::Handled);
        R5900BlockDispatcherOptions options{};
        options.block_options.max_instructions = 1u;
        options.host_syscalls = &service;
        R5900BlockDispatcher dispatcher(memory, options);
        R5900IrExecutionState state{};
        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction,
               "handled syscall must resume to next guest instruction");
        expect(result.next_pc == base + 4u,
               "handled syscall must resume at PC+4");
        expect(result.blocks_executed == 0u && result.instructions_executed == 0u,
               "host syscall must not count as native block/instruction");
        expect(result.syscalls_handled == 1u && service.calls == 1u,
               "handled syscall must increment only host counter");
        expect(service.last_request.guest_pc == base &&
                   service.last_request.raw_instruction == kSyscall,
               "dispatcher must forward exact syscall provenance");
        expect(state.gpr[2].low64 == 0x12345678u,
               "host state mutation must survive resume");
    }

    {
        constexpr std::uint32_t kPostSetupPc = base + 8u;
        auto memory = make_memory(
            {i_type(0x09u, 0u, 9u, 0x0055u), kSyscall, kUnsupportedXori},
            base);

        R5900HostSyscallService service{};
        R5900BlockDispatcherOptions options{};
        options.block_options.max_instructions = 1u;
        options.host_syscalls = &service;
        R5900BlockDispatcher dispatcher(memory, options);

        R5900IrExecutionState state{};
        state.gpr[2].high64 = 0xa5a5a5a5a5a5a5a5ull;
        state.gpr[3].low64 = 0x3cu;
        state.gpr[4].low64 = 0x004e8670u;
        state.gpr[5].low64 = 0x01ff0000u;
        state.gpr[6].low64 = 0x00010000u;
        state.gpr[7].low64 = 0x01d9ce80u;
        state.gpr[8].low64 = 0x00100220u;

        const auto result = dispatcher.run(base, state, 2u);
        expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction,
               "production SetupThread must resume to deliberate unsupported instruction");
        expect(result.next_pc == kPostSetupPc,
               "production SetupThread must resume at syscall PC+4");
        expect(result.blocks_executed == 1u && result.instructions_executed == 1u,
               "only native prefix must count before production SetupThread");
        expect(result.syscalls_handled == 1u,
               "production SetupThread must increment host counter exactly once");
        expect(state.gpr[9].low64 == 0x55u,
               "native prefix must commit before SetupThread host handling");
        expect(state.gpr[2].low64 == 0x02000000u &&
                   state.gpr[2].high64 == 0xa5a5a5a5a5a5a5a5ull,
               "SetupThread v0 result must survive dispatcher resume");
        expect(service.setup_thread_context().has_value() &&
                   service.setup_thread_context()->stack_top == 0x02000000u,
               "dispatcher path must commit production SetupThread context");
    }

    {
        constexpr std::uint32_t kPostSetupHeapPc = base + 32u;
        auto memory = make_memory(
            {
                i_type(0x09u, 0u, 9u, 0x0055u),     // native prefix
                kSyscall,                            // SetupThread
                i_type(0x09u, 2u, 29u, 0x0000u),   // guest move sp,v0
                i_type(0x0fu, 0u, 4u, 0x01ecu),    // LUI a0,0x01ec
                i_type(0x0du, 4u, 4u, 0xea00u),    // ORI a0,a0,0xea00
                i_type(0x09u, 0u, 5u, 0xffffu),    // ADDIU a1,r0,-1
                i_type(0x09u, 0u, 3u, 0x003du),    // ADDIU v1,r0,0x3d
                kSyscall,                            // SetupHeap
                kUnsupportedXori,                    // deliberate boundary
                kSyscall,                            // analyzer guard; must never be handled
            },
            base);

        R5900HostSyscallService service{};
        R5900BlockDispatcherOptions options{};
        options.block_options.max_instructions = 16u;
        options.host_syscalls = &service;
        R5900BlockDispatcher dispatcher(memory, options);

        R5900IrExecutionState state{};
        state.gpr[2].high64 = 0xa5a5a5a5a5a5a5a5ull;
        state.gpr[29].high64 = 0x2929292929292929ull;
        state.gpr[3].low64 = 0x3cu;
        state.gpr[4].low64 = 0x004e8670u;
        state.gpr[5].low64 = 0x01ff0000u;
        state.gpr[6].low64 = 0x00010000u;
        state.gpr[7].low64 = 0x01d9ce80u;
        state.gpr[8].low64 = 0x00100220u;

        const auto result = dispatcher.run(base, state, 3u);
        expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction,
               "SetupThread/SetupHeap path must reach deliberate boundary");
        expect(result.next_pc == kPostSetupHeapPc,
               "SetupHeap must resume at its syscall PC+4");
        expect(result.blocks_executed == 2u && result.instructions_executed == 6u,
               "two-syscall path must count only the two native prefixes");
        expect(result.syscalls_handled == 2u,
               "SetupThread/SetupHeap path must handle exactly two syscalls");
        expect(dispatcher.cache_size() == 2u,
               "neither syscall may create a native cache entry");
        expect(state.gpr[9].low64 == 0x55u,
               "native prefix must commit before first syscall");
        expect(state.gpr[29].low64 == 0x02000000u &&
                   state.gpr[29].high64 == 0x2929292929292929ull,
               "guest instruction must copy SetupThread v0 into sp");
        expect(state.gpr[2].low64 == 0x02000000u &&
                   state.gpr[2].high64 == 0xa5a5a5a5a5a5a5a5ull,
               "SetupHeap must preserve SetupThread v0 result");
        expect(state.gpr[3].low64 == 0x3du &&
                   state.gpr[4].low64 == 0x01ecea00u &&
                   static_cast<std::uint32_t>(state.gpr[5].low64) == 0xffffffffu,
               "guest SetupHeap argument preparation must commit before syscall");
        expect(service.setup_thread_context().has_value() &&
                   service.setup_thread_context()->stack_base == 0x01ff0000u &&
                   service.setup_thread_context()->stack_top == 0x02000000u,
               "two-syscall dispatcher path must retain SetupThread context");
        expect(service.setup_heap_context().has_value() &&
                   service.setup_heap_context()->heap_start == 0x01ecea00u &&
                   service.setup_heap_context()->requested_heap_size == 0xffffffffu &&
                   service.setup_heap_context()->heap_end == 0x01ff0000u,
               "two-syscall dispatcher path must record Burnout SetupHeap context");
    }

    {
        const auto jal = j_type(0x03u, base + 0x20u);
        auto memory = make_memory(
            {
                i_type(0x09u, 29u, 29u, 0xfff0u), // ADDIU sp,sp,-16
                kUnsupportedLd,                    // LD ra,0(sp)
                jal,                               // must remain unexecuted
                0u,                                // JAL delay slot
            },
            base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[29] = {0x02000000u, 0x2929292929292929ull};
        state.gpr[31] = {0x001001f0u, 0x3131313131313131ull};

        const auto result = dispatcher.run(base, state, 2u);
        expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction,
               "unsupported LD before JAL must stop as unsupported instruction");
        expect(result.next_pc == base + 4u,
               "unsupported LD before JAL must report exact first unexecuted PC");
        expect(result.blocks_executed == 1u && result.instructions_executed == 1u,
               "dispatcher must execute only supported prefix before LD boundary");
        expect(state.gpr[29].low64 == 0x01fffff0u &&
                   state.gpr[29].high64 == 0x2929292929292929ull,
               "supported ADDIU prefix must commit before LD boundary");
        expect(state.gpr[31].low64 == 0x001001f0u &&
                   state.gpr[31].high64 == 0x3131313131313131ull,
               "JAL after unsupported LD must not mutate return address");
    }

    {
        const auto jal = j_type(0x03u, base + 0x20u);
        auto memory = make_memory(
            {
                kUnsupportedLd, // boundary at entry
                jal,            // must remain unexecuted
                0u,             // JAL delay slot
            },
            base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        state.gpr[31] = {0x001001f0u, 0x3131313131313131ull};

        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction,
               "unsupported LD at entry must stop as unsupported instruction");
        expect(result.next_pc == base,
               "unsupported LD at entry must retain exact boundary PC");
        expect(result.blocks_executed == 0u && result.instructions_executed == 0u,
               "unsupported LD at entry must execute no later transfer");
        expect(state.gpr[31].low64 == 0x001001f0u &&
                   state.gpr[31].high64 == 0x3131313131313131ull,
               "JAL after entry LD boundary must not mutate return address");
    }

    {
        auto memory = make_memory({kSyscall}, base);
        FakeHostSyscallService service(R5900HostSyscallStatus::Unsupported);
        R5900BlockDispatcherOptions options{};
        options.host_syscalls = &service;
        R5900BlockDispatcher dispatcher(memory, options);
        R5900IrExecutionState state{};
        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::UnsupportedSyscall,
               "unsupported host syscall must have dedicated stop reason");
        expect(result.next_pc == base && result.syscalls_handled == 0u,
               "unsupported host syscall must not advance or count handled");
        expect(result.message.find("unsupported by fake") != std::string::npos,
               "dispatcher must preserve unsupported diagnostic");
    }

    {
        auto memory = make_memory({kSyscall}, base);
        FakeHostSyscallService service(R5900HostSyscallStatus::Fault);
        R5900BlockDispatcherOptions options{};
        options.host_syscalls = &service;
        R5900BlockDispatcher dispatcher(memory, options);
        R5900IrExecutionState state{};
        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::HostSyscallFailure,
               "faulting host syscall must have dedicated stop reason");
        expect(result.next_pc == base && result.syscalls_handled == 0u,
               "faulting host syscall must not advance or count handled");
        expect(result.message.find("fault from fake") != std::string::npos,
               "dispatcher must preserve fault diagnostic");
    }

    std::cout << "r5900_block_dispatcher_syscall_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
