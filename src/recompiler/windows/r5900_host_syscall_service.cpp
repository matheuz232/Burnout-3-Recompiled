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
constexpr std::int32_t kCreateSemaSelector = 0x40;

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
        setup_heap_context_.reset();
        state.gpr[2].low64 = static_cast<std::uint64_t>(context.stack_top);
        return {R5900HostSyscallStatus::Handled, {}};
    }

    if (selector == kSetupHeapSelector) {
        const auto heap_start = ee_gpr_low32(state, 4u);
        const auto heap_size_raw = ee_gpr_low32(state, 5u);
        const auto heap_size_signed = std::bit_cast<std::int32_t>(heap_size_raw);

        if (!setup_thread_context_.has_value()) {
            return {
                R5900HostSyscallStatus::Fault,
                "SetupHeap requires a successful SetupThread context",
            };
        }

        if (heap_size_raw == std::numeric_limits<std::uint32_t>::max()) {
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

        if (heap_size_signed <= 0) {
            return {
                R5900HostSyscallStatus::Fault,
                "SetupHeap explicit heap_size must be positive",
            };
        }

        if (heap_start > std::numeric_limits<std::uint32_t>::max() - heap_size_raw) {
            return {
                R5900HostSyscallStatus::Fault,
                "SetupHeap explicit heap end overflows 32-bit guest address space",
            };
        }

        const auto heap_end = heap_start + heap_size_raw;
        if (heap_end > setup_thread_context_->stack_base) {
            return {
                R5900HostSyscallStatus::Fault,
                "SetupHeap explicit heap_end exceeds stack_base",
            };
        }

        setup_heap_context_ = R5900SetupHeapContext{
            heap_start,
            heap_size_raw,
            heap_end,
        };
        return {R5900HostSyscallStatus::Handled, {}};
    }

    if (selector == kCreateSemaSelector) {
        const auto address = ee_gpr_low32(state, 4u);
        const auto reject = [&](R5900HostSyscallStatus status, const char* detail) {
            return R5900HostSyscallResult{
                status,
                "CreateSema at guest PC " + format_pc(request.guest_pc) +
                    ", descriptor " + format_pc(address) + ": " + detail,
            };
        };
        if ((address & 3u) != 0u) {
            return reject(R5900HostSyscallStatus::Fault,
                          "descriptor must be four-byte aligned");
        }
        // Validate the complete EE ABI object before adding any field offsets.
        if (address == 0u || !memory.translate(address, 24u).has_value()) {
            return reject(R5900HostSyscallStatus::Fault,
                          "descriptor must be non-null and fully backed by EE RAM");
        }
        const auto maximum = *memory.read_u32(address + 4u);
        const auto initial = *memory.read_u32(address + 8u);
        const auto attr = *memory.read_u32(address + 16u);
        const auto option = *memory.read_u32(address + 20u);
        if (std::bit_cast<std::int32_t>(maximum) <= 0 ||
            std::bit_cast<std::int32_t>(initial) < 0 || initial > maximum) {
            return reject(R5900HostSyscallStatus::Fault,
                          "counts require max_count > 0 and 0 <= init_count <= max_count");
        }
        if (attr != 0u) {
            return reject(R5900HostSyscallStatus::Unsupported,
                          "nonzero semaphore attributes are unsupported in v0");
        }
        if (semaphore_count_ == semaphores_.size()) {
            return reject(R5900HostSyscallStatus::Fault,
                          "project v0 semaphore capacity exhausted");
        }
        const auto id = static_cast<std::uint32_t>(semaphore_count_ + 1u);
        semaphores_[semaphore_count_] = {id, initial, maximum, initial, 0u, attr, option};
        ++semaphore_count_;
        state.gpr[2].low64 = id;
        return {R5900HostSyscallStatus::Handled, {}};
    }

    std::ostringstream out;
    out << "host syscall at guest PC " << format_pc(request.guest_pc)
        << ": unsupported EE syscall selector " << std::dec << selector
        << " (0x" << std::hex << static_cast<std::uint32_t>(selector) << ')';

    return {R5900HostSyscallStatus::Unsupported, out.str()};
}

} // namespace b3r::recompiler
