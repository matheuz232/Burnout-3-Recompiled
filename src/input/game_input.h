#pragma once

#include <cstdint>

namespace b3r::input {

enum class GameInputButton : std::uint32_t {
    DpadUp    = 1u << 0,
    DpadDown  = 1u << 1,
    DpadLeft  = 1u << 2,
    DpadRight = 1u << 3,
    Start     = 1u << 4,
    Select    = 1u << 5,
    Cross     = 1u << 6,
    Circle    = 1u << 7,
    Square    = 1u << 8,
    Triangle  = 1u << 9,
    L1        = 1u << 10,
    R1        = 1u << 11,
    L2        = 1u << 12,
    R2        = 1u << 13,
    L3        = 1u << 14,
    R3        = 1u << 15,
};

struct GameInputState {
    std::uint32_t buttons{};
    float left_x{};
    float left_y{};
    float right_x{};
    float right_y{};
    float left_trigger{};
    float right_trigger{};
    bool gamepad_connected{};
};

[[nodiscard]] constexpr std::uint32_t button_mask(GameInputButton button) noexcept {
    return static_cast<std::uint32_t>(button);
}

[[nodiscard]] bool is_button_pressed(const GameInputState& state,
                                     GameInputButton button) noexcept;
void set_button(GameInputState& state, GameInputButton button, bool pressed = true) noexcept;
[[nodiscard]] float clamp_axis(float value) noexcept;
[[nodiscard]] float clamp_trigger(float value) noexcept;
[[nodiscard]] float digital_axis(bool negative, bool positive) noexcept;
[[nodiscard]] GameInputState merge_input_states(const GameInputState& keyboard,
                                                const GameInputState& gamepad) noexcept;

} // namespace b3r::input
