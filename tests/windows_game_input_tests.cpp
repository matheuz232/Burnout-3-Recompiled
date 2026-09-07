#include "platform/windows/windows_game_input.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using b3r::input::GameInputButton;
using b3r::input::GameInputState;
using b3r::input::is_button_pressed;
using b3r::platform::windows::WindowsGameInput;
using b3r::platform::windows::WindowsGameInputApi;

struct FakeEnvironment {
    std::array<DWORD, XUSER_MAX_COUNT> status{};
    std::array<XINPUT_STATE, XUSER_MAX_COUNT> states{};
    std::array<unsigned, XUSER_MAX_COUNT> calls{};
    std::array<bool, 256> keys{};
};

FakeEnvironment g_fake{};

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "windows_game_input_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

void expect_near(float actual, float expected, const std::string& message) {
    expect(std::fabs(actual - expected) <= 0.0001f, message);
}

void reset_fake() {
    g_fake = {};
    g_fake.status.fill(ERROR_DEVICE_NOT_CONNECTED);
}

DWORD WINAPI fake_xinput_get_state(DWORD index, XINPUT_STATE* state) {
    if (index >= XUSER_MAX_COUNT) {
        return ERROR_BAD_ARGUMENTS;
    }
    ++g_fake.calls[index];
    const DWORD result = g_fake.status[index];
    if (result == ERROR_SUCCESS && state != nullptr) {
        *state = g_fake.states[index];
    }
    return result;
}

SHORT WINAPI fake_get_async_key_state(int key) {
    if (key < 0 || key >= static_cast<int>(g_fake.keys.size())) {
        return 0;
    }
    return g_fake.keys[static_cast<std::size_t>(key)]
        ? static_cast<SHORT>(0x8000)
        : static_cast<SHORT>(0);
}

WindowsGameInput make_input() {
    return WindowsGameInput{WindowsGameInputApi{&fake_xinput_get_state,
                                                 &fake_get_async_key_state}};
}

void connect_controller_zero() {
    g_fake.status[0] = ERROR_SUCCESS;
}

void test_neutral_without_devices() {
    reset_fake();
    auto input = make_input();
    const GameInputState state = input.poll();
    expect(!state.gamepad_connected, "no controller should be disconnected");
    expect(state.buttons == 0u, "neutral buttons");
    expect_near(state.left_x, 0.0f, "neutral left x");
    expect_near(state.left_y, 0.0f, "neutral left y");
    expect_near(state.right_x, 0.0f, "neutral right x");
    expect_near(state.right_y, 0.0f, "neutral right y");
    expect_near(state.left_trigger, 0.0f, "neutral left trigger");
    expect_near(state.right_trigger, 0.0f, "neutral right trigger");
}

void test_xinput_digital_mappings() {
    struct Mapping {
        WORD native;
        GameInputButton canonical;
        const char* name;
    };

    constexpr std::array mappings{
        Mapping{XINPUT_GAMEPAD_A, GameInputButton::Cross, "A -> Cross"},
        Mapping{XINPUT_GAMEPAD_B, GameInputButton::Circle, "B -> Circle"},
        Mapping{XINPUT_GAMEPAD_X, GameInputButton::Square, "X -> Square"},
        Mapping{XINPUT_GAMEPAD_Y, GameInputButton::Triangle, "Y -> Triangle"},
        Mapping{XINPUT_GAMEPAD_DPAD_UP, GameInputButton::DpadUp, "DpadUp"},
        Mapping{XINPUT_GAMEPAD_DPAD_DOWN, GameInputButton::DpadDown, "DpadDown"},
        Mapping{XINPUT_GAMEPAD_DPAD_LEFT, GameInputButton::DpadLeft, "DpadLeft"},
        Mapping{XINPUT_GAMEPAD_DPAD_RIGHT, GameInputButton::DpadRight, "DpadRight"},
        Mapping{XINPUT_GAMEPAD_START, GameInputButton::Start, "Start"},
        Mapping{XINPUT_GAMEPAD_BACK, GameInputButton::Select, "Back -> Select"},
        Mapping{XINPUT_GAMEPAD_LEFT_SHOULDER, GameInputButton::L1, "Left shoulder"},
        Mapping{XINPUT_GAMEPAD_RIGHT_SHOULDER, GameInputButton::R1, "Right shoulder"},
        Mapping{XINPUT_GAMEPAD_LEFT_THUMB, GameInputButton::L3, "Left thumb"},
        Mapping{XINPUT_GAMEPAD_RIGHT_THUMB, GameInputButton::R3, "Right thumb"},
    };

    for (const auto& mapping : mappings) {
        reset_fake();
        connect_controller_zero();
        g_fake.states[0].Gamepad.wButtons = mapping.native;
        auto input = make_input();
        const auto state = input.poll();
        expect(state.gamepad_connected, std::string(mapping.name) + ": connected flag");
        expect(is_button_pressed(state, mapping.canonical), mapping.name);
    }
}

