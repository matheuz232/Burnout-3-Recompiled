#include "input/game_input.h"
#include "input/ps2_pad_report.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
using b3r::input::GameInputButton;
using b3r::input::GameInputState;
using b3r::input::Ps2PadButton;
using b3r::input::button_mask;
using b3r::input::encode_ps2_pad_report;

[[noreturn]] void fail(const char* message) {
    std::cerr << "ps2_pad_report_tests: FAIL: " << message << '\n';
    std::exit(1);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

struct ButtonCase {
    GameInputButton host;
    Ps2PadButton ps2;
};

constexpr std::array<ButtonCase, 16> kButtons{{
    {GameInputButton::Select,    Ps2PadButton::Select},
    {GameInputButton::L3,        Ps2PadButton::L3},
    {GameInputButton::R3,        Ps2PadButton::R3},
    {GameInputButton::Start,     Ps2PadButton::Start},
    {GameInputButton::DpadUp,    Ps2PadButton::DpadUp},
    {GameInputButton::DpadRight, Ps2PadButton::DpadRight},
    {GameInputButton::DpadDown,  Ps2PadButton::DpadDown},
    {GameInputButton::DpadLeft,  Ps2PadButton::DpadLeft},
    {GameInputButton::L2,        Ps2PadButton::L2},
    {GameInputButton::R2,        Ps2PadButton::R2},
    {GameInputButton::L1,        Ps2PadButton::L1},
    {GameInputButton::R1,        Ps2PadButton::R1},
    {GameInputButton::Triangle,  Ps2PadButton::Triangle},
    {GameInputButton::Circle,    Ps2PadButton::Circle},
    {GameInputButton::Cross,     Ps2PadButton::Cross},
    {GameInputButton::Square,    Ps2PadButton::Square},
}};

constexpr std::uint16_t ps2_mask(Ps2PadButton button) {
    return static_cast<std::uint16_t>(button);
}

void test_default_and_digital_buttons() {
    const b3r::input::Ps2PadReport default_report{};
    expect(!default_report.connected, "default report connected");
    expect(default_report.buttons_active_low == 0xffffu, "default buttons not neutral");
    expect(default_report.right_x == 0x80u && default_report.right_y == 0x80u &&
           default_report.left_x == 0x80u && default_report.left_y == 0x80u,
           "default sticks not neutral");

    const auto neutral = encode_ps2_pad_report(GameInputState{});
    expect(neutral.connected, "default virtual pad should be connected");
    expect(neutral.buttons_active_low == 0xffffu, "connected neutral buttons wrong");

    for (const auto& entry : kButtons) {
        GameInputState state{};
        state.buttons = button_mask(entry.host);
        const auto report = encode_ps2_pad_report(state);
        const auto expected = static_cast<std::uint16_t>(0xffffu & ~ps2_mask(entry.ps2));
        expect(report.buttons_active_low == expected, "single-button active-low mapping mismatch");
    }

    GameInputState combo{};
    combo.buttons = button_mask(GameInputButton::Cross) |
                    button_mask(GameInputButton::Start) |
                    button_mask(GameInputButton::DpadLeft);
    const auto combo_report = encode_ps2_pad_report(combo);
    const auto combo_mask = ps2_mask(Ps2PadButton::Cross) |
                            ps2_mask(Ps2PadButton::Start) |
                            ps2_mask(Ps2PadButton::DpadLeft);
    expect(combo_report.buttons_active_low == static_cast<std::uint16_t>(0xffffu & ~combo_mask),
           "combined active-low mapping mismatch");

    GameInputState all{};
    for (const auto& entry : kButtons) {
        all.buttons |= button_mask(entry.host);
    }
    expect(encode_ps2_pad_report(all).buttons_active_low == 0x0000u,
           "all buttons should clear all 16 PS2 bits");
}

void test_stick_axis_encoding() {
    using b3r::input::encode_ps2_stick_axis;
    using b3r::input::encode_ps2_stick_vertical;

    expect(encode_ps2_stick_axis(-1.0f) == 0x00u, "horizontal -1 endpoint");
    expect(encode_ps2_stick_axis(0.0f) == 0x80u, "horizontal neutral");
    expect(encode_ps2_stick_axis(+1.0f) == 0xffu, "horizontal +1 endpoint");
    expect(encode_ps2_stick_axis(-2.0f) == 0x00u, "horizontal low clamp");
    expect(encode_ps2_stick_axis(+2.0f) == 0xffu, "horizontal high clamp");
    expect(encode_ps2_stick_axis(-0.5f) == 0x40u, "horizontal negative midpoint");
    expect(encode_ps2_stick_axis(+0.5f) == 0xbfu, "horizontal positive midpoint");

    expect(encode_ps2_stick_vertical(+1.0f) == 0x00u, "vertical up endpoint");
    expect(encode_ps2_stick_vertical(0.0f) == 0x80u, "vertical neutral");
    expect(encode_ps2_stick_vertical(-1.0f) == 0xffu, "vertical down endpoint");
    expect(encode_ps2_stick_vertical(+0.5f) == 0x40u, "vertical positive midpoint");
    expect(encode_ps2_stick_vertical(-0.5f) == 0xbfu, "vertical negative midpoint");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    expect(encode_ps2_stick_axis(nan) == 0x80u, "NaN not neutral");
    expect(encode_ps2_stick_axis(inf) == 0x80u, "+Inf not neutral");
    expect(encode_ps2_stick_axis(-inf) == 0x80u, "-Inf not neutral");
    expect(encode_ps2_stick_vertical(nan) == 0x80u, "vertical NaN not neutral");
}

void test_report_stick_order_and_connection() {
    GameInputState state{};
    state.buttons = button_mask(GameInputButton::Cross);
    state.left_x = -1.0f;
    state.left_y = +1.0f;
    state.right_x = +1.0f;
    state.right_y = -1.0f;
    state.gamepad_connected = false;

    const auto keyboard_virtual_pad = encode_ps2_pad_report(state, true);
    expect(keyboard_virtual_pad.connected, "keyboard-only virtual pad disconnected");
    expect(keyboard_virtual_pad.left_x == 0x00u, "left_x field swapped/wrong");
    expect(keyboard_virtual_pad.left_y == 0x00u, "left_y field swapped/wrong");
    expect(keyboard_virtual_pad.right_x == 0xffu, "right_x field swapped/wrong");
    expect(keyboard_virtual_pad.right_y == 0xffu, "right_y field swapped/wrong");
    expect(keyboard_virtual_pad.buttons_active_low == 0xbfffu,
           "keyboard-only digital state lost");

    state.gamepad_connected = true;
    const auto explicitly_disconnected = encode_ps2_pad_report(state, false);
    expect(!explicitly_disconnected.connected, "explicit disconnect ignored");
    expect(explicitly_disconnected.buttons_active_low == 0xffffu,
           "disconnected report leaked buttons");
    expect(explicitly_disconnected.right_x == 0x80u &&
           explicitly_disconnected.right_y == 0x80u &&
           explicitly_disconnected.left_x == 0x80u &&
           explicitly_disconnected.left_y == 0x80u,
           "disconnected report leaked sticks");
}

} // namespace

int main() {
    test_default_and_digital_buttons();
    test_stick_axis_encoding();
    test_report_stick_order_and_connection();
    std::cout << "ps2_pad_report_tests: PASS\n";
    return 0;
}
