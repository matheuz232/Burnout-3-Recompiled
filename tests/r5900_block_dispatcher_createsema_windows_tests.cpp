#include "analysis/ps2_pad_activation.h"
#include "input/ps2_pad_report.h"
#include "recompiler/windows/r5900_block_dispatcher.h"
#include "r5900_createsema_test_support.h"
#include "runtime/ps2_pad_hle_service.h"

#include <array>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>

using namespace b3r::recompiler;
using namespace b3r::test_support::createsema;

namespace {

const char* stop_reason_name(R5900DispatchStopReason reason) noexcept {
    switch (reason) {
    case R5900DispatchStopReason::BlockBudgetExhausted: return "BlockBudgetExhausted";
    case R5900DispatchStopReason::ControlFlow: return "ControlFlow";
    case R5900DispatchStopReason::UnsupportedInstruction: return "UnsupportedInstruction";
    case R5900DispatchStopReason::Trap: return "Trap";
    case R5900DispatchStopReason::UnsupportedSyscall: return "UnsupportedSyscall";
    case R5900DispatchStopReason::HostSyscallFailure: return "HostSyscallFailure";
    case R5900DispatchStopReason::GuestCallFailure: return "GuestCallFailure";
    case R5900DispatchStopReason::InvalidBlockBudget: return "InvalidBlockBudget";
    case R5900DispatchStopReason::AnalysisFailure: return "AnalysisFailure";
    case R5900DispatchStopReason::LoweringFailure: return "LoweringFailure";
    case R5900DispatchStopReason::CompileFailure: return "CompileFailure";
    case R5900DispatchStopReason::MemoryAccessFailure: return "MemoryAccessFailure";
    }
    return "UnknownStopReason";
}

const char* instruction_class_name(R5900InstructionClass instruction_class) noexcept {
    switch (instruction_class) {
    case R5900InstructionClass::Unknown: return "Unknown";
    case R5900InstructionClass::Alu: return "Alu";
    case R5900InstructionClass::Branch: return "Branch";
    case R5900InstructionClass::Jump: return "Jump";
    case R5900InstructionClass::Load: return "Load";
    case R5900InstructionClass::Store: return "Store";
    case R5900InstructionClass::System: return "System";
    }
    return "Unknown";
}

bool is_discovered_boundary(R5900DispatchStopReason reason) noexcept {
    return reason == R5900DispatchStopReason::UnsupportedInstruction ||
           reason == R5900DispatchStopReason::UnsupportedSyscall;
}

std::string format_boundary_probe(
    b3r::runtime::Ps2MemoryMap& memory,
    const R5900DispatchResult& result) {
    std::ostringstream output;
    output << "BOUNDARY_PROBE reason=" << stop_reason_name(result.reason)
           << " pc=0x" << std::hex << std::setfill('0') << std::setw(8) << result.next_pc;

    const auto raw = memory.read_u32(result.next_pc);
    if (raw.has_value()) {
        const auto decoded = decode_r5900(*raw);
        output << " raw=0x" << std::setw(8) << *raw
               << " instruction=" << r5900_instruction_name(decoded.instruction)
               << " class=" << instruction_class_name(decoded.instruction_class)
               << " opcode=0x" << std::setw(2)
               << static_cast<unsigned>(decoded.primary_opcode)
               << std::dec
               << " rs=" << static_cast<unsigned>(decoded.rs)
               << " rt=" << static_cast<unsigned>(decoded.rt)
               << " rd=" << static_cast<unsigned>(decoded.rd)
               << " immediate=0x" << std::hex << std::setw(4) << decoded.immediate;
    } else {
        output << " raw=UNMAPPED instruction=UNMAPPED class=UNMAPPED"
               << " opcode=UNMAPPED rs=UNMAPPED rt=UNMAPPED rd=UNMAPPED immediate=UNMAPPED";
    }

    output << std::dec
           << " blocks=" << result.blocks_executed
           << " instructions=" << result.instructions_executed
           << " syscalls=" << result.syscalls_handled;
    return output.str();
}

void validate_external_startup(const char* path) {
    std::ifstream input(path, std::ios::binary);
    expect(static_cast<bool>(input), "external ELF must open");
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    expect(!input.bad(), "external ELF read must finish");
    const auto parsed = parse_ps2_elf(bytes);
    expect(parsed.ok(), "complete external ELF must pass the production loader");
    expect(parsed.image->entry_point() == 0x00100008u, "unexpected external entry point");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "external ELF must map into EE RAM");
    R5900HostSyscallService service;
    R5900BlockDispatcherOptions options;
    options.host_syscalls = &service;
    options.block_options.max_instructions = 256u;
    R5900BlockDispatcher dispatcher(*built.memory, options);
    R5900IrExecutionState state;
    const auto result = dispatcher.run(parsed.image->entry_point(), state, 4000000u);

