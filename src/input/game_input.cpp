#include "input/game_input.h"

#include <algorithm>
#include <cmath>

namespace b3r::input {

bool is_button_pressed(const GameInputState& state, GameInputButton button) noexcept {
    return (state.buttons & button_mask(button)) != 0u;
}

void set_button(GameInputState& state, GameInputButton button, bool pressed) noexcept {
    const auto mask = button_mask(button);
    if (pressed) {
        state.buttons |= mask;
    } else {
        state.buttons &= ~mask;
    }
}

float clamp_axis(float value) noexcept {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return std::clamp(value, -1.0f, 1.0f);
}

float clamp_trigger(float value) noexcept {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return std::clamp(value, 0.0f, 1.0f);
}

float digital_axis(bool negative, bool positive) noexcept {
    if (negative == positive) {
        return 0.0f;
    }
    return negative ? -1.0f : 1.0f;
}

GameInputState merge_input_states(const GameInputState& keyboard,
                                  const GameInputState& gamepad) noexcept {
    GameInputState result{};
    result.buttons = keyboard.buttons | gamepad.buttons;
    result.left_x = keyboard.left_x != 0.0f ? clamp_axis(keyboard.left_x)
                                            : clamp_axis(gamepad.left_x);
    result.left_y = keyboard.left_y != 0.0f ? clamp_axis(keyboard.left_y)
                                            : clamp_axis(gamepad.left_y);
    result.right_x = clamp_axis(gamepad.right_x);
    result.right_y = clamp_axis(gamepad.right_y);
    result.left_trigger = std::max(clamp_trigger(keyboard.left_trigger),
                                   clamp_trigger(gamepad.left_trigger));
    result.right_trigger = std::max(clamp_trigger(keyboard.right_trigger),
                                    clamp_trigger(gamepad.right_trigger));
    result.gamepad_connected = gamepad.gamepad_connected;
    return result;
}

} // namespace b3r::input
