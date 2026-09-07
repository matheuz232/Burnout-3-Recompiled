#include "recompiler/windows/r5900_block_dispatcher.h"
#include "r5900_createsema_test_support.h"

using namespace b3r::recompiler;
using namespace b3r::test_support::createsema;

int main() {
    // Public ISA encodings and synthetic data only; no game payload.
    constexpr std::uint32_t entry = 0x00114ed0u;
    constexpr std::uint32_t wrapper = 0x0010be20u;
    auto memory = make_memory({
        {entry, {i_type(9u, 29u, 29u, 0xffb0u), i_type(9u, 0u, 2u, 1u),
                 i_type(0x3fu, 29u, 31u, 0x40u), r_type(29u, 0u, 4u, 0x2du),
                 i_type(0x2bu, 29u, 2u, 4u), i_type(0x2bu, 29u, 2u, 8u),
                 j_type(3u, wrapper), 0u, i_type(0x2bu, 29u, 2u, 0x30u),
                 0x70000000u}},
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
                   result.next_pc == entry + 36u,
               "startup must resume through JR and store ID before the sentinel");
        expect(result.blocks_executed == 4u && result.instructions_executed == 12u &&
                   result.syscalls_handled == 1u,
               "native instructions and host calls must be accounted separately");
        expect(state.gpr[2].low64 == id && state.gpr[2].high64 == 0xfedcba9876543210ull,
               "CreateSema must return a fresh ID and preserve v0 high64");
        expect(memory.read_u32(0x01ffffd0u) == id &&
                   memory.read_u64(0x01ffffe0u) == 0x00115118u,
               "guest SW must store the ID while the saved RA remains intact");
        expect(state.gpr[31].high64 == 0x123456789abcdef0ull,
               "native call and return must preserve RA high64");
        expect(result.cache_misses == (id == 1u ? 4u : 0u) &&
                   result.cache_hits == (id == 1u ? 0u : 4u) &&
                   result.recompilations == 0u,
               "cache replay must execute each syscall afresh");
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
