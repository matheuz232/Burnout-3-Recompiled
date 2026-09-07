#include "runtime/ps2_pad_hle_service.h"

#include "runtime/ps2_memory_map.h"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace b3r::runtime {
namespace {

constexpr std::uint32_t kPadStateDisconnected = 0x00u;
constexpr std::uint32_t kPadStateStable = 0x06u;
constexpr std::size_t kPadAreaSize = 256u;
constexpr std::uint32_t kPadAreaAlignment = 64u;

std::uint32_t gpr_low32(const recompiler::R5900IrExecutionState& state,
                        std::size_t index) noexcept {
    return static_cast<std::uint32_t>(state.gpr[index].low64);
}

void set_v0(recompiler::R5900IrExecutionState& state, std::uint32_t value) noexcept {
    state.gpr[2].low64 = value;
}

std::string format_pc(std::uint32_t pc) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << pc;
    return out.str();
}

recompiler::R5900GuestCallResult handled() {
    return {recompiler::R5900GuestCallStatus::Handled, {}};
}

recompiler::R5900GuestCallResult fault(std::string_view function,
                                       std::uint32_t pc,
                                       std::string detail) {
    return {
        recompiler::R5900GuestCallStatus::Fault,
        std::string(function) + " at guest PC " + format_pc(pc) + ": " + std::move(detail),
    };
}

bool is_port_zero_slot_zero(const recompiler::R5900IrExecutionState& state) noexcept {
    return gpr_low32(state, 4u) == 0u && gpr_low32(state, 5u) == 0u;
}

std::string port_slot_detail(const recompiler::R5900IrExecutionState& state) {
    std::ostringstream out;
    out << "unsupported port/slot " << gpr_low32(state, 4u)
        << '/' << gpr_low32(state, 5u) << "; v0 supports only 0/0";
    return out.str();
}

} // namespace

Ps2PadHleService::Ps2PadHleService(Ps2PadHleBindings bindings) noexcept
    : bindings_(bindings) {}

void Ps2PadHleService::set_report(const input::Ps2PadReport& report) noexcept {
    report_ = report;
}

bool Ps2PadHleService::initialized() const noexcept {
    return initialized_;
}

bool Ps2PadHleService::port_open() const noexcept {
    return port_open_;
}

std::uint32_t Ps2PadHleService::pad_area_address() const noexcept {
    return pad_area_address_;
}

recompiler::R5900GuestCallResult Ps2PadHleService::try_handle(
    const recompiler::R5900GuestCallRequest& request,
    recompiler::R5900IrExecutionState& state,
    Ps2MemoryMap& memory) {
    const auto pc = request.guest_pc;
    if (pc == 0u) {
        return {recompiler::R5900GuestCallStatus::NotHandled, {}};
    }

    if (bindings_.pad_init != 0u && pc == bindings_.pad_init) {
        const auto mode = gpr_low32(state, 4u);
        if (mode != 0u) {
            return fault("padInit", pc,
                         "mode must be 0, got " + std::to_string(mode));
        }
        initialized_ = true;
        set_v0(state, 1u);
        return handled();
    }

    if (bindings_.pad_port_open != 0u && pc == bindings_.pad_port_open) {
        if (!initialized_) {
            return fault("padPortOpen", pc, "padInit(0) has not completed");
        }
        if (!is_port_zero_slot_zero(state)) {
            return fault("padPortOpen", pc, port_slot_detail(state));
        }

        const auto pad_area = gpr_low32(state, 6u);
        if (pad_area == 0u) {
            return fault("padPortOpen", pc, "padArea must be non-null");
        }
        if ((pad_area % kPadAreaAlignment) != 0u) {
            return fault("padPortOpen", pc, "padArea must be 64-byte aligned");
        }
        if (!memory.translate(pad_area, kPadAreaSize).has_value()) {
            return fault("padPortOpen", pc,
                         "complete 256-byte padArea must be backed by EE RAM");
        }

        port_open_ = true;
        pad_area_address_ = pad_area;
        set_v0(state, 1u);
        return handled();
    }

    if (bindings_.pad_get_state != 0u && pc == bindings_.pad_get_state) {
        if (!is_port_zero_slot_zero(state)) {
            return fault("padGetState", pc, port_slot_detail(state));
        }
        const auto value = port_open_ && report_.connected
            ? kPadStateStable
            : kPadStateDisconnected;
        set_v0(state, value);
        return handled();
    }

    if (bindings_.pad_read != 0u && pc == bindings_.pad_read) {
        return fault("padRead", pc,
                     "report read is not enabled by the lifecycle implementation");
    }

    if (bindings_.pad_port_close != 0u && pc == bindings_.pad_port_close) {
        if (!is_port_zero_slot_zero(state)) {
            return fault("padPortClose", pc, port_slot_detail(state));
        }
        port_open_ = false;
        pad_area_address_ = 0u;
        set_v0(state, 1u);
        return handled();
    }

    if (bindings_.pad_end != 0u && pc == bindings_.pad_end) {
        initialized_ = false;
        port_open_ = false;
        pad_area_address_ = 0u;
        set_v0(state, 1u);
        return handled();
    }

    return {recompiler::R5900GuestCallStatus::NotHandled, {}};
}

} // namespace b3r::runtime
