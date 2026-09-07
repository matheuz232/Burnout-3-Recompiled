#pragma once

#include "input/ps2_pad_report.h"
#include "recompiler/r5900_guest_call_service.h"

#include <cstdint>

namespace b3r::runtime {

struct Ps2PadHleBindings {
    std::uint32_t pad_init{};
    std::uint32_t pad_port_open{};
    std::uint32_t pad_get_state{};
    std::uint32_t pad_read{};
    std::uint32_t pad_port_close{};
    std::uint32_t pad_end{};
};

class Ps2PadHleService final : public recompiler::IR5900GuestCallService {
public:
    explicit Ps2PadHleService(Ps2PadHleBindings bindings) noexcept;

    void set_report(const input::Ps2PadReport& report) noexcept;

    [[nodiscard]] recompiler::R5900GuestCallResult try_handle(
        const recompiler::R5900GuestCallRequest& request,
        recompiler::R5900IrExecutionState& state,
        Ps2MemoryMap& memory) override;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool port_open() const noexcept;
    [[nodiscard]] std::uint32_t pad_area_address() const noexcept;

private:
    Ps2PadHleBindings bindings_{};
    input::Ps2PadReport report_{};
    bool initialized_{};
    bool port_open_{};
    std::uint32_t pad_area_address_{};
};

} // namespace b3r::runtime
