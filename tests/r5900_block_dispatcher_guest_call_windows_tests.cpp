#include "recompiler/ps2_elf.h"
#include "recompiler/r5900_guest_call_service.h"
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
    std::cerr << "r5900_block_dispatcher_guest_call_windows_tests: FAIL: " << message << '\n';
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

b3r::runtime::Ps2MemoryMap make_memory(const std::vector<std::uint32_t>& words,
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
        put_u32(bytes, static_cast<std::size_t>(kPayloadOffset) + index * 4u, words[index]);
    }

    const auto parsed = b3r::recompiler::parse_ps2_elf(bytes);
    expect(parsed.ok(), "synthetic guest-call ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "synthetic guest-call ELF must map");
    return std::move(*built.memory);
}

class FakeGuestCallService final : public b3r::recompiler::IR5900GuestCallService {
public:
    b3r::recompiler::R5900GuestCallStatus status{
        b3r::recompiler::R5900GuestCallStatus::NotHandled};
    std::uint32_t expected_pc{};
    std::size_t calls{};

    b3r::recompiler::R5900GuestCallResult try_handle(
        const b3r::recompiler::R5900GuestCallRequest& request,
        b3r::recompiler::R5900IrExecutionState& state,
        b3r::runtime::Ps2MemoryMap&) override {
        ++calls;
        if (request.guest_pc != expected_pc) {
            return {b3r::recompiler::R5900GuestCallStatus::Fault, "unexpected guest PC"};
        }
        if (status == b3r::recompiler::R5900GuestCallStatus::Handled) {
            state.gpr[2].low64 = 0x12345678u;
            return {status, {}};
        }
        if (status == b3r::recompiler::R5900GuestCallStatus::Fault) {
            return {status, "guest-call fault"};
        }
        return {status, {}};
    }
};

} // namespace

int main() {
    using namespace b3r::recompiler;

    constexpr std::uint32_t base = 0x00100000u;
    constexpr std::uint32_t unsupported_xori =
        (0x0eu << 26u) | (1u << 21u) | (1u << 16u) | 1u;

    {
        auto memory = make_memory({unsupported_xori}, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};
        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction,
               "null guest service must preserve normal dispatch");
        expect(result.next_pc == base,
               "null guest service must retain unsupported boundary PC");
    }

    {
        auto memory = make_memory({unsupported_xori}, base);
        FakeGuestCallService service{};
        service.expected_pc = base;
        service.status = R5900GuestCallStatus::NotHandled;
        R5900BlockDispatcherOptions options{};
        options.guest_calls = &service;
        R5900BlockDispatcher dispatcher(memory, options);
        R5900IrExecutionState state{};
        const auto result = dispatcher.run(base, state, 1u);
        expect(service.calls == 1u,
               "guest-call service must be queried at current PC");
        expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction,
               "NotHandled must preserve normal dispatch");
    }

    {
        auto memory = make_memory({unsupported_xori, unsupported_xori}, base);
        FakeGuestCallService service{};
        service.expected_pc = base;
        service.status = R5900GuestCallStatus::Handled;
        R5900BlockDispatcherOptions options{};
        options.guest_calls = &service;
        R5900BlockDispatcher dispatcher(memory, options);
        R5900IrExecutionState state{};
        state.gpr[31].low64 = base + 4u;
        const auto result = dispatcher.run(base, state, 1u);
        expect(service.calls == 2u,
               "handled call must resume and query the return PC");
        expect(result.reason == R5900DispatchStopReason::GuestCallFailure,
               "fake mismatch at return PC must expose deterministic guest-call fault");
        expect(result.next_pc == base + 4u,
               "handled call must resume at low32(ra)");
        expect(result.guest_calls_handled == 1u,
               "handled guest-call counter must increment once");
        expect(result.blocks_executed == 0u && result.instructions_executed == 0u,
               "handled guest call must not consume native block/instruction");
        expect(dispatcher.cache_size() == 0u,
               "handled guest call must not create cache entry");
        expect(state.gpr[2].low64 == 0x12345678u,
               "guest service register mutation must survive");
    }

    {
        auto memory = make_memory({unsupported_xori}, base);
        FakeGuestCallService service{};
        service.expected_pc = base;
        service.status = R5900GuestCallStatus::Fault;
        R5900BlockDispatcherOptions options{};
        options.guest_calls = &service;
        R5900BlockDispatcher dispatcher(memory, options);
        R5900IrExecutionState state{};
        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::GuestCallFailure,
               "guest-call fault must map to GuestCallFailure");
        expect(result.next_pc == base && result.message == "guest-call fault",
               "guest-call fault must preserve exact PC/message");
    }

    std::cout << "r5900_block_dispatcher_guest_call_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
