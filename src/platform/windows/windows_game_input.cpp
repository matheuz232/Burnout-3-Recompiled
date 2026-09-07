#include "platform/windows/windows_game_input.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace b3r::platform::windows {
namespace {

DWORD WINAPI real_xinput_get_state(DWORD index, XINPUT_STATE* state) {
    return XInputGetState(index, state);
}

SHORT WINAPI real_get_async_key_state(int key) {
    return GetAsyncKeyState(key);
}

[[nodiscard]] bool key_down(const WindowsGameInputApi& api, int key) noexcept {
    if (api.get_async_key_state == nullptr) {
        return false;
    }
    const auto value = static_cast<std::uint16_t>(api.get_async_key_state(key));
    return (value & 0x8000u) != 0u;
}

[[nodiscard]] std::pair<float, float> normalize_stick(SHORT raw_x,
                                                       SHORT raw_y,
                                                       float deadzone) noexcept {
    const float x = static_cast<float>(raw_x);
    const float y = static_cast<float>(raw_y);
    const float magnitude = std::sqrt((x * x) + (y * y));
    if (magnitude <= deadzone || magnitude <= 0.0f) {
        return {0.0f, 0.0f};
    }

    constexpr float max_magnitude = 32767.0f;
    const float clamped_magnitude = std::min(magnitude, max_magnitude);
    const float scaled = (clamped_magnitude - deadzone) / (max_magnitude - deadzone);
    const float direction_x = x / magnitude;
    const float direction_y = y / magnitude;
    return {
        b3r::input::clamp_axis(direction_x * scaled),
        b3r::input::clamp_axis(direction_y * scaled),
    };
}

[[nodiscard]] float normalize_trigger(BYTE value) noexcept {
    if (value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
        return 0.0f;
    }

    const float numerator = static_cast<float>(value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    const float denominator = static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    return b3r::input::clamp_trigger(numerator / denominator);
}

void map_button(b3r::input::GameInputState& state,
                WORD native_buttons,
                WORD native_mask,
                b3r::input::GameInputButton canonical) noexcept {
    if ((native_buttons & native_mask) != 0u) {
        b3r::input::set_button(state, canonical);
    }
}

} // namespace

WindowsGameInput::WindowsGameInput() noexcept
    : api_{&real_xinput_get_state, &real_get_async_key_state} {}

WindowsGameInput::WindowsGameInput(WindowsGameInputApi api) noexcept
    : api_(api) {}

b3r::input::GameInputState WindowsGameInput::poll_keyboard() const noexcept {
    b3r::input::GameInputState state{};

    const bool dpad_up = key_down(api_, VK_UP);
    const bool dpad_down = key_down(api_, VK_DOWN);
    const bool dpad_left = key_down(api_, VK_LEFT);
    const bool dpad_right = key_down(api_, VK_RIGHT);
    b3r::input::set_button(state, b3r::input::GameInputButton::DpadUp, dpad_up);
    b3r::input::set_button(state, b3r::input::GameInputButton::DpadDown, dpad_down);
    b3r::input::set_button(state, b3r::input::GameInputButton::DpadLeft, dpad_left);
    b3r::input::set_button(state, b3r::input::GameInputButton::DpadRight, dpad_right);

    b3r::input::set_button(state, b3r::input::GameInputButton::Start, key_down(api_, VK_RETURN));
    b3r::input::set_button(state, b3r::input::GameInputButton::Select, key_down(api_, VK_BACK));
    b3r::input::set_button(state, b3r::input::GameInputButton::Cross, key_down(api_, 'X'));
    b3r::input::set_button(state, b3r::input::GameInputButton::Circle, key_down(api_, 'C'));
    b3r::input::set_button(state, b3r::input::GameInputButton::Square, key_down(api_, 'Z'));
    b3r::input::set_button(state, b3r::input::GameInputButton::Triangle, key_down(api_, 'V'));
    b3r::input::set_button(state, b3r::input::GameInputButton::L1, key_down(api_, 'Q'));
    b3r::input::set_button(state, b3r::input::GameInputButton::R1, key_down(api_, 'E'));

    const bool left_trigger = key_down(api_, '1');
    const bool right_trigger = key_down(api_, '3');
    b3r::input::set_button(state, b3r::input::GameInputButton::L2, left_trigger);
    b3r::input::set_button(state, b3r::input::GameInputButton::R2, right_trigger);
    state.left_trigger = left_trigger ? 1.0f : 0.0f;
    state.right_trigger = right_trigger ? 1.0f : 0.0f;

    state.left_x = b3r::input::digital_axis(key_down(api_, 'A'), key_down(api_, 'D'));
    state.left_y = b3r::input::digital_axis(key_down(api_, 'S'), key_down(api_, 'W'));
    return state;
}

b3r::input::GameInputState WindowsGameInput::map_gamepad(const XINPUT_GAMEPAD& gamepad) const noexcept {
    b3r::input::GameInputState state{};

    map_button(state, gamepad.wButtons, XINPUT_GAMEPAD_A, b3r::input::GameInputButton::Cross);
    map_button(state, gamepad.wButtons, XINPUT_GAMEPAD_B, b3r::input::GameInputButton::Circle);
    map_button(state, gamepad.wButtons, XINPUT_GAMEPAD_X, b3r::input::GameInputButton::Square);
    map_button(state, gamepad.wButtons, XINPUT_GAMEPAD_Y, b3r::input::GameInputButton::Triangle);
    map_button(state, gamepad.wButtons, XINPUT_GAMEPAD_DPAD_UP, b3r::input::GameInputButton::DpadUp);
    map_button(state, gamepad.wButtons, XINPUT_GAMEPAD_DPAD_DOWN, b3r::input::GameInputButton::DpadDown);
    map_button(state, gamepad.wButtons, XINPUT_GAMEPAD_DPAD_LEFT, b3r::input::GameInputButton::DpadLeft);
    map_button(state, gamepad.wButtons, XINPUT_GAMEPAD_DPAD_RIGHT, b3r::input::GameInputButton::DpadRight);
    map_button(state, gamepad.wButtons, XINPUT_GAMEPAD_START, b3r::input::GameInputButton::Start);
    map_button(state, gamepad.wButtons, XINPUT_GAMEPAD_BACK, b3r::input::GameInputButton::Select);
    map_button(state, gamepad.wButtons, XINPUT_GAMEPAD_LEFT_SHOULDER, b3r::input::GameInputButton::L1);
    map_button(state, gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_SHOULDER, b3r::input::GameInputButton::R1);
    map_button(state, gamepad.wButtons, XINPUT_GAMEPAD_LEFT_THUMB, b3r::input::GameInputButton::L3);
    map_button(state, gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_THUMB, b3r::input::GameInputButton::R3);

    const auto left = normalize_stick(gamepad.sThumbLX,
                                      gamepad.sThumbLY,
                                      static_cast<float>(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE));
    const auto right = normalize_stick(gamepad.sThumbRX,
                                       gamepad.sThumbRY,
                                       static_cast<float>(XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE));
    state.left_x = left.first;
    state.left_y = left.second;
    state.right_x = right.first;
    state.right_y = right.second;

    state.left_trigger = normalize_trigger(gamepad.bLeftTrigger);
    state.right_trigger = normalize_trigger(gamepad.bRightTrigger);
    if (gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
        b3r::input::set_button(state, b3r::input::GameInputButton::L2);
    }
    if (gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
        b3r::input::set_button(state, b3r::input::GameInputButton::R2);
    }

    return state;
}

bool WindowsGameInput::try_controller(DWORD index,
                                      b3r::input::GameInputState& state) noexcept {
    if (api_.xinput_get_state == nullptr || index >= XUSER_MAX_COUNT) {
        return false;
    }

    XINPUT_STATE native{};
    if (api_.xinput_get_state(index, &native) != ERROR_SUCCESS) {
        return false;
    }

    state = map_gamepad(native.Gamepad);
    state.gamepad_connected = true;
    return true;
}

b3r::input::GameInputState WindowsGameInput::poll() noexcept {
    b3r::input::GameInputState gamepad{};
    active_controller_ = XUSER_MAX_COUNT;
    for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
        if (try_controller(index, gamepad)) {
            active_controller_ = index;
            break;
        }
    }

    return b3r::input::merge_input_states(poll_keyboard(), gamepad);
}

} // namespace b3r::platform::windows