    std::cout << format_boundary_probe(*built.memory, result)
              << " semaphores=" << service.semaphores().size()
              << " diagnostic=" << result.message << '\n';

    expect(result.next_pc != 0x00114f08u,
           "external startup must execute past the observed post-CreateSema LD boundary");
    expect(result.syscalls_handled == 4u && service.semaphores().size() == 2u &&
               service.setup_thread_context().has_value() &&
               service.setup_heap_context().has_value(),
           "external startup must cross SetupThread, SetupHeap and two CreateSema calls");
    expect(is_discovered_boundary(result.reason),
           "external startup must stop at a controlled unsupported instruction or syscall boundary");
    for (const auto& semaphore : service.semaphores()) {
        expect(semaphore.count == 1u && semaphore.max_count == 1u && semaphore.attr == 0u,
               "external semaphore parameters must match the diagnosed startup");
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2) {
        validate_external_startup(argv[1]);
        return EXIT_SUCCESS;
    }
    expect(argc == 1, "usage: r5900_block_dispatcher_createsema_windows_tests [external ELF]");

    expect(is_discovered_boundary(R5900DispatchStopReason::UnsupportedInstruction),
           "unsupported instruction must be a discovered boundary");
    expect(is_discovered_boundary(R5900DispatchStopReason::UnsupportedSyscall),
           "unsupported syscall must be a discovered boundary");
    expect(!is_discovered_boundary(R5900DispatchStopReason::CompileFailure) &&
               !is_discovered_boundary(R5900DispatchStopReason::AnalysisFailure) &&
               !is_discovered_boundary(R5900DispatchStopReason::MemoryAccessFailure) &&
               !is_discovered_boundary(R5900DispatchStopReason::HostSyscallFailure),
           "unexpected execution failures must not be accepted as discovered boundaries");

