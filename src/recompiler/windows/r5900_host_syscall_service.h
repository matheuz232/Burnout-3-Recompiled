#pragma once

#include "recompiler/r5900_ir_executor.h"
#include "runtime/ps2_memory_map.h"

#include <cstdint>
#include <optional>
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

struct R5900SetupThreadContext {
    std::uint32_t gp{};
    std::uint32_t stack_base{};
    std::uint32_t stack_size{};
    std::uint32_t stack_top{};
    std::uint32_t args{};
    std::uint32_t root_func{};
};

struct R5900SetupHeapContext {
    std::uint32_t heap_start{};
    std::uint32_t requested_heap_size{};
    std::uint32_t heap_end{};
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

    [[nodiscard]] const std::optional<R5900SetupThreadContext>&
    setup_thread_context() const noexcept {
        return setup_thread_context_;
    }

    [[nodiscard]] const std::optional<R5900SetupHeapContext>&
    setup_heap_context() const noexcept {
        return setup_heap_context_;
    }

private:
    std::optional<R5900SetupThreadContext> setup_thread_context_{};
    std::optional<R5900SetupHeapContext> setup_heap_context_{};
};

} // namespace b3r::recompiler
