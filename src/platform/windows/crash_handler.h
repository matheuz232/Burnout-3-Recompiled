#pragma once

#include <cstdint>

namespace b3r::platform::windows {

class CrashHandler {
public:
    static void install() noexcept;
    static void set_execution_markers(std::uint32_t ps2_address, std::uintptr_t native_address) noexcept;
};

} // namespace b3r::platform::windows
