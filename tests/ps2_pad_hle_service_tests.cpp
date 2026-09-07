#include "recompiler/ps2_elf.h"
#include "runtime/ps2_memory_map.h"
#include "runtime/ps2_pad_hle_service.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

[[noreturn]] void fail(const char* message) {
    std::cerr << "ps2_pad_hle_service_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void expect_contains(const std::string& text,
                     std::string_view needle,
                     const char* message) {
    if (text.find(needle) == std::string::npos) {
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

b3r::runtime::Ps2MemoryMap make_memory() {
    constexpr std::uint32_t kProgramHeaderOffset = 52u;
    constexpr std::uint32_t kPayloadOffset = 0x100u;
    constexpr std::uint32_t kBase = 0x00100000u;
    Bytes bytes(kPayloadOffset + 4u, 0u);

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
    put_u32(bytes, 24u, kBase);
    put_u32(bytes, 28u, kProgramHeaderOffset);
    put_u16(bytes, 40u, 52u);
    put_u16(bytes, 42u, 32u);
    put_u16(bytes, 44u, 1u);

    put_u32(bytes, kProgramHeaderOffset + 0u, 1u);
    put_u32(bytes, kProgramHeaderOffset + 4u, kPayloadOffset);
    put_u32(bytes, kProgramHeaderOffset + 8u, kBase);
    put_u32(bytes, kProgramHeaderOffset + 12u, kBase);
    put_u32(bytes, kProgramHeaderOffset + 16u, 4u);
    put_u32(bytes, kProgramHeaderOffset + 20u, 4u);
    put_u32(bytes, kProgramHeaderOffset + 24u, 5u);
    put_u32(bytes, kProgramHeaderOffset + 28u, 0x1000u);

    const auto parsed = b3r::recompiler::parse_ps2_elf(bytes);
    expect(parsed.ok(), "synthetic PAD HLE ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "synthetic PAD HLE ELF must map");
    return std::move(*built.memory);
}

constexpr b3r::runtime::Ps2PadHleBindings kBindings{
    .pad_init = 0x00110000u,
    .pad_port_open = 0x00110020u,
    .pad_get_state = 0x00110040u,
    .pad_read = 0x00110060u,
    .pad_port_close = 0x00110080u,
    .pad_end = 0x001100a0u,
};

b3r::recompiler::R5900GuestCallResult invoke(
    b3r::runtime::Ps2PadHleService& service,
    std::uint32_t pc,
    b3r::recompiler::R5900IrExecutionState& state,
    b3r::runtime::Ps2MemoryMap& memory) {
    return service.try_handle({pc}, state, memory);
}

void set_args(b3r::recompiler::R5900IrExecutionState& state,
              std::uint32_t a0,
              std::uint32_t a1 = 0u,
              std::uint32_t a2 = 0u) {
    state.gpr[4].low64 = a0;
    state.gpr[5].low64 = a1;
    state.gpr[6].low64 = a2;
}

} // namespace

int main() {
    using b3r::recompiler::R5900GuestCallStatus;
    using b3r::recompiler::R5900IrExecutionState;
    using b3r::runtime::Ps2PadHleService;

    constexpr std::uint32_t kValidPadArea = 0x00120000u;
    constexpr std::uint32_t kSecondPadArea = 0x00120400u;
    constexpr std::uint32_t kCrossingPadArea = 0x01ffff80u;

    {
        auto memory = make_memory();
        Ps2PadHleService service(kBindings);
        R5900IrExecutionState state{};
        state.gpr[2] = {0x11112222u, 0x3333444455556666ull};
        const auto result = invoke(service, 0x0011f000u, state, memory);
        expect(result.status == R5900GuestCallStatus::NotHandled,
               "unbound PC must not be intercepted");
        expect(state.gpr[2].low64 == 0x11112222u &&
                   state.gpr[2].high64 == 0x3333444455556666ull,
               "unbound PC must not mutate v0");

        const auto zero_pc = invoke(service, 0u, state, memory);
        expect(zero_pc.status == R5900GuestCallStatus::NotHandled,
               "guest PC zero must not be intercepted");

        Ps2PadHleService zero_service({});
        const auto zero_binding = invoke(zero_service, kBindings.pad_init, state, memory);
        expect(zero_binding.status == R5900GuestCallStatus::NotHandled,
               "zero-valued bindings must remain unbound");
    }

    {
        auto memory = make_memory();
        Ps2PadHleService service(kBindings);
        R5900IrExecutionState state{};
        state.gpr[2].high64 = 0xa5a5a5a5a5a5a5a5ull;
        set_args(state, 0u);
        const auto result = invoke(service, kBindings.pad_init, state, memory);
        expect(result.status == R5900GuestCallStatus::Handled && service.initialized(),
               "padInit(0) must initialize successfully");
        expect(state.gpr[2].low64 == 1u &&
                   state.gpr[2].high64 == 0xa5a5a5a5a5a5a5a5ull,
               "padInit success must return 1 while preserving v0 high64");
    }

    {
        auto memory = make_memory();
        Ps2PadHleService service(kBindings);
        R5900IrExecutionState state{};
        set_args(state, 1u);
        const auto result = invoke(service, kBindings.pad_init, state, memory);
        expect(result.status == R5900GuestCallStatus::Fault && !service.initialized(),
               "nonzero padInit mode must fault without initialization");
        expect_contains(result.message, "padInit", "padInit fault must name function");
    }

    {
        auto memory = make_memory();
        Ps2PadHleService service(kBindings);
        R5900IrExecutionState state{};
        set_args(state, 0u, 0u, kValidPadArea);
        const auto result = invoke(service, kBindings.pad_port_open, state, memory);
        expect(result.status == R5900GuestCallStatus::Fault && !service.port_open(),
               "padPortOpen before padInit must fault");
        expect_contains(result.message, "padPortOpen",
                        "padPortOpen pre-init fault must name function");
    }

    {
        auto memory = make_memory();
        Ps2PadHleService service(kBindings);
        R5900IrExecutionState state{};
        set_args(state, 0u);
        expect(invoke(service, kBindings.pad_init, state, memory).status ==
                   R5900GuestCallStatus::Handled,
               "fixture padInit must succeed");

        set_args(state, 1u, 0u, kValidPadArea);
        expect(invoke(service, kBindings.pad_port_open, state, memory).status ==
                   R5900GuestCallStatus::Fault,
               "port 1 must be rejected");
        set_args(state, 0u, 1u, kValidPadArea);
        expect(invoke(service, kBindings.pad_port_open, state, memory).status ==
                   R5900GuestCallStatus::Fault,
               "slot 1 must be rejected");
        set_args(state, 0u, 0u, 0u);
        expect(invoke(service, kBindings.pad_port_open, state, memory).status ==
                   R5900GuestCallStatus::Fault,
               "null padArea must be rejected");
        set_args(state, 0u, 0u, kValidPadArea + 1u);
        expect(invoke(service, kBindings.pad_port_open, state, memory).status ==
                   R5900GuestCallStatus::Fault,
               "misaligned padArea must be rejected");
        set_args(state, 0u, 0u, kCrossingPadArea);
        expect(invoke(service, kBindings.pad_port_open, state, memory).status ==
                   R5900GuestCallStatus::Fault,
               "padArea crossing EE RAM end must be rejected");
        expect(!service.port_open() && service.pad_area_address() == 0u,
               "failed padPortOpen calls must not mutate closed state");
    }

    {
        auto memory = make_memory();
        Ps2PadHleService service(kBindings);
        R5900IrExecutionState state{};
        set_args(state, 0u);
        expect(invoke(service, kBindings.pad_init, state, memory).status ==
                   R5900GuestCallStatus::Handled,
               "fixture padInit must succeed");
        set_args(state, 0u, 0u, kValidPadArea);
        const auto opened = invoke(service, kBindings.pad_port_open, state, memory);
        expect(opened.status == R5900GuestCallStatus::Handled &&
                   service.port_open() && service.pad_area_address() == kValidPadArea &&
                   state.gpr[2].low64 == 1u,
               "valid padPortOpen must record state and return 1");

        set_args(state, 0u, 0u, kSecondPadArea + 1u);
        const auto failed_reopen = invoke(service, kBindings.pad_port_open, state, memory);
        expect(failed_reopen.status == R5900GuestCallStatus::Fault,
               "failed re-open must report Fault");
        expect(service.port_open() && service.pad_area_address() == kValidPadArea,
               "failed re-open must preserve previous open state/address");
    }

    {
        auto memory = make_memory();
        Ps2PadHleService service(kBindings);
        R5900IrExecutionState state{};
        set_args(state, 0u, 0u);
        const auto closed = invoke(service, kBindings.pad_get_state, state, memory);
        expect(closed.status == R5900GuestCallStatus::Handled && state.gpr[2].low64 == 0u,
               "closed padGetState must return PAD_STATE_DISCONN");

        set_args(state, 0u);
        expect(invoke(service, kBindings.pad_init, state, memory).status ==
                   R5900GuestCallStatus::Handled,
               "fixture padInit must succeed");
        set_args(state, 0u, 0u, kValidPadArea);
        expect(invoke(service, kBindings.pad_port_open, state, memory).status ==
                   R5900GuestCallStatus::Handled,
               "fixture padPortOpen must succeed");

        b3r::input::Ps2PadReport disconnected{};
        disconnected.connected = false;
        service.set_report(disconnected);
        set_args(state, 0u, 0u);
        expect(invoke(service, kBindings.pad_get_state, state, memory).status ==
                   R5900GuestCallStatus::Handled && state.gpr[2].low64 == 0u,
               "open disconnected padGetState must return DISCONN");

        b3r::input::Ps2PadReport connected{};
        connected.connected = true;
        service.set_report(connected);
        expect(invoke(service, kBindings.pad_get_state, state, memory).status ==
                   R5900GuestCallStatus::Handled && state.gpr[2].low64 == 6u,
               "open connected padGetState must return STABLE");

        set_args(state, 1u, 0u);
        const auto bad_port = invoke(service, kBindings.pad_get_state, state, memory);
        expect(bad_port.status == R5900GuestCallStatus::Fault,
               "padGetState must reject unsupported port");
        expect_contains(bad_port.message, "padGetState",
                        "padGetState fault must name function");
    }

    {
        auto memory = make_memory();
        Ps2PadHleService service(kBindings);
        R5900IrExecutionState state{};
        b3r::input::Ps2PadReport connected{};
        connected.connected = true;
        service.set_report(connected);

        set_args(state, 0u);
        expect(invoke(service, kBindings.pad_init, state, memory).status ==
                   R5900GuestCallStatus::Handled,
               "fixture padInit must succeed");
        set_args(state, 0u, 0u, kValidPadArea);
        expect(invoke(service, kBindings.pad_port_open, state, memory).status ==
                   R5900GuestCallStatus::Handled,
               "fixture padPortOpen must succeed");

        set_args(state, 0u, 0u);
        const auto closed = invoke(service, kBindings.pad_port_close, state, memory);
        expect(closed.status == R5900GuestCallStatus::Handled &&
                   !service.port_open() && service.pad_area_address() == 0u &&
                   state.gpr[2].low64 == 1u,
               "padPortClose must close, clear address, and return 1");
        const auto closed_again = invoke(service, kBindings.pad_port_close, state, memory);
        expect(closed_again.status == R5900GuestCallStatus::Handled &&
                   !service.port_open() && state.gpr[2].low64 == 1u,
               "padPortClose must be idempotent");

        set_args(state, 0u, 0u, kValidPadArea);
        expect(invoke(service, kBindings.pad_port_open, state, memory).status ==
                   R5900GuestCallStatus::Handled,
               "fixture re-open must succeed");
        const auto ended = invoke(service, kBindings.pad_end, state, memory);
        expect(ended.status == R5900GuestCallStatus::Handled &&
                   !service.initialized() && !service.port_open() &&
                   service.pad_area_address() == 0u && state.gpr[2].low64 == 1u,
               "padEnd must reset lifecycle state and return 1");

        set_args(state, 0u);
        expect(invoke(service, kBindings.pad_init, state, memory).status ==
                   R5900GuestCallStatus::Handled,
               "padInit after padEnd must succeed in HLE v0");
        set_args(state, 0u, 0u, kValidPadArea);
        expect(invoke(service, kBindings.pad_port_open, state, memory).status ==
                   R5900GuestCallStatus::Handled,
               "padPortOpen after re-init must succeed");
        set_args(state, 0u, 0u);
        expect(invoke(service, kBindings.pad_get_state, state, memory).status ==
                   R5900GuestCallStatus::Handled && state.gpr[2].low64 == 6u,
               "report snapshot must survive padEnd/re-init");
    }

    std::cout << "ps2_pad_hle_service_tests: PASS\n";
    return EXIT_SUCCESS;
}
