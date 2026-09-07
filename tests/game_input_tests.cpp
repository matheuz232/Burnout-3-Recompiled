#include "input/game_input.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "game_input_tests: FAIL: " << message << '\n';
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

void test_neutral_defaults() {
    const b3r::input::GameInputState state{};
    expect(state.buttons == 0u, "neutral buttons");
    expect_near(state.left_x, 0.0f, "neutral left_x");
    expect_near(state.left_y, 0.0f, "neutral left_y");
    expect_near(state.right_x, 0.0f, "neutral right_x");
    expect_near(state.right_y, 0.0f, "neutral right_y");
    expect_near(state.left_trigger, 0.0f, "neutral left trigger");
    expect_near(state.right_trigger, 0.0f, "neutral right trigger");
    expect(!state.gamepad_connected, "neutral connection flag");
}

void test_buttons_and_clamps() {
    b3r::input::GameInputState state{};
    b3r::input::set_button(state, b3r::input::GameInputButton::Cross);
    expect(b3r::input::is_button_pressed(state, b3r::input::GameInputButton::Cross),
           "Cross set/test");
    b3r::input::set_button(state, b3r::input::GameInputButton::Cross, false);
    expect(!b3r::input::is_button_pressed(state, b3r::input::GameInputButton::Cross),
           "Cross clear");
    expect_near(b3r::input::clamp_axis(-2.0f), -1.0f, "axis lower clamp");
    expect_near(b3r::input::clamp_axis(2.0f), 1.0f, "axis upper clamp");
    expect_near(b3r::input::clamp_axis(INFINITY), 0.0f, "axis infinity becomes neutral");
    expect_near(b3r::input::clamp_trigger(-1.0f), 0.0f, "trigger lower clamp");
    expect_near(b3r::input::clamp_trigger(2.0f), 1.0f, "trigger upper clamp");
    expect_near(b3r::input::clamp_trigger(NAN), 0.0f, "trigger NaN becomes neutral");
}

void test_digital_axis() {
    expect_near(b3r::input::digital_axis(false, false), 0.0f, "neutral digital axis");
    expect_near(b3r::input::digital_axis(true, false), -1.0f, "negative digital axis");
    expect_near(b3r::input::digital_axis(false, true), 1.0f, "positive digital axis");
    expect_near(b3r::input::digital_axis(true, true), 0.0f, "opposite digital axis cancels");
}

void test_merge() {
    b3r::input::GameInputState keyboard{};
    b3r::input::set_button(keyboard, b3r::input::GameInputButton::Cross);
    keyboard.left_x = -1.0f;
    keyboard.left_trigger = 1.0f;

    b3r::input::GameInputState gamepad{};
    b3r::input::set_button(gamepad, b3r::input::GameInputButton::Circle);
    gamepad.left_x = 0.5f;
    gamepad.left_y = 0.25f;
    gamepad.right_x = 0.75f;
    gamepad.right_y = -0.5f;
    gamepad.left_trigger = 0.25f;
    gamepad.right_trigger = 0.8f;
    gamepad.gamepad_connected = true;

    const auto merged = b3r::input::merge_input_states(keyboard, gamepad);
    expect(b3r::input::is_button_pressed(merged, b3r::input::GameInputButton::Cross),
           "merged Cross");
    expect(b3r::input::is_button_pressed(merged, b3r::input::GameInputButton::Circle),
           "merged Circle");
    expect_near(merged.left_x, -1.0f, "keyboard left_x override");
    expect_near(merged.left_y, 0.25f, "gamepad left_y retained");
    expect_near(merged.right_x, 0.75f, "gamepad right_x retained");
    expect_near(merged.right_y, -0.5f, "gamepad right_y retained");
    expect_near(merged.left_trigger, 1.0f, "trigger max left");
    expect_near(merged.right_trigger, 0.8f, "trigger max right");
    expect(merged.gamepad_connected, "connection metadata from gamepad");
}

} // namespace

int main() {
    test_neutral_defaults();
    test_buttons_and_clamps();
    test_digital_axis();
    test_merge();
    std::cout << "game_input_tests: PASS\n";
    return EXIT_SUCCESS;
}
