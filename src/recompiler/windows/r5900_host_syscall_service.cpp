#include "recompiler/windows/r5900_host_syscall_service.h"

#include "recompiler/r5900_decoder.h"

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace b3r::recompiler {
namespace {

std::int32_t ee_syscall_selector(const R5900IrExecutionState& state) noexcept {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(state.gpr[3].low64));
}

std::string format_pc(std::uint32_t pc) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << pc;
    return out.str();
}

} // namespace

R5900HostSyscallResult R5900HostSyscallService::handle(
    const R5900HostSyscallRequest& request,
    R5900IrExecutionState& state,
    runtime::Ps2MemoryMap& memory) {
    (void)memory;

    if (decode_r5900(request.raw_instruction).instruction != R5900Instruction::Syscall) {
        return {
            R5900HostSyscallStatus::Fault,
            "host syscall at guest PC " + format_pc(request.guest_pc) +
                ": raw instruction is not SYSCALL",
        };
    }

    const auto selector = ee_syscall_selector(state);
    std::ostringstream out;
    out << "host syscall at guest PC " << format_pc(request.guest_pc)
        << ": unsupported EE syscall selector " << std::dec << selector
        << " (0x" << std::hex << static_cast<std::uint32_t>(selector) << ')';

    return {R5900HostSyscallStatus::Unsupported, out.str()};
}

} // namespace b3r::recompiler