void test_stick_deadzones_and_extremes() {
    reset_fake();
    connect_controller_zero();
    g_fake.states[0].Gamepad.sThumbLX = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    g_fake.states[0].Gamepad.sThumbRY = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
    auto input = make_input();
    auto state = input.poll();
    expect_near(state.left_x, 0.0f, "left deadzone boundary x");
    expect_near(state.left_y, 0.0f, "left deadzone boundary y");
    expect_near(state.right_x, 0.0f, "right deadzone boundary x");
    expect_near(state.right_y, 0.0f, "right deadzone boundary y");

    reset_fake();
    connect_controller_zero();
    g_fake.states[0].Gamepad.sThumbLX = 32767;
    g_fake.states[0].Gamepad.sThumbLY = 0;
    g_fake.states[0].Gamepad.sThumbRX = -32768;
    g_fake.states[0].Gamepad.sThumbRY = 0;
    input = make_input();
    state = input.poll();
    expect_near(state.left_x, 1.0f, "left full positive x");
    expect_near(state.right_x, -1.0f, "right full negative x");
    expect(state.left_x >= -1.0f && state.left_x <= 1.0f, "left x range");
    expect(state.right_x >= -1.0f && state.right_x <= 1.0f, "right x range");
}

void test_trigger_threshold_and_saturation() {
    reset_fake();
    connect_controller_zero();
    g_fake.states[0].Gamepad.bLeftTrigger = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    g_fake.states[0].Gamepad.bRightTrigger = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    auto input = make_input();
    auto state = input.poll();
    expect_near(state.left_trigger, 0.0f, "left trigger threshold is zero");
    expect_near(state.right_trigger, 0.0f, "right trigger threshold is zero");
    expect(!is_button_pressed(state, GameInputButton::L2), "L2 threshold not digital");
    expect(!is_button_pressed(state, GameInputButton::R2), "R2 threshold not digital");

    reset_fake();
    connect_controller_zero();
    g_fake.states[0].Gamepad.bLeftTrigger = 255;
    g_fake.states[0].Gamepad.bRightTrigger = 255;
    input = make_input();
    state = input.poll();
    expect_near(state.left_trigger, 1.0f, "left trigger saturation");
    expect_near(state.right_trigger, 1.0f, "right trigger saturation");
    expect(is_button_pressed(state, GameInputButton::L2), "L2 full digital");
    expect(is_button_pressed(state, GameInputButton::R2), "R2 full digital");
}

