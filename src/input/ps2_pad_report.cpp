#include "input/ps2_pad_report.h"

namespace b3r::input {
namespace {

void encode_button(std::uint16_t& active_low,
                   const GameInputState& state,
                   GameInputButton host,
                   Ps2PadButton ps2) noexcept {
    if (is_button_pressed(state, host)) {
        active_low = static_cast<std::uint16_t>(
            active_low & ~static_cast<std::uint16_t>(ps2));
    }
}

} // namespace

std::uint8_t encode_ps2_stick_axis(float) noexcept {
    return 0x80u;
}

std::uint8_t encode_ps2_stick_vertical(float) noexcept {
    return 0x80u;
}

Ps2PadReport encode_ps2_pad_report(const GameInputState& state,
                                   bool virtual_pad_connected) noexcept {
    Ps2PadReport report{};
    if (!virtual_pad_connected) {
        return report;
    }

    report.connected = true;
    encode_button(report.buttons_active_low, state, GameInputButton::Select, Ps2PadButton::Select);
    encode_button(report.buttons_active_low, state, GameInputButton::L3, Ps2PadButton::L3);
    encode_button(report.buttons_active_low, state, GameInputButton::R3, Ps2PadButton::R3);
    encode_button(report.buttons_active_low, state, GameInputButton::Start, Ps2PadButton::Start);
    encode_button(report.buttons_active_low, state, GameInputButton::DpadUp, Ps2PadButton::DpadUp);
    encode_button(report.buttons_active_low, state, GameInputButton::DpadRight, Ps2PadButton::DpadRight);
    encode_button(report.buttons_active_low, state, GameInputButton::DpadDown, Ps2PadButton::DpadDown);
    encode_button(report.buttons_active_low, state, GameInputButton::DpadLeft, Ps2PadButton::DpadLeft);
    encode_button(report.buttons_active_low, state, GameInputButton::L2, Ps2PadButton::L2);
    encode_button(report.buttons_active_low, state, GameInputButton::R2, Ps2PadButton::R2);
    encode_button(report.buttons_active_low, state, GameInputButton::L1, Ps2PadButton::L1);
    encode_button(report.buttons_active_low, state, GameInputButton::R1, Ps2PadButton::R1);
    encode_button(report.buttons_active_low, state, GameInputButton::Triangle, Ps2PadButton::Triangle);
    encode_button(report.buttons_active_low, state, GameInputButton::Circle, Ps2PadButton::Circle);
    encode_button(report.buttons_active_low, state, GameInputButton::Cross, Ps2PadButton::Cross);
    encode_button(report.buttons_active_low, state, GameInputButton::Square, Ps2PadButton::Square);
    return report;
}

} // namespace b3r::input
