# Game Input v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add deterministic Windows host input acquisition using keyboard + XInput and publish it as a platform-neutral `GameInputState` sampled once per runtime frame.

**Architecture:** A new portable `b3r_input` library owns the canonical button/stick/trigger state and merge helpers. `b3r_windows_runtime` owns `WindowsGameInput`, which polls injectable Win32/XInput callbacks, normalizes controller data, merges keyboard input, and handles controller disconnect/reconnect. The existing Win32 bootstrap consumes one immutable sample per frame; PS2 PAD/SIO2/libpad remains a later milestone.

**Tech Stack:** C++20, CMake 3.25+, Visual Studio 2022 x64, Win32 `GetAsyncKeyState`, XInput `XInputGetState`, existing GitHub Actions Windows Server 2022 CI.

**Spec:** `docs/superpowers/specs/2026-09-07-game-input-v0-design.md`

## Global Constraints

- Windows x64 only for the host backend; portable canonical input code must not include Win32/XInput headers.
- No third-party input library.
- No PS2 SIO2/PAD protocol, libpad HLE, DualShock 2 pressure mode, rumble, remapping UI, Raw Input, DirectInput, HID, SDL, multiple players, or keyboard right-stick emulation in v0.
- Input polling is `noexcept` and non-fatal.
- Keyboard and XInput are sampled once per runtime frame before the placeholder simulation section.
- CI tests must not require physical keyboard/controller hardware.
- No proprietary Burnout 3 data may be added to the repository.
- TDD RED -> GREEN with focused commits and fresh Windows CI evidence before marking the component `CI_VALIDATED`.

---

## File structure locked by this plan

### New portable files

- `src/input/game_input.h` — canonical enums/state and public helper declarations.
- `src/input/game_input.cpp` — clamping, button mutation/query, digital-axis construction, deterministic merge.
- `tests/game_input_tests.cpp` — platform-neutral canonical-state contract.

### New Windows files

- `src/platform/windows/windows_game_input.h` — injectable Windows API contract and `WindowsGameInput` stateful poller declaration.
- `src/platform/windows/windows_game_input.cpp` — XInput/keyboard acquisition, normalization, mapping, controller selection/reconnect.
- `tests/windows_game_input_tests.cpp` — fake-API deterministic Windows backend tests.

### Modified files

- `CMakeLists.txt` — `b3r_input`, portable test, Windows source/link/test registration.
- `src/platform/windows/win_main.cpp` — one `WindowsGameInput::poll()` call per frame before simulation.
- `docs/PROGRESS.md` — mark host Game Input v0 `CI_VALIDATED` only after final green CI.
- `docs/validation/2026-09-07-game-input-v0.md` — RED/GREEN/final CI evidence.

---

### Task 1: Canonical platform-neutral input state

