#include "analysis/ps2_pad_runtime_confirmation.h"
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

constexpr std::uint32_t j_type(std::uint8_t op, std::uint32_t target) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           ((target >> 2u) & 0x03ffffffu);
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

b3r::analysis::PadBindingDiscoveryResult pad_read_discovery(std::uint32_t pc) {
    using namespace b3r::analysis;
    PadBindingDiscoveryResult discovery{};
    for (std::size_t index = 0; index < discovery.resolutions.size(); ++index) {
        discovery.resolutions[index].function = static_cast<PadBindingFunction>(index);
    }
    auto& read = discovery.resolutions[
        static_cast<std::size_t>(PadBindingFunction::PadRead)];
    read.confidence = PadBindingConfidence::Candidate;
    read.evidence.push_back({PadBindingFunction::PadRead,
                             PadBindingEvidenceKind::StaticFingerprint,
                             pc,
                             100u,
                             "synthetic-padRead"});
    return discovery;
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

class UnrelatedGuestCallService final : public b3r::recompiler::IR5900GuestCallService {
public:
    explicit UnrelatedGuestCallService(std::uint32_t handled_pc) noexcept
        : handled_pc_(handled_pc) {}

    std::size_t calls{};

    b3r::recompiler::R5900GuestCallResult try_handle(
        const b3r::recompiler::R5900GuestCallRequest& request,
        b3r::recompiler::R5900IrExecutionState&,
        b3r::runtime::Ps2MemoryMap&) override {
        ++calls;
        if (request.guest_pc == handled_pc_) {
            return {b3r::recompiler::R5900GuestCallStatus::Handled, {}};
        }
        return {b3r::recompiler::R5900GuestCallStatus::NotHandled, {}};
    }

private:
    std::uint32_t handled_pc_{};
};

} // namespace

