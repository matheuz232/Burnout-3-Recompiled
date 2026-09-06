#pragma once

#include "recompiler/r5900_ir_executor.h"
#include "runtime/ps2_memory_map.h"

#include <cstdint>
#include <string>

namespace b3r::recompiler {

enum class R5900HostSyscallStatus {
    Handled,
    Unsupported,
    Fault,
};

struct R5900HostSyscallRequest {
    std::uint32_t guest_pc{};
    std::uint32_t raw_instruction{};
};

struct R5900HostSyscallResult {
    R5900HostSyscallStatus status{R5900HostSyscallStatus::Unsupported};
    std::string message{};
};

class IR5900HostSyscallService {
public:
    virtual ~IR5900HostSyscallService() = default;

    [[nodiscard]] virtual R5900HostSyscallResult handle(
        const R5900HostSyscallRequest& request,
        R5900IrExecutionState& state,
        runtime::Ps2MemoryMap& memory) = 0;
};

class R5900HostSyscallService final : public IR5900HostSyscallService {
public:
    [[nodiscard]] R5900HostSyscallResult handle(
        const R5900HostSyscallRequest& request,
        R5900IrExecutionState& state,
        runtime::Ps2MemoryMap& memory) override;
};

} // namespace b3r::recompiler