**Files:**
- Create: `src/input/game_input.h`
- Create: `src/input/game_input.cpp`
- Create: `tests/game_input_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: C++20 standard library only.
- Produces:

```cpp
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
```

Merge contract:

```text
buttons            = keyboard.buttons OR gamepad.buttons
left_x             = keyboard.left_x != 0 ? keyboard.left_x : gamepad.left_x
left_y             = keyboard.left_y != 0 ? keyboard.left_y : gamepad.left_y
right_x/right_y    = gamepad right stick
left_trigger       = max(keyboard.left_trigger, gamepad.left_trigger)
right_trigger      = max(keyboard.right_trigger, gamepad.right_trigger)
gamepad_connected  = gamepad.gamepad_connected
```

- [ ] **Step 1: Add the failing canonical test and CMake target**

Create `tests/game_input_tests.cpp` with this executable-test style:

```cpp
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
    if (!condition) fail(message);
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
    expect_near(b3r::input::clamp_trigger(-1.0f), 0.0f, "trigger lower clamp");
    expect_near(b3r::input::clamp_trigger(2.0f), 1.0f, "trigger upper clamp");
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
    expect(b3r::input::is_button_pressed(merged, b3r::input::GameInputButton::Cross), "merged Cross");
    expect(b3r::input::is_button_pressed(merged, b3r::input::GameInputButton::Circle), "merged Circle");
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
```

Add to `CMakeLists.txt` before Windows-only targets:

```cmake
add_library(b3r_input
  src/input/game_input.cpp
)
target_include_directories(b3r_input PUBLIC src)
if(MSVC)
  target_compile_options(b3r_input PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(b3r_input PRIVATE -Wall -Wextra -Wpedantic)
endif()
```

Inside `B3R_BUILD_TESTS`:

```cmake
add_executable(game_input_tests tests/game_input_tests.cpp)
target_link_libraries(game_input_tests PRIVATE b3r_input)
add_test(NAME game_input_tests COMMAND game_input_tests)
```

Do not create `src/input/game_input.*` yet in the RED commit.

- [ ] **Step 2: Commit and verify RED**

```bash
git add CMakeLists.txt tests/game_input_tests.cpp
git commit -m "test: define canonical game input contract"
```

Run Windows CI. Expected: Build failure because `src/input/game_input.cpp` and/or `input/game_input.h` do not exist. Confirm the failure is limited to the new canonical input target before continuing.

- [ ] **Step 3: Implement the minimal canonical model**

Create `src/input/game_input.h` with the exact interface above.

Create `src/input/game_input.cpp`:

```cpp
#include "input/game_input.h"

#include <algorithm>
#include <cmath>

namespace b3r::input {

bool is_button_pressed(const GameInputState& state, GameInputButton button) noexcept {
    return (state.buttons & button_mask(button)) != 0u;
}

void set_button(GameInputState& state, GameInputButton button, bool pressed) noexcept {
    const auto mask = button_mask(button);
    if (pressed) state.buttons |= mask;
    else state.buttons &= ~mask;
}

float clamp_axis(float value) noexcept {
    if (!std::isfinite(value)) return 0.0f;
    return std::clamp(value, -1.0f, 1.0f);
}

float clamp_trigger(float value) noexcept {
    if (!std::isfinite(value)) return 0.0f;
    return std::clamp(value, 0.0f, 1.0f);
}

float digital_axis(bool negative, bool positive) noexcept {
    if (negative == positive) return 0.0f;
    return negative ? -1.0f : 1.0f;
}

GameInputState merge_input_states(const GameInputState& keyboard,
                                  const GameInputState& gamepad) noexcept {
    GameInputState result{};
    result.buttons = keyboard.buttons | gamepad.buttons;
    result.left_x = keyboard.left_x != 0.0f ? clamp_axis(keyboard.left_x) : clamp_axis(gamepad.left_x);
    result.left_y = keyboard.left_y != 0.0f ? clamp_axis(keyboard.left_y) : clamp_axis(gamepad.left_y);
    result.right_x = clamp_axis(gamepad.right_x);
    result.right_y = clamp_axis(gamepad.right_y);
    result.left_trigger = std::max(clamp_trigger(keyboard.left_trigger), clamp_trigger(gamepad.left_trigger));
    result.right_trigger = std::max(clamp_trigger(keyboard.right_trigger), clamp_trigger(gamepad.right_trigger));
    result.gamepad_connected = gamepad.gamepad_connected;
    return result;
}

} // namespace b3r::input
```

- [ ] **Step 4: Verify GREEN and regressions**

Run locally where available:

```bash
cmake -S . -B build -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target game_input_tests
ctest --test-dir build -C Release -R '^game_input_tests$' --output-on-failure
```

On Windows CI, require `game_input_tests` PASS and no pre-existing portable test regression.

- [ ] **Step 5: Commit GREEN**

```bash
git add src/input/game_input.h src/input/game_input.cpp
git commit -m "feat: add canonical game input state"
```

---

### Task 2: Windows keyboard/XInput mapping and normalization

**Files:**
- Create: `src/platform/windows/windows_game_input.h`
- Create: `src/platform/windows/windows_game_input.cpp`
- Create: `tests/windows_game_input_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 `b3r::input::GameInputState`, `set_button`, `digital_axis`, `merge_input_states`.
- Produces:

```cpp
namespace b3r::platform::windows {

using XInputGetStateFn = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
using GetAsyncKeyStateFn = SHORT (WINAPI*)(int);

struct WindowsGameInputApi {
    XInputGetStateFn xinput_get_state{};
    GetAsyncKeyStateFn get_async_key_state{};
};

class WindowsGameInput {
public:
    WindowsGameInput() noexcept;
    explicit WindowsGameInput(WindowsGameInputApi api) noexcept;

    [[nodiscard]] b3r::input::GameInputState poll() noexcept;

private:
    [[nodiscard]] b3r::input::GameInputState poll_keyboard() const noexcept;
    [[nodiscard]] b3r::input::GameInputState map_gamepad(const XINPUT_GAMEPAD& gamepad) const noexcept;
    [[nodiscard]] bool try_controller(DWORD index, b3r::input::GameInputState& state) noexcept;

    WindowsGameInputApi api_{};
    DWORD active_controller_{XUSER_MAX_COUNT};
};

} // namespace b3r::platform::windows
```

`active_controller_ == XUSER_MAX_COUNT` means no active controller.

- [ ] **Step 1: Write RED tests for deterministic mappings and fake APIs**

Create `tests/windows_game_input_tests.cpp`. Use global fake state because the injected APIs are C function pointers:

```cpp
#include "platform/windows/windows_game_input.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

struct FakeInputEnvironment {
    std::array<DWORD, XUSER_MAX_COUNT> status{};
    std::array<XINPUT_STATE, XUSER_MAX_COUNT> states{};
    std::array<unsigned, XUSER_MAX_COUNT> calls{};
    std::array<bool, 256> keys{};
};

FakeInputEnvironment* g_fake{};

DWORD WINAPI fake_xinput_get_state(DWORD index, XINPUT_STATE* state) {
    ++g_fake->calls.at(index);
    if (g_fake->status.at(index) != ERROR_SUCCESS) return g_fake->status.at(index);
    *state = g_fake->states.at(index);
    return ERROR_SUCCESS;
}

SHORT WINAPI fake_get_async_key_state(int key) {
    if (key < 0 || key >= static_cast<int>(g_fake->keys.size())) return 0;
    return g_fake->keys[static_cast<std::size_t>(key)] ? static_cast<SHORT>(0x8000) : 0;
}

b3r::platform::windows::WindowsGameInput make_input(FakeInputEnvironment& env) {
    g_fake = &env;
    return b3r::platform::windows::WindowsGameInput({
        &fake_xinput_get_state,
        &fake_get_async_key_state,
    });
}
```

Tests in this RED must explicitly cover:

```cpp
void test_neutral_without_controller_or_keys();
void test_xinput_button_mapping();
void test_stick_deadzones_and_saturation();
void test_trigger_threshold_and_digital_l2_r2();
void test_keyboard_mapping_and_opposite_wasd_cancel();
void test_keyboard_gamepad_merge();
```

Use these exact assertions for representative coverage:

```cpp
// XInput physical mapping
env.status.fill(ERROR_DEVICE_NOT_CONNECTED);
env.status[0] = ERROR_SUCCESS;
env.states[0].Gamepad.wButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B |
                                  XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y |
                                  XINPUT_GAMEPAD_DPAD_UP |
                                  XINPUT_GAMEPAD_START |
                                  XINPUT_GAMEPAD_BACK |
                                  XINPUT_GAMEPAD_LEFT_SHOULDER |
                                  XINPUT_GAMEPAD_RIGHT_SHOULDER |
                                  XINPUT_GAMEPAD_LEFT_THUMB |
                                  XINPUT_GAMEPAD_RIGHT_THUMB;
const auto mapped = make_input(env).poll();
expect_pressed(mapped, GameInputButton::Cross);
expect_pressed(mapped, GameInputButton::Circle);
expect_pressed(mapped, GameInputButton::Square);
expect_pressed(mapped, GameInputButton::Triangle);
expect_pressed(mapped, GameInputButton::DpadUp);
expect_pressed(mapped, GameInputButton::Start);
expect_pressed(mapped, GameInputButton::Select);
expect_pressed(mapped, GameInputButton::L1);
expect_pressed(mapped, GameInputButton::R1);
expect_pressed(mapped, GameInputButton::L3);
expect_pressed(mapped, GameInputButton::R3);

// Deadzone exact zero
env.states[0].Gamepad.sThumbLX = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
env.states[0].Gamepad.sThumbLY = 0;
const auto deadzone = make_input(env).poll();
expect_near(deadzone.left_x, 0.0f, "left deadzone x");
expect_near(deadzone.left_y, 0.0f, "left deadzone y");

// Saturation/range
env.states[0].Gamepad.sThumbLX = 32767;
env.states[0].Gamepad.sThumbLY = 0;
const auto saturated = make_input(env).poll();
expect_near(saturated.left_x, 1.0f, "left stick saturation");

// Trigger threshold
env.states[0].Gamepad.bLeftTrigger = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
const auto threshold = make_input(env).poll();
expect_near(threshold.left_trigger, 0.0f, "trigger threshold exact zero");
expect(!is_button_pressed(threshold, GameInputButton::L2), "L2 not digital at threshold");
env.states[0].Gamepad.bLeftTrigger = 255;
const auto full_trigger = make_input(env).poll();
expect_near(full_trigger.left_trigger, 1.0f, "full trigger");
expect_pressed(full_trigger, GameInputButton::L2);

// Keyboard merge
env.keys['A'] = true;
env.keys['X'] = true;
env.states[0].Gamepad.sThumbLX = 32767;
env.states[0].Gamepad.wButtons = XINPUT_GAMEPAD_B;
const auto merged = make_input(env).poll();
expect_near(merged.left_x, -1.0f, "keyboard steering overrides gamepad axis");
expect_pressed(merged, GameInputButton::Cross);
expect_pressed(merged, GameInputButton::Circle);
```

Map all fixed keyboard bindings from the spec in the keyboard test, not only the representative examples.

Modify `CMakeLists.txt` RED-side to reference the not-yet-created Windows source and test:

```cmake
add_library(b3r_windows_runtime STATIC
  src/platform/windows/crash_handler.cpp
  src/platform/windows/qpc_clock.cpp
  src/platform/windows/win32_window.cpp
  src/platform/windows/windows_frame_pacer.cpp
  src/platform/windows/windows_game_input.cpp
)
target_link_libraries(b3r_windows_runtime PUBLIC b3r_core b3r_input dbghelp winmm Xinput)
```

Inside Windows tests:

```cmake
add_executable(windows_game_input_tests tests/windows_game_input_tests.cpp)
target_link_libraries(windows_game_input_tests PRIVATE b3r_windows_runtime)
if(MSVC)
  target_compile_options(windows_game_input_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()
add_test(NAME windows_game_input_tests COMMAND windows_game_input_tests)
```

Do not create `windows_game_input.*` in the RED commit.

- [ ] **Step 2: Commit and verify RED**

```bash
git add CMakeLists.txt tests/windows_game_input_tests.cpp
git commit -m "test: define Windows game input mapping contract"
```

Run Windows CI. Expected: Build failure only because `windows_game_input.cpp/.h` are absent. Confirm pre-existing targets are not failing for unrelated reasons.

- [ ] **Step 3: Implement keyboard polling and XInput normalization**

Create `windows_game_input.h` with the interface above and include:

```cpp
#include "input/game_input.h"
#include <windows.h>
#include <Xinput.h>
```

In `windows_game_input.cpp`, implement real API defaults:

```cpp
namespace {
DWORD WINAPI real_xinput_get_state(DWORD index, XINPUT_STATE* state) {
    return XInputGetState(index, state);
}
SHORT WINAPI real_get_async_key_state(int key) {
    return GetAsyncKeyState(key);
}
}

WindowsGameInput::WindowsGameInput() noexcept
    : WindowsGameInput({&real_xinput_get_state, &real_get_async_key_state}) {}

WindowsGameInput::WindowsGameInput(WindowsGameInputApi api) noexcept : api_(api) {}
```

Treat null injected function pointers as unavailable APIs rather than crashes: no XInput callback means no gamepad sample; no keyboard callback means neutral keyboard.

Use these helpers in the `.cpp`:

```cpp
struct StickValue { float x{}; float y{}; };

StickValue normalize_stick(SHORT raw_x, SHORT raw_y, SHORT deadzone) noexcept {
    const float x = static_cast<float>(raw_x);
    const float y = static_cast<float>(raw_y);
    const float magnitude = std::sqrt(x * x + y * y);
    if (magnitude <= static_cast<float>(deadzone)) return {};

    const float clamped_magnitude = std::min(magnitude, 32767.0f);
    const float normalized_magnitude =
        (clamped_magnitude - static_cast<float>(deadzone)) /
        (32767.0f - static_cast<float>(deadzone));
    const float direction_x = x / magnitude;
    const float direction_y = y / magnitude;
    return {
        b3r::input::clamp_axis(direction_x * normalized_magnitude),
        b3r::input::clamp_axis(direction_y * normalized_magnitude),
    };
}

float normalize_trigger(BYTE value) noexcept {
    if (value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) return 0.0f;
    return b3r::input::clamp_trigger(
        static_cast<float>(value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
        static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD));
}
```

`map_gamepad()` must map all XInput buttons in the spec, normalize both sticks using `XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE` / `XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE`, normalize both triggers, and set L2/R2 digital buttons only when the respective trigger byte is greater than `XINPUT_GAMEPAD_TRIGGER_THRESHOLD`.

`poll_keyboard()` must test the high bit only:

```cpp
const auto down = [this](int key) noexcept {
    return api_.get_async_key_state != nullptr &&
           (api_.get_async_key_state(key) & static_cast<SHORT>(0x8000)) != 0;
};
```

Map:

```text
VK_UP/DOWN/LEFT/RIGHT -> D-pad
W/S -> left_y +1/-1
A/D -> left_x -1/+1
VK_RETURN -> Start
VK_BACK -> Select
X -> Cross
C -> Circle
Z -> Square
V -> Triangle
Q -> L1
E -> R1
'1' -> L2 + left_trigger=1
'3' -> R2 + right_trigger=1
```

For W/S and A/D use `digital_axis(negative, positive)` so opposite keys cancel exactly.

At the end of `poll()`, return `merge_input_states(keyboard, gamepad)`.

- [ ] **Step 4: Verify mapping GREEN**

Run:

```bash
cmake --build build --config Release --target windows_game_input_tests
ctest --test-dir build -C Release -R '^(game_input_tests|windows_game_input_tests)$' --output-on-failure
```

Then Windows CI. Require both input tests PASS, existing Win32 window test PASS, and no new warning/error from the input source under `/W4 /permissive-`.

- [ ] **Step 5: Commit GREEN**

```bash
git add src/platform/windows/windows_game_input.h src/platform/windows/windows_game_input.cpp
git commit -m "feat: add Windows keyboard and XInput mapping"
```

---

### Task 3: Stateful controller selection, disconnect, and reconnect

**Files:**
- Modify: `tests/windows_game_input_tests.cpp`
- Modify: `src/platform/windows/windows_game_input.cpp`

**Interfaces:**
- Consumes: Task 2 `WindowsGameInput::poll()` and `active_controller_` sentinel contract.
- Produces behavior only; no new public interface.

Selection algorithm required by this task:

```text
if active_controller_ is valid:
    try active controller first
    if success: use it and do not scan others
    if failure: remember failed index, clear active controller
scan indices 0..XUSER_MAX_COUNT-1, skipping the just-failed active index
    first ERROR_SUCCESS becomes active and supplies the sample
if none succeed:
    publish neutral gamepad state with gamepad_connected=false
next poll repeats the scan, allowing reconnection
```

- [ ] **Step 1: Add failing stateful-selection tests**

Append these tests to `windows_game_input_tests.cpp`:

```cpp
void test_first_connected_controller_is_selected() {
    FakeInputEnvironment env{};
    env.status.fill(ERROR_DEVICE_NOT_CONNECTED);
    env.status[2] = ERROR_SUCCESS;
    env.states[2].Gamepad.wButtons = XINPUT_GAMEPAD_A;
    auto input = make_input(env);
    const auto state = input.poll();
    expect(state.gamepad_connected, "controller 2 connected");
    expect_pressed(state, GameInputButton::Cross);
    expect(env.calls[0] == 1 && env.calls[1] == 1 && env.calls[2] == 1,
           "initial scan stops at first connected index");
    expect(env.calls[3] == 0, "index after first connected was not scanned");
}

void test_active_controller_is_reused_without_full_scan() {
    FakeInputEnvironment env{};
    env.status.fill(ERROR_DEVICE_NOT_CONNECTED);
    env.status[1] = ERROR_SUCCESS;
    auto input = make_input(env);
    (void)input.poll();
    env.calls.fill(0);
    (void)input.poll();
    expect(env.calls[1] == 1, "active controller polled once");
    expect(env.calls[0] == 0 && env.calls[2] == 0 && env.calls[3] == 0,
           "active success avoids scan");
}

void test_disconnect_falls_back_to_another_controller() {
    FakeInputEnvironment env{};
    env.status.fill(ERROR_DEVICE_NOT_CONNECTED);
    env.status[1] = ERROR_SUCCESS;
    auto input = make_input(env);
    (void)input.poll();

    env.status[1] = ERROR_DEVICE_NOT_CONNECTED;
    env.status[3] = ERROR_SUCCESS;
    env.states[3].Gamepad.wButtons = XINPUT_GAMEPAD_B;
    env.calls.fill(0);
    const auto state = input.poll();
    expect(state.gamepad_connected, "fallback controller connected");
    expect_pressed(state, GameInputButton::Circle);
    expect(env.calls[1] == 1, "failed active controller tried first");
    expect(env.calls[0] == 1 && env.calls[2] == 1 && env.calls[3] == 1,
           "fallback scan checked remaining indices in order");
}

void test_all_disconnected_then_reconnect() {
    FakeInputEnvironment env{};
    env.status.fill(ERROR_DEVICE_NOT_CONNECTED);
    auto input = make_input(env);
    const auto disconnected = input.poll();
    expect(!disconnected.gamepad_connected, "all-disconnected state");

    env.status[0] = ERROR_SUCCESS;
    env.states[0].Gamepad.wButtons = XINPUT_GAMEPAD_A;
    env.calls.fill(0);
    const auto reconnected = input.poll();
    expect(reconnected.gamepad_connected, "reconnect detected on later poll");
    expect_pressed(reconnected, GameInputButton::Cross);
    expect(env.calls[0] == 1, "reconnect scan starts at zero");
}
```

Also add one case where the injected XInput callback returns a non-`ERROR_DEVICE_NOT_CONNECTED` error and verify the runtime treats it as a disconnected sample rather than terminating.

- [ ] **Step 2: Commit and verify RED**

```bash
git add tests/windows_game_input_tests.cpp
git commit -m "test: define XInput reconnect contract"
```

Run `windows_game_input_tests`. Expected: at least the active-controller reuse/fallback call-count contract fails if Task 2 only scans statelessly.

- [ ] **Step 3: Implement the stateful selection algorithm**

Implement `try_controller()`:

```cpp
bool WindowsGameInput::try_controller(DWORD index,
                                      b3r::input::GameInputState& state) noexcept {
    if (api_.xinput_get_state == nullptr || index >= XUSER_MAX_COUNT) return false;
    XINPUT_STATE native{};
    if (api_.xinput_get_state(index, &native) != ERROR_SUCCESS) return false;
    state = map_gamepad(native.Gamepad);
    state.gamepad_connected = true;
    return true;
}
```

Implement `poll()` around it:

```cpp
b3r::input::GameInputState WindowsGameInput::poll() noexcept {
    const auto keyboard = poll_keyboard();
    b3r::input::GameInputState gamepad{};

    DWORD failed_active = XUSER_MAX_COUNT;
    if (active_controller_ < XUSER_MAX_COUNT) {
        if (try_controller(active_controller_, gamepad)) {
            return b3r::input::merge_input_states(keyboard, gamepad);
        }
        failed_active = active_controller_;
        active_controller_ = XUSER_MAX_COUNT;
    }

    for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
        if (index == failed_active) continue;
        if (try_controller(index, gamepad)) {
            active_controller_ = index;
            break;
        }
    }

    return b3r::input::merge_input_states(keyboard, gamepad);
}
```

- [ ] **Step 4: Verify GREEN and complete Windows input suite**

Run:

```bash
ctest --test-dir build -C Release -R '^(game_input_tests|windows_game_input_tests|win32_window_smoke_tests)$' --output-on-failure
```

Then Windows CI. Require all new connection tests PASS and no regressions.

- [ ] **Step 5: Commit GREEN**

```bash
git add src/platform/windows/windows_game_input.cpp
git commit -m "feat: handle XInput disconnect and reconnect"
```

---

### Task 4: Runtime frame integration, final regression gate, and evidence

**Files:**
- Modify: `src/platform/windows/win_main.cpp`
- Modify: `docs/PROGRESS.md`
- Create: `docs/validation/2026-09-07-game-input-v0.md`

**Interfaces:**
- Consumes: Task 2/3 `b3r::platform::windows::WindowsGameInput`.
- Produces: one immutable `GameInputState` sample per runtime frame, ready for the later PS2 PAD adapter.

- [ ] **Step 1: Integrate one poll per frame**

Add:

```cpp
#include "platform/windows/windows_game_input.h"
```

After successful window creation and before the `try` frame-loop body, construct:

```cpp
b3r::platform::windows::WindowsGameInput game_input;
```

Inside:

```cpp
while (window.pump_messages()) {
```

place exactly one poll after `frame_begin` and before `simulation_begin`:

```cpp
const auto input_state = game_input.poll();
(void)input_state;
```

Do not log button state every frame and do not connect it to guest memory/HLE in this milestone.

- [ ] **Step 2: Build the actual Win32 executable and run focused regression tests**

Run:

```bash
cmake --build build --config Release --target Burnout3Recompiled_Test game_input_tests windows_game_input_tests
ctest --test-dir build -C Release -R '^(game_input_tests|windows_game_input_tests|win32_window_smoke_tests|qpc_clock_windows_tests|frame_pacer_windows_tests)$' --output-on-failure
```

Expected: all selected tests PASS and `Burnout3Recompiled_Test.exe` links with `b3r_windows_runtime` + XInput successfully.

- [ ] **Step 3: Commit runtime integration**

```bash
git add src/platform/windows/win_main.cpp
git commit -m "feat: sample game input once per frame"
```

- [ ] **Step 4: Run the full Windows CI gate on the implementation head**

Push the implementation head and require the complete Windows CI workflow to pass:

```text
Configure                PASS
Build                    PASS
CTest                    0 failures (expected total: previous 67 + game_input_tests + windows_game_input_tests = 69)
Frame pacing telemetry   PASS
Pacing probe smoke       PASS
Analyzer package         PASS
Pacing package           PASS
```

Do not claim `CI_VALIDATED` until the actual CTest total and zero-failure output are observed; if the repository gains/removes unrelated tests before execution, record the observed total rather than forcing `69`.

- [ ] **Step 5: Write validation record and update progress snapshot**

Create `docs/validation/2026-09-07-game-input-v0.md` with these sections and concrete values from the executed work:

```markdown
# Game Input v0 Validation

Date: 2026-09-07
Branch: <implementation branch>
Final implementation head before docs: `<sha>`

## Scope
- canonical host input state
- keyboard fallback
- XInput mapping/deadzones/triggers
- deterministic keyboard+gamepad merge
- disconnect/reconnect controller selection
- one poll per Win32 frame
- explicitly no PS2 PAD/SIO2/libpad guest integration

## TDD evidence
- Canonical RED: `<sha>` — `<observed failure>`
- Canonical GREEN: `<sha>` — `<CI run/result>`
- Windows mapping RED: `<sha>` — `<observed failure>`
- Windows mapping GREEN: `<sha>` — `<CI run/result>`
- Reconnect RED: `<sha>` — `<observed failure>`
- Reconnect GREEN: `<sha>` — `<CI run/result>`

## Final CI
- Windows CI: `#<run>` (`<run id>`)
- CTest: `<pass>/<total> PASS`
- Frame pacing telemetry: PASS
- Pacing probe: PASS
- package gates: PASS

## Remaining boundary
Host input is available but Burnout 3 still cannot see controller state until a future PS2 PAD/SIO2/libpad adapter is implemented and validated.
```

Update `docs/PROGRESS.md`:

```text
Game input | CI_VALIDATED | Keyboard + XInput host acquisition, deterministic deadzones/merge/reconnect; PS2 PAD adapter remains pending
```

Keep `Game initialization`, `Menu / gameplay`, graphics, and audio statuses unchanged.

- [ ] **Step 6: Commit documentation in one commit**

```bash
git add docs/PROGRESS.md docs/validation/2026-09-07-game-input-v0.md
git commit -m "docs: record Game Input v0 validation"
```

- [ ] **Step 7: Run the final CI gate on the exact documentation head**

Push the documentation head. Fetch that exact workflow run and verify:

```text
head_sha == documentation commit SHA
status == completed
conclusion == success
CTest == 0 failures
all pacing/package steps == success
```

Only after this gate may `Game Input v0` be reported complete/`CI_VALIDATED`.

---

## Plan self-review result

- Spec coverage: all canonical model, mappings, radial deadzones, trigger thresholds, keyboard bindings, merge rules, controller selection/reconnect, runtime polling, CI and documentation requirements map to Tasks 1–4.
- Scope: PS2 PAD/SIO2/libpad, vibration, remapping, Raw Input/HID and multiplayer remain excluded.
- Type consistency: `GameInputButton`, `GameInputState`, `WindowsGameInputApi`, `WindowsGameInput::poll()`, callback signatures and `active_controller_` sentinel are used consistently across tasks.
- Placeholder scan: implementation steps contain concrete APIs, algorithms, tests, commands and expected failures; documentation placeholders are intentionally runtime evidence fields to be replaced with observed SHAs/run IDs during Task 4, not implementation ambiguity.