int main() {
    using namespace b3r::recompiler;

    constexpr std::uint32_t base = 0x00100000u;
    constexpr std::uint32_t andi =
        (0x0cu << 26u) | (1u << 21u) | (2u << 16u) | 0x00ffu;
    constexpr std::uint32_t unsupported_xori =
        (0x0eu << 26u) | (1u << 21u) | (1u << 16u) | 1u;

    {
        auto memory = make_memory({andi}, base);
        R5900BlockDispatcherOptions options{};
        options.block_options.max_instructions = 1u;
        R5900BlockDispatcher dispatcher(memory, options);
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x1234u;
        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted,
               "null guest service must preserve normal supported dispatch");
        expect(result.next_pc == base + 4u && result.guest_calls_handled == 0u,
               "null guest service must not intercept or alter normal next PC");
    }

    {
        auto memory = make_memory({andi}, base);
        FakeGuestCallService service{};
        service.expected_pc = base;
        service.status = R5900GuestCallStatus::NotHandled;
        R5900BlockDispatcherOptions options{};
        options.block_options.max_instructions = 1u;
        options.guest_calls = &service;
        R5900BlockDispatcher dispatcher(memory, options);
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x1234u;
        const auto result = dispatcher.run(base, state, 1u);
        expect(service.calls == 1u,
               "guest-call service must be queried at current PC");
        expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted &&
                   result.next_pc == base + 4u,
               "NotHandled must preserve normal supported dispatch");
        expect(result.guest_calls_handled == 0u && state.gpr[2].low64 == 0x34u,
               "NotHandled must not count or block the guest instruction");
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

    {
        using namespace b3r::analysis;
        constexpr std::uint32_t pad_read_pc = base + 0x20u;
        constexpr std::uint32_t guest_buffer = 0x00120000u;
        const std::vector<std::uint32_t> words{
            j_type(0x03u, pad_read_pc),
            0u,
            0u, 0u, 0u, 0u, 0u, 0u,
            unsupported_xori,
            0u,
        };
        auto memory = make_memory(words, base);
        const auto before_span = memory.translate(guest_buffer, 32u);
        expect(before_span.has_value(), "PAD runtime guest buffer must be backed");
        const std::vector<std::uint8_t> before(before_span->begin(), before_span->end());

        auto discovery = pad_read_discovery(pad_read_pc);
        Ps2PadRuntimeConfirmation confirmation(discovery, memory);
        UnrelatedGuestCallService guest_service(base + 0x0000f000u);
        R5900BlockDispatcherOptions options{};
        options.call_observer = &confirmation;
        options.guest_calls = &guest_service;
        R5900BlockDispatcher dispatcher(memory, options);

        R5900IrExecutionState first_state{};
        first_state.gpr[4].low64 = 0u;
        first_state.gpr[5].low64 = 0u;
        first_state.gpr[6].low64 = guest_buffer;
        const auto first = dispatcher.run(base, first_state, 1u);
        expect(first.reason == R5900DispatchStopReason::BlockBudgetExhausted &&
                   first.next_pc == pad_read_pc && first.blocks_executed == 1u,
               "synthetic padRead caller must complete exactly one guest block");
        expect(first.guest_calls_handled == 0u,
               "unrelated guest-call service must not handle synthetic padRead caller");

        const auto first_confirmation = confirmation.result();
        const auto& first_read = first_confirmation.functions[
            static_cast<std::size_t>(PadBindingFunction::PadRead)];
        expect(first_read.runtime_status == PadRuntimeConfirmationStatus::RuntimeConfirmed &&
                   first_read.guest_pc.has_value() && *first_read.guest_pc == pad_read_pc,
               "dispatcher observation must runtime-confirm synthetic padRead PC");
        expect(first_read.calls_observed == 1u && first_read.compatible_calls == 1u &&
                   first_read.incompatible_calls == 0u,
               "first synthetic padRead confirmation counters mismatch");

        const auto after_first_span = memory.translate(guest_buffer, 32u);
        expect(after_first_span.has_value() &&
                   std::vector<std::uint8_t>(after_first_span->begin(), after_first_span->end()) == before,
               "PAD runtime confirmation must not mutate the guest read buffer");

        R5900IrExecutionState second_state{};
        second_state.gpr[4].low64 = 0u;
        second_state.gpr[5].low64 = 0u;
        second_state.gpr[6].low64 = guest_buffer;
        const auto second = dispatcher.run(base, second_state, 1u);
        expect(second.fast_cache_hits == 1u && second.guest_calls_handled == 0u,
               "second synthetic padRead caller must use fast cache without guest HLE");
        const auto second_confirmation = confirmation.result();
        const auto& second_read = second_confirmation.functions[
            static_cast<std::size_t>(PadBindingFunction::PadRead)];
        expect(second_read.calls_observed == 2u && second_read.compatible_calls == 2u &&
                   second_read.incompatible_calls == 0u,
               "fast-cache replay must add exactly one compatible padRead observation");

        const auto after_second_span = memory.translate(guest_buffer, 32u);
        expect(after_second_span.has_value() &&
                   std::vector<std::uint8_t>(after_second_span->begin(), after_second_span->end()) == before,
               "fast-cache PAD confirmation must leave guest RAM byte-identical");
    }

    {
        using namespace b3r::analysis;
        constexpr std::uint32_t pad_read_pc = base + 0x20u;
        auto memory = make_memory({
            j_type(0x03u, pad_read_pc),
            0u,
            0u, 0u, 0u, 0u, 0u, 0u,
            unsupported_xori,
            0u,
        }, base);
        auto discovery = pad_read_discovery(pad_read_pc);
        Ps2PadRuntimeConfirmation confirmation(discovery, memory);
        R5900BlockDispatcherOptions options{};
        options.call_observer = &confirmation;
        R5900BlockDispatcher dispatcher(memory, options);
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 0u;
        state.gpr[5].low64 = 0u;
        state.gpr[6].low64 = 0x01fffff0u;
        const auto dispatch = dispatcher.run(base, state, 1u);
        expect(dispatch.blocks_executed == 1u,
               "invalid padRead ABI must not alter guest block execution");
        const auto result = confirmation.result();
        const auto& read = result.functions[
            static_cast<std::size_t>(PadBindingFunction::PadRead)];
        expect(read.runtime_status == PadRuntimeConfirmationStatus::ObservedIncompatible &&
                   read.calls_observed == 1u && read.compatible_calls == 0u &&
                   read.incompatible_calls == 1u,
               "partially backed padRead buffer must classify runtime-incompatible");
    }

    {
        using namespace b3r::analysis;
        constexpr std::uint32_t pad_read_pc = base + 0x20u;
        constexpr std::uint32_t unrelated_target = base + 0x24u;
        auto memory = make_memory({
            j_type(0x03u, unrelated_target),
            0u,
            0u, 0u, 0u, 0u, 0u, 0u,
            unsupported_xori,
            unsupported_xori,
            0u,
        }, base);
        auto discovery = pad_read_discovery(pad_read_pc);
        Ps2PadRuntimeConfirmation confirmation(discovery, memory);
        R5900BlockDispatcherOptions options{};
        options.call_observer = &confirmation;
        R5900BlockDispatcher dispatcher(memory, options);
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 0u;
        state.gpr[5].low64 = 0u;
        state.gpr[6].low64 = 0x00120000u;
        const auto dispatch = dispatcher.run(base, state, 1u);
        expect(dispatch.blocks_executed == 1u && dispatch.next_pc == unrelated_target,
               "unrelated synthetic JAL must still execute normally");
        const auto result = confirmation.result();
        const auto& read = result.functions[
            static_cast<std::size_t>(PadBindingFunction::PadRead)];
        expect(read.runtime_status == PadRuntimeConfirmationStatus::Unobserved &&
                   read.calls_observed == 0u,
               "JAL to non-evidence target must leave padRead runtime-unobserved");
    }

    std::cout << "r5900_block_dispatcher_guest_call_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