void test_keyboard_button_mappings() {
    struct Mapping {
        int key;
        GameInputButton canonical;
        const char* name;
    };

    constexpr std::array mappings{
        Mapping{VK_UP, GameInputButton::DpadUp, "Up"},
        Mapping{VK_DOWN, GameInputButton::DpadDown, "Down"},
        Mapping{VK_LEFT, GameInputButton::DpadLeft, "Left"},
        Mapping{VK_RIGHT, GameInputButton::DpadRight, "Right"},
        Mapping{VK_RETURN, GameInputButton::Start, "Enter"},
        Mapping{VK_BACK, GameInputButton::Select, "Backspace"},
        Mapping{'X', GameInputButton::Cross, "X -> Cross"},
        Mapping{'C', GameInputButton::Circle, "C -> Circle"},
        Mapping{'Z', GameInputButton::Square, "Z -> Square"},
        Mapping{'V', GameInputButton::Triangle, "V -> Triangle"},
        Mapping{'Q', GameInputButton::L1, "Q -> L1"},
        Mapping{'E', GameInputButton::R1, "E -> R1"},
        Mapping{'1', GameInputButton::L2, "1 -> L2"},
        Mapping{'3', GameInputButton::R2, "3 -> R2"},
    };

    for (const auto& mapping : mappings) {
        reset_fake();
        g_fake.keys[static_cast<std::size_t>(mapping.key)] = true;
        auto input = make_input();
        const auto state = input.poll();
        expect(is_button_pressed(state, mapping.canonical), mapping.name);
        if (mapping.canonical == GameInputButton::L2) {
            expect_near(state.left_trigger, 1.0f, "keyboard L2 trigger");
        }
        if (mapping.canonical == GameInputButton::R2) {
            expect_near(state.right_trigger, 1.0f, "keyboard R2 trigger");
        }
    }
}

void test_keyboard_left_stick_and_opposites() {
    reset_fake();
    g_fake.keys['A'] = true;
    auto input = make_input();
    auto state = input.poll();
    expect_near(state.left_x, -1.0f, "A left");

    reset_fake();
    g_fake.keys['D'] = true;
    input = make_input();
    state = input.poll();
    expect_near(state.left_x, 1.0f, "D right");

    reset_fake();
    g_fake.keys['W'] = true;
    input = make_input();
    state = input.poll();
    expect_near(state.left_y, 1.0f, "W up");

    reset_fake();
    g_fake.keys['S'] = true;
    input = make_input();
    state = input.poll();
    expect_near(state.left_y, -1.0f, "S down");

    reset_fake();
    g_fake.keys['A'] = true;
    g_fake.keys['D'] = true;
    g_fake.keys['W'] = true;
    g_fake.keys['S'] = true;
    input = make_input();
    state = input.poll();
    expect_near(state.left_x, 0.0f, "A+D cancel");
    expect_near(state.left_y, 0.0f, "W+S cancel");
}

void test_keyboard_gamepad_merge() {
    reset_fake();
    connect_controller_zero();
    g_fake.states[0].Gamepad.wButtons = XINPUT_GAMEPAD_B;
    g_fake.states[0].Gamepad.sThumbLX = 32767;
    g_fake.states[0].Gamepad.sThumbRX = 32767;
    g_fake.keys['A'] = true;
    g_fake.keys['X'] = true;

    auto input = make_input();
    const auto state = input.poll();
    expect_near(state.left_x, -1.0f, "keyboard overrides gamepad left x");
    expect_near(state.right_x, 1.0f, "gamepad right x retained");
    expect(is_button_pressed(state, GameInputButton::Cross), "keyboard Cross retained");
    expect(is_button_pressed(state, GameInputButton::Circle), "gamepad Circle retained");
}

void test_null_injected_apis_are_safe() {
    reset_fake();
    WindowsGameInput input{WindowsGameInputApi{nullptr, nullptr}};
    const auto state = input.poll();
    expect(!state.gamepad_connected, "null APIs disconnected");
    expect(state.buttons == 0u, "null APIs neutral buttons");
}

} // namespace

int main() {
    test_neutral_without_devices();
    test_xinput_digital_mappings();
    test_stick_deadzones_and_extremes();
    test_trigger_threshold_and_saturation();
    test_keyboard_button_mappings();
    test_keyboard_left_stick_and_opposites();
    test_keyboard_gamepad_merge();
    test_null_injected_apis_are_safe();
    std::cout << "windows_game_input_tests: PASS\n";
    return EXIT_SUCCESS;
}