    // Public ISA encodings and synthetic data only; no game payload.
    constexpr std::uint32_t entry = 0x00114ed0u;
    constexpr std::uint32_t wrapper = 0x0010be20u;
    auto memory = make_memory({
        {entry, {i_type(9u, 29u, 29u, 0xffb0u), i_type(9u, 0u, 2u, 1u),
                 i_type(0x3fu, 29u, 31u, 0x40u), r_type(29u, 0u, 4u, 0x2du),
                 i_type(0x2bu, 29u, 2u, 4u), i_type(0x2bu, 29u, 2u, 8u),
                 j_type(3u, wrapper), 0u, i_type(0x2bu, 29u, 2u, 0x30u),
                 i_type(0x37u, 29u, 31u, 0x40u), 0x70000000u}},
        {wrapper, {i_type(9u, 0u, 3u, 0x40u), 0x0000000cu,
                   r_type(31u, 0u, 0u, 8u), 0u}},
    });
    R5900HostSyscallService service;
    R5900BlockDispatcherOptions options;
    options.host_syscalls = &service;
    R5900BlockDispatcher dispatcher(memory, options);
    for (std::uint32_t id = 1u; id <= 2u; ++id) {
        R5900IrExecutionState state;
        state.gpr[29].low64 = 0x01fffff0u;
        state.gpr[31] = {0x00115118u, 0x123456789abcdef0ull};
        state.gpr[2].high64 = 0xfedcba9876543210ull;
        const auto result = dispatcher.run(entry, state, 8u);
        expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction &&
                   result.next_pc == entry + 40u,
               "startup must execute LD and stop only at the following sentinel");
        expect(result.blocks_executed == 4u && result.instructions_executed == 13u &&
                   result.syscalls_handled == 1u,
               "native instructions and host calls must be accounted separately across LD");
        const auto boundary = format_boundary_probe(memory, result);
        expect(boundary.find("reason=UnsupportedInstruction") != std::string::npos &&
                   boundary.find("pc=0x00114ef8") != std::string::npos &&
                   boundary.find("raw=0x70000000") != std::string::npos &&
                   boundary.find("instruction=UNKNOWN") != std::string::npos,
               "boundary probe must report stable stop, PC, raw word and mnemonic");
        expect(state.gpr[2].low64 == id && state.gpr[2].high64 == 0xfedcba9876543210ull,
               "CreateSema must return a fresh ID and preserve v0 high64");
        expect(memory.read_u32(0x01ffffd0u) == id &&
                   memory.read_u64(0x01ffffe0u) == 0x00115118u,
               "guest SW must store the ID while the saved RA remains intact");
        expect(state.gpr[31].low64 == 0x00115118u &&
                   state.gpr[31].high64 == 0x123456789abcdef0ull,
               "LD must restore saved RA low64 and preserve RA high64");
        expect(result.cache_misses == (id == 1u ? 4u : 0u) &&
                   result.cache_hits == (id == 1u ? 0u : 4u) &&
                   result.fast_cache_hits == (id == 1u ? 0u : 2u) &&
                   result.recompilations == 0u,
               "cache replay must execute each syscall and LD afresh");
    }
    R5900IrExecutionState bad;
    bad.gpr[4].low64 = 0x02000000u;
    bad.gpr[2].low64 = 99u;
    const auto fault = dispatcher.run(wrapper, bad, 8u);
    expect(fault.reason == R5900DispatchStopReason::HostSyscallFailure &&
               fault.next_pc == wrapper + 4u && fault.syscalls_handled == 0u &&
               fault.blocks_executed == 1u && fault.instructions_executed == 1u &&
               bad.gpr[2].low64 == 99u,
           "bad descriptor must fault at exact syscall PC before the return");
    R5900BlockDispatcher no_service(memory);
    const auto trap = no_service.run(wrapper, bad, 8u);
    expect(trap.reason == R5900DispatchStopReason::Trap && trap.next_pc == wrapper + 4u,
           "null service must preserve the existing trap boundary");

    {
        using namespace b3r::analysis;
        constexpr std::array<std::uint32_t, 6> pad_pcs{
            0x00100100u,
            0x00100120u,
            0x00100140u,
            0x00100160u,
            0x00100180u,
            0x001001a0u,
        };
        constexpr std::uint32_t unknown_pc = 0x001001c0u;
        constexpr std::uint32_t return_pc = 0x001001e0u;
        constexpr std::uint32_t pad_area = 0x00120000u;
        constexpr std::uint32_t read_destination = 0x00121000u;

        auto pad_memory = make_memory({
            {pad_pcs[0], {0x70000000u}},
            {pad_pcs[1], {0x70000000u}},
            {pad_pcs[2], {0x70000000u}},
            {pad_pcs[3], {0x70000000u}},
            {pad_pcs[4], {0x70000000u}},
            {pad_pcs[5], {0x70000000u}},
            {return_pc, {0x70000000u}},
        });

        PadBindingDiscoveryResult discovery{};
        PadRuntimeConfirmationResult runtime_result{};
        for (std::size_t index = 0; index < pad_pcs.size(); ++index) {
            const auto function = static_cast<PadBindingFunction>(index);
            const auto confidence =
                index == 0u || index == 4u || index == 5u
                    ? PadBindingConfidence::Trusted
                    : (index == 2u ? PadBindingConfidence::Unresolved
                                   : PadBindingConfidence::Candidate);
            auto& resolution = discovery.resolutions[index];
            resolution.function = function;
            resolution.confidence = confidence;
            resolution.evidence.push_back({
                function,
                index == 0u ? PadBindingEvidenceKind::ElfSymbol
                            : PadBindingEvidenceKind::StaticFingerprint,
                pad_pcs[index],
                index == 0u ? 1000u : 100u,
                "synthetic-runtime-activation",
            });

            auto& runtime_function = runtime_result.functions[index];
            runtime_function.function = function;
            runtime_function.static_confidence = confidence;
            runtime_function.runtime_status = PadRuntimeConfirmationStatus::RuntimeConfirmed;
            runtime_function.guest_pc = pad_pcs[index];
            runtime_function.calls_observed = 1u;
            runtime_function.compatible_calls = 1u;
        }

        const auto decision =
            make_ps2_pad_activation_decision(discovery, runtime_result);
        expect(decision.readiness == PadActivationReadiness::Ready &&
                   decision.bindings.has_value(),
               "six synthetic evidence-backed confirmations must activate atomically");
        expect(decision.bindings->pad_init == pad_pcs[0] &&
                   decision.bindings->pad_port_open == pad_pcs[1] &&
                   decision.bindings->pad_get_state == pad_pcs[2] &&
                   decision.bindings->pad_read == pad_pcs[3] &&
                   decision.bindings->pad_port_close == pad_pcs[4] &&
                   decision.bindings->pad_end == pad_pcs[5],
               "activation decision must materialize the exact six synthetic PCs");

        b3r::runtime::Ps2PadHleService pad_service(*decision.bindings);
        b3r::input::Ps2PadReport report{};
        report.connected = true;
        report.buttons_active_low = 0xffefu;
        report.left_x = 0x12u;
        report.left_y = 0x34u;
        report.right_x = 0x56u;
        report.right_y = 0x78u;
        pad_service.set_report(report);

        R5900BlockDispatcherOptions pad_options{};
        pad_options.guest_calls = &pad_service;
        R5900BlockDispatcher pad_dispatcher(pad_memory, pad_options);

        const auto run_pad_call = [&](std::uint32_t pc, R5900IrExecutionState& state) {
            state.gpr[31].low64 = return_pc;
            const auto result = pad_dispatcher.run(pc, state, 1u);
            expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction &&
                       result.next_pc == return_pc,
                   "handled PAD HLE call must resume at the synthetic return sentinel");
            expect(result.guest_calls_handled == 1u && result.blocks_executed == 0u,
                   "PAD HLE interception must count one guest call and no native block");
            return result;
        };

        R5900IrExecutionState init_state{};
        init_state.gpr[4].low64 = 0u;
        run_pad_call(decision.bindings->pad_init, init_state);
        expect(init_state.gpr[2].low64 == 1u && pad_service.initialized(),
               "activated padInit must initialize HLE and return success");

        expect(pad_memory.translate(pad_area, 256u).has_value() &&
                   (pad_area % 64u) == 0u,
               "synthetic padArea must be aligned and completely backed");
        R5900IrExecutionState open_state{};
        open_state.gpr[4].low64 = 0u;
        open_state.gpr[5].low64 = 0u;
        open_state.gpr[6].low64 = pad_area;
        run_pad_call(decision.bindings->pad_port_open, open_state);
        expect(open_state.gpr[2].low64 == 1u && pad_service.port_open() &&
                   pad_service.pad_area_address() == pad_area,
               "activated padPortOpen must open the exact synthetic pad area");

        R5900IrExecutionState state_state{};
        state_state.gpr[4].low64 = 0u;
        state_state.gpr[5].low64 = 0u;
        run_pad_call(decision.bindings->pad_get_state, state_state);
        expect(state_state.gpr[2].low64 == 0x06u,
               "activated padGetState must report stable connected state");

        expect(pad_memory.translate(read_destination, 32u).has_value(),
               "synthetic padRead destination must be completely backed");
        R5900IrExecutionState read_state{};
        read_state.gpr[4].low64 = 0u;
        read_state.gpr[5].low64 = 0u;
        read_state.gpr[6].low64 = read_destination;
        run_pad_call(decision.bindings->pad_read, read_state);
        expect(read_state.gpr[2].low64 == 32u,
               "activated padRead must return the 32-byte report size");
        const auto read_span = pad_memory.translate(read_destination, 32u);
        expect(read_span.has_value(), "padRead output span must remain backed");
        constexpr std::array<std::uint8_t, 8> expected_prefix{
            0x00u, 0x79u, 0xefu, 0xffu, 0x56u, 0x78u, 0x12u, 0x34u};
        for (std::size_t index = 0; index < expected_prefix.size(); ++index) {
            expect((*read_span)[index] == expected_prefix[index],
                   "activated padRead report prefix mismatch");
        }
        for (std::size_t index = expected_prefix.size(); index < read_span->size(); ++index) {
            expect((*read_span)[index] == 0u,
                   "activated padRead report tail must remain zero");
        }

        R5900IrExecutionState close_state{};
        close_state.gpr[4].low64 = 0u;
        close_state.gpr[5].low64 = 0u;
        run_pad_call(decision.bindings->pad_port_close, close_state);
        expect(close_state.gpr[2].low64 == 1u && !pad_service.port_open() &&
                   pad_service.pad_area_address() == 0u,
               "activated padPortClose must clear the open state and pad area");

        R5900IrExecutionState end_state{};
        run_pad_call(decision.bindings->pad_end, end_state);
        expect(end_state.gpr[2].low64 == 1u && !pad_service.initialized() &&
                   !pad_service.port_open(),
               "activated padEnd must clear PAD HLE lifecycle state");

        const std::array<std::uint32_t, 6> materialized{
            decision.bindings->pad_init,
            decision.bindings->pad_port_open,
            decision.bindings->pad_get_state,
            decision.bindings->pad_read,
            decision.bindings->pad_port_close,
            decision.bindings->pad_end,
        };
        for (const auto pc : materialized) {
            expect(pc != unknown_pc,
                   "fixed unknown PC must differ from every materialized PAD binding");
        }
        R5900IrExecutionState unknown_state{};
        const auto unknown = pad_service.try_handle(
            R5900GuestCallRequest{unknown_pc}, unknown_state, pad_memory);
        expect(unknown.status == R5900GuestCallStatus::NotHandled,
               "unbound synthetic PC must remain outside activated PAD HLE");
    }

    std::cout << "r5900_block_dispatcher_createsema_windows_tests: PASS\n";
}