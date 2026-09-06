#include "recompiler/windows/r5900_host_syscall_service.h"

#include "recompiler/r5900_decoder.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>

namespace b3r::recompiler {
namespace {

constexpr std::int32_t kSetupThreadSelector = 0x3c;
constexpr std::int32_t kSetupHeapSelector = 0x3d;

std::int32_t ee_syscall_selector(const R5900IrExecutionState& state) noexcept {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(state.gpr[3].low64));
}

std::uint32_t ee_gpr_low32(
    const R5900IrExecutionState& state,
    std::size_t index) noexcept {
    return static_cast<std::uint32_t>(state.gpr[index].low64);
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
    if (selector == kSetupThreadSelector) {
        const auto gp = ee_gpr_low32(state, 4u);
        const auto stack = ee_gpr_low32(state, 5u);
        const auto stack_size_raw = ee_gpr_low32(state, 6u);
        const auto stack_size_signed = std::bit_cast<std::int32_t>(stack_size_raw);
        const auto args = ee_gpr_low32(state, 7u);
        const auto root_func = ee_gpr_low32(state, 8u);

        if (stack == std::numeric_limits<std::uint32_t>::max()) {
            return {
                R5900HostSyscallStatus::Unsupported,
                "SetupThread automatic-stack mode is unsupported in v0",
            };
        }

        if (stack_size_signed <= 0) {
            return {
                R5900HostSyscallStatus::Fault,
                "SetupThread explicit stack_size must be positive",
            };
        }

        if (stack > std::numeric_limits<std::uint32_t>::max() - stack_size_raw) {
            return {
                R5900HostSyscallStatus::Fault,
                "SetupThread explicit stack top overflows 32-bit guest address space",
            };
        }

        const R5900SetupThreadContext context{
            gp,
            stack,
            stack_size_raw,
            stack + stack_size_raw,
            args,
            root_func,
        };
        setup_thread_context_ = context;
        state.gpr[2].low64 = static_cast<std::uint64_t>(context.stack_top);
        return {R5900HostSyscallStatus::Handled, {}};
    }

    if (selector == kSetupHeapSelector) {
        const auto heap_start = ee_gpr_low32(state, 4u);
        const auto heap_size_raw = ee_gpr_low32(state, 5u);

        if (!setup_thread_context_.has_value()) {
            return {
                R5900HostSyscallStatus::Fault,
                "SetupHeap requires a successful SetupThread context",
            };
        }

        if (heap_size_raw != std::numeric_limits<std::uint32_t>::max()) {
            return {
                R5900HostSyscallStatus::Fault,
                "SetupHeap explicit-size mode is not implemented yet",
            };
        }

        const auto heap_end = setup_thread_context_->stack_base;
        if (heap_start >= heap_end) {
            return {
                R5900HostSyscallStatus::Fault,
                "SetupHeap automatic-size heap_start must be below stack_base",
            };
        }

        setup_heap_context_ = R5900SetupHeapContext{
            heap_start,
            heap_size_raw,
            heap_end,
        };
        return {R5900HostSyscallStatus::Handled, {}};
    }

    std::ostringstream out;
    out << "host syscall at guest PC " << format_pc(request.guest_pc)
        << ": unsupported EE syscall selector " << std::dec << selector
        << " (0x" << std::hex << static_cast<std::uint32_t>(selector) << ')';

    return {R5900HostSyscallStatus::Unsupported, out.str()};
}

} // namespace b3r::recompiler
