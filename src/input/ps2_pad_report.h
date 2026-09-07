#pragma once

#include "input/game_input.h"

#include <cstdint>

namespace b3r::input {

enum class Ps2PadButton : std::uint16_t {
    Select    = 0x0001,
    L3        = 0x0002,
    R3        = 0x0004,
    Start     = 0x0008,
    DpadUp    = 0x0010,
    DpadRight = 0x0020,
    DpadDown  = 0x0040,
    DpadLeft  = 0x0080,
    L2        = 0x0100,
    R2        = 0x0200,
    L1        = 0x0400,
    R1        = 0x0800,
    Triangle  = 0x1000,
    Circle    = 0x2000,
    Cross     = 0x4000,
    Square    = 0x8000,
};

struct Ps2PadReport {
    bool connected{};
    std::uint16_t buttons_active_low{0xffffu};
    std::uint8_t right_x{0x80u};
    std::uint8_t right_y{0x80u};
    std::uint8_t left_x{0x80u};
    std::uint8_t left_y{0x80u};
};

[[nodiscard]] std::uint8_t encode_ps2_stick_axis(float axis) noexcept;
[[nodiscard]] std::uint8_t encode_ps2_stick_vertical(float axis) noexcept;
[[nodiscard]] Ps2PadReport encode_ps2_pad_report(
    const GameInputState& state,
    bool virtual_pad_connected = true) noexcept;

} // namespace b3r::input
