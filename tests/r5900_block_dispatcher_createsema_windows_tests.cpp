#include "recompiler/windows/r5900_block_dispatcher.h"
#include "r5900_createsema_test_support.h"

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
    std::cout << "EXTERNAL_STARTUP next_pc=0x" << std::hex << result.next_pc << std::dec
              << " blocks=" << result.blocks_executed
              << " instructions=" << result.instructions_executed
              << " syscalls=" << result.syscalls_handled
              << " semaphores=" << service.semaphores().size()
              << " diagnostic=" << result.message << '\n';
    expect(result.next_pc != 0x00114f08u,
           "external startup must execute past the observed post-CreateSema LD boundary");
    expect(result.syscalls_handled == 4u && service.semaphores().size() == 2u &&
               service.setup_thread_context().has_value() &&
               service.setup_heap_context().has_value(),
           "external startup must cross SetupThread, SetupHeap and two CreateSema calls");
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
    std::cout << "r5900_block_dispatcher_createsema_windows_tests: PASS\n";
}
