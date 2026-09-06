#include "recompiler/windows/r5900_host_syscall_service.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_host_syscall_service_windows_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    runtime::Ps2MemoryMap memory{};
    R5900HostSyscallService service{};

    R5900IrExecutionState state{};
    state.gpr[0] = {};
    state.gpr[1] = {0x1122334455667788ull, 0x8877665544332211ull};
    state.gpr[3].low64 = 0x1234u;

    const auto before = state;
    const auto unsupported = service.handle(
        R5900HostSyscallRequest{0x00100000u, 0x0000000cu},
        state,
        memory);

    expect(unsupported.status == R5900HostSyscallStatus::Unsupported,
           "unknown EE syscall selector must be unsupported");
    expect(unsupported.message.find("0x00100000") != std::string::npos,
           "unsupported diagnostic must include guest PC");
    expect(unsupported.message.find("4660") != std::string::npos,
           "unsupported diagnostic must include decimal selector");
    expect(state.gpr[1].low64 == before.gpr[1].low64 &&
               state.gpr[1].high64 == before.gpr[1].high64,
           "unsupported syscall must not mutate unrelated state");
    expect(state.gpr[0].low64 == 0u && state.gpr[0].high64 == 0u,
           "unsupported syscall must preserve architectural r0");

    const auto malformed = service.handle(
        R5900HostSyscallRequest{0x00100004u, 0x00000000u},
        state,
        memory);
    expect(malformed.status == R5900HostSyscallStatus::Fault,
           "non-SYSCALL raw word must fault the host-service contract");
    expect(malformed.message.find("not SYSCALL") != std::string::npos,
           "malformed diagnostic must identify instruction kind");

    std::cout << "r5900_host_syscall_service_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
