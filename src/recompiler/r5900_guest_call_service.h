#pragma once

#include "recompiler/r5900_ir_executor.h"

#include <cstdint>
#include <string>

namespace b3r::runtime {
class Ps2MemoryMap;
}

namespace b3r::recompiler {

struct R5900GuestCallRequest {
    std::uint32_t guest_pc{};
};

enum class R5900GuestCallStatus {
    Handled,
    NotHandled,
    Fault,
};

struct R5900GuestCallResult {
    R5900GuestCallStatus status{R5900GuestCallStatus::NotHandled};
    std::string message{};
};

class IR5900GuestCallService {
public:
    virtual ~IR5900GuestCallService() = default;

    [[nodiscard]] virtual R5900GuestCallResult try_handle(
        const R5900GuestCallRequest& request,
        R5900IrExecutionState& state,
        runtime::Ps2MemoryMap& memory) = 0;
};

} // namespace b3r::recompiler
