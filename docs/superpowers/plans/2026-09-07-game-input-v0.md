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
- Keyboard and XInput are sampled once per runtime frame before the simulation section.
- CI tests must not require physical keyboard/controller hardware.
- No proprietary Burnout 3 data may be added to the repository.
- TDD RED -> GREEN with focused commits and fresh Windows CI evidence before marking the component `CI_VALIDATED`.

---

## File Structure

New portable files:

```text
src/input/game_input.h
src/input/game_input.cpp
tests/game_input_tests.cpp
```

New Windows files:

```text
src/platform/windows/windows_game_input.h
src/platform/windows/windows_game_input.cpp
tests/windows_game_input_tests.cpp
```

Modified files:

```text
CMakeLists.txt
src/platform/windows/win_main.cpp
docs/PROGRESS.md
```

Final validation record:

```text
docs/validation/2026-09-07-game-input-v0.md
```

---

### Task 1: Canonical Platform-Neutral Input State

**Files:**
- Create: `src/input/game_input.h`
- Create: `src/input/game_input.cpp`
- Create: `tests/game_input_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

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

[[nodiscard]] bool is_button_pressed(const GameInputState&, GameInputButton) noexcept;
void set_button(GameInputState&, GameInputButton, bool pressed = true) noexcept;
[[nodiscard]] float clamp_axis(float) noexcept;
[[nodiscard]] float clamp_trigger(float) noexcept;
[[nodiscard]] float digital_axis(bool negative, bool positive) noexcept;
[[nodiscard]] GameInputState merge_input_states(const GameInputState& keyboard,
                                                const GameInputState& gamepad) noexcept;

} // namespace b3r::input
```

Merge semantics are fixed:

```text
buttons            = keyboard OR gamepad
left_x/left_y      = nonzero keyboard axis, otherwise gamepad axis
right_x/right_y    = gamepad only
triggers           = max(keyboard, gamepad)
gamepad_connected  = gamepad source only
```

- [ ] **Step 1: Write the failing portable test**

Create `tests/game_input_tests.cpp` using the repository's executable-test pattern (`expect`, `fail`, `EXIT_FAILURE`). Cover all of these exact cases:

```cpp
GameInputState{};                              // all neutral
set_button(state, GameInputButton::Cross);    // set/query
set_button(state, GameInputButton::Cross, false);
clamp_axis(-2.0f) == -1.0f;
clamp_axis(2.0f) == 1.0f;
clamp_axis(NAN) == 0.0f;
clamp_trigger(-1.0f) == 0.0f;
clamp_trigger(2.0f) == 1.0f;
clamp_trigger(NAN) == 0.0f;
digital_axis(false, false) == 0.0f;
digital_axis(true, false) == -1.0f;
digital_axis(false, true) == 1.0f;
digital_axis(true, true) == 0.0f;
```

Add a merge test with keyboard `Cross`, `left_x=-1`, `left_trigger=1`; gamepad `Circle`, `left_x=.5`, `left_y=.25`, `right_x=.75`, `right_y=-.5`, `left_trigger=.25`, `right_trigger=.8`, `gamepad_connected=true`. Assert OR-ed buttons, keyboard `left_x`, gamepad `left_y/right_x/right_y`, trigger maxima, and connected metadata.

Add to `CMakeLists.txt`:

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

Do not create `src/input/game_input.*` yet.

- [ ] **Step 2: Commit and verify RED**

```bash
git add CMakeLists.txt tests/game_input_tests.cpp
git commit -m "test: define canonical game input contract"
```

Run the Windows CI workflow. Expected: **Configure fails** because `src/input/game_input.cpp` does not exist. Confirm no unrelated failure before continuing.

- [ ] **Step 3: Implement the minimal portable model**

Create `src/input/game_input.h` with the exact public interface above.

Implement `src/input/game_input.cpp` with:

```cpp
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
```

Implement `merge_input_states()` exactly according to the fixed merge semantics above, clamping every published axis/trigger before returning it.

- [ ] **Step 4: Verify GREEN**

```bash
cmake -S . -B build -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target game_input_tests
ctest --test-dir build -C Release -R '^game_input_tests$' --output-on-failure
```

Then run Windows CI and require the new test plus all pre-existing tests to pass.

- [ ] **Step 5: Commit GREEN**

```bash
git add src/input/game_input.h src/input/game_input.cpp
git commit -m "feat: add canonical game input state"
```

---

### Task 2: Windows Keyboard/XInput Mapping and Normalization

**Files:**
- Create: `src/platform/windows/windows_game_input.h`
- Create: `src/platform/windows/windows_game_input.cpp`
- Create: `tests/windows_game_input_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

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
    [[nodiscard]] b3r::input::GameInputState map_gamepad(const XINPUT_GAMEPAD&) const noexcept;
    [[nodiscard]] bool try_controller(DWORD index,
                                      b3r::input::GameInputState& state) noexcept;

    WindowsGameInputApi api_{};
    DWORD active_controller_{XUSER_MAX_COUNT};
};

} // namespace b3r::platform::windows
```

- [ ] **Step 1: Write the failing Windows mapping test**

Create `tests/windows_game_input_tests.cpp` with a global fake environment containing:

```cpp
std::array<DWORD, XUSER_MAX_COUNT> status;
std::array<XINPUT_STATE, XUSER_MAX_COUNT> states;
std::array<unsigned, XUSER_MAX_COUNT> calls;
std::array<bool, 256> keys;
```

Inject C callbacks:

```cpp
DWORD WINAPI fake_xinput_get_state(DWORD index, XINPUT_STATE* state);
SHORT WINAPI fake_get_async_key_state(int key);
```

Cover all XInput digital mappings:

```text
A Cross
B Circle
X Square
Y Triangle
DPAD_* matching D-pad
START Start
BACK Select
LEFT_SHOULDER L1
RIGHT_SHOULDER R1
LEFT_THUMB L3
RIGHT_THUMB R3
```

Cover these normalization contracts:

```text
left stick magnitude <= XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE => exact zero
right stick magnitude <= XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE => exact zero
full positive axis => +1 within 0.0001
full negative axis => -1 within 0.0001
trigger <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD => 0 and no L2/R2 digital bit
trigger 255 => 1 and corresponding L2/R2 digital bit
```

Cover every keyboard binding from the spec:

```text
Arrows -> D-pad
W/A/S/D -> left stick
Enter -> Start
Backspace -> Select
X/C/Z/V -> Cross/Circle/Square/Triangle
Q/E -> L1/R1
1/3 -> L2/R2 + trigger 1.0
```

Assert A+D and W+S cancel their respective keyboard axis to zero.

Add a merge case where keyboard A overrides positive XInput left-X while gamepad right stick remains intact and keyboard/gamepad digital buttons are both preserved.

Modify `CMakeLists.txt`:

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

Do not create `windows_game_input.*` yet.

- [ ] **Step 2: Commit and verify RED**

```bash
git add CMakeLists.txt tests/windows_game_input_tests.cpp
git commit -m "test: define Windows game input mapping contract"
```

Run Windows CI. Expected: **Configure fails** because `src/platform/windows/windows_game_input.cpp` does not exist.

- [ ] **Step 3: Implement real API defaults and normalization**

Header includes remain Windows-only:

```cpp
#include "input/game_input.h"
#include <windows.h>
#include <Xinput.h>
```

Default callbacks:

```cpp
DWORD WINAPI real_xinput_get_state(DWORD index, XINPUT_STATE* state) {
    return XInputGetState(index, state);
}

SHORT WINAPI real_get_async_key_state(int key) {
    return GetAsyncKeyState(key);
}
```

Null injected callbacks are treated as unavailable APIs, never dereferenced.

Use radial deadzone removal:

```cpp
const float magnitude = std::sqrt(x * x + y * y);
if (magnitude <= deadzone) return {0.0f, 0.0f};
const float clamped = std::min(magnitude, 32767.0f);
const float scaled = (clamped - deadzone) / (32767.0f - deadzone);
result_x = clamp_axis((x / magnitude) * scaled);
result_y = clamp_axis((y / magnitude) * scaled);
```

Use trigger normalization:

```cpp
if (value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) return 0.0f;
return clamp_trigger(
    static_cast<float>(value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
    static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD));
```

Set L2/R2 digital bits only when the raw trigger byte is strictly greater than the threshold.

Keyboard state uses only the `GetAsyncKeyState` high bit (`0x8000`). Use `digital_axis()` for W/S and A/D so opposites cancel exactly.

`poll()` returns `merge_input_states(poll_keyboard(), gamepad_state)`.

- [ ] **Step 4: Verify GREEN**

```bash
cmake --build build --config Release --target windows_game_input_tests
ctest --test-dir build -C Release -R '^(game_input_tests|windows_game_input_tests)$' --output-on-failure
```

Run Windows CI and require both input suites plus the existing Win32 runtime tests to pass.

- [ ] **Step 5: Commit GREEN**

```bash
git add src/platform/windows/windows_game_input.h src/platform/windows/windows_game_input.cpp
git commit -m "feat: add Windows keyboard and XInput mapping"
```

---

### Task 3: Controller Selection, Disconnect, and Reconnect

**Files:**
- Modify: `tests/windows_game_input_tests.cpp`
- Modify: `src/platform/windows/windows_game_input.cpp`

**Public interface:** unchanged from Task 2.

**State machine:**

```text
If active_controller_ is valid:
  poll it first
  success -> use it and stop
  failure -> remember failed index and clear active_controller_
Scan controller indices 0..XUSER_MAX_COUNT-1, skipping the just-failed active index
  first success -> store as active and use it
No success -> neutral gamepad state, gamepad_connected=false
Next frame scans again, so reconnect is discoverable
```

- [ ] **Step 1: Add failing reconnect tests**

Add deterministic cases that assert both state and call counts:

```text
controller 2 is first connected -> scan 0,1,2; do not poll 3
next frame -> poll only active controller 2
active controller 1 disconnects and controller 3 is connected -> poll 1 first, then scan 0,2,3
all disconnected -> gamepad_connected=false
later controller 0 connects -> next poll discovers controller 0
arbitrary non-success XInput error -> treated as disconnected, not fatal
```

- [ ] **Step 2: Commit and verify RED**

```bash
git add tests/windows_game_input_tests.cpp
git commit -m "test: define XInput reconnect contract"
```

Run only `windows_game_input_tests`. Expected: active-controller reuse and/or fallback call-count assertions fail until stateful selection is implemented.

- [ ] **Step 3: Implement `try_controller()` and stateful `poll()`**

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

Use the exact state machine above, and skip the active index during the fallback scan after it has already failed once in the same frame.

- [ ] **Step 4: Verify GREEN**

```bash
ctest --test-dir build -C Release -R '^(game_input_tests|windows_game_input_tests|win32_window_smoke_tests)$' --output-on-failure
```

Then run Windows CI and require zero regressions.

- [ ] **Step 5: Commit GREEN**

```bash
git add src/platform/windows/windows_game_input.cpp
git commit -m "feat: handle XInput disconnect and reconnect"
```

---

### Task 4: Runtime Integration and Final Validation

**Files:**
- Modify: `src/platform/windows/win_main.cpp`
- Modify: `docs/PROGRESS.md`
- Create: `docs/validation/2026-09-07-game-input-v0.md`

- [ ] **Step 1: Integrate one sample per frame**

Add:

```cpp
#include "platform/windows/windows_game_input.h"
```

After successful window creation:

```cpp
b3r::platform::windows::WindowsGameInput game_input;
```

Inside the frame loop, immediately after `frame_begin` and before `simulation_begin`, add exactly one call:

```cpp
const auto input_state = game_input.poll();
(void)input_state;
```

Do not log per-frame input and do not expose it to guest state in this milestone.

- [ ] **Step 2: Build the actual Win32 executable and focused regressions**

```bash
cmake --build build --config Release --target Burnout3Recompiled_Test game_input_tests windows_game_input_tests
ctest --test-dir build -C Release -R '^(game_input_tests|windows_game_input_tests|win32_window_smoke_tests|qpc_clock_windows_tests|frame_pacer_windows_tests)$' --output-on-failure
```

Expected: every selected test passes and `Burnout3Recompiled_Test.exe` links successfully with XInput.

- [ ] **Step 3: Commit runtime integration**

```bash
git add src/platform/windows/win_main.cpp
git commit -m "feat: sample game input once per frame"
```

- [ ] **Step 4: Run full Windows CI on the implementation head**

Require:

```text
Configure                PASS
Build                    PASS
CTest                    0 failures
Frame pacing telemetry   PASS
Pacing probe smoke       PASS
Analyzer package         PASS
Pacing package           PASS
```

The expected CTest count is the previous 67 plus `game_input_tests` and `windows_game_input_tests`, normally 69. Record the actual count emitted by CI rather than assuming it if unrelated tests changed.

- [ ] **Step 5: Record evidence using actual observed values**

Create `docs/validation/2026-09-07-game-input-v0.md`. Populate every SHA/run/count from Git/GitHub output obtained during Tasks 1–4. The record must contain these headings:

```markdown
# Game Input v0 Validation

## Scope
## TDD evidence
## Windows mapping/deadzone coverage
## Disconnect/reconnect coverage
## Runtime integration
## Final Windows CI
## Remaining boundary
```

Under `TDD evidence`, list the actual canonical RED/GREEN, Windows mapping RED/GREEN, and reconnect RED/GREEN commit SHAs plus the observed failure/success for each. Under `Final Windows CI`, record the actual workflow number, run ID, exact head SHA, actual CTest pass count, and pacing/package results.

`Remaining boundary` must state that host input is now available but Burnout 3 still cannot observe it until a later PS2 PAD/SIO2/libpad adapter is implemented.

Update the `docs/PROGRESS.md` component row to:

```text
Game input | CI_VALIDATED | Keyboard + XInput host acquisition, deterministic deadzones/merge/reconnect; PS2 PAD adapter remains pending
```

Do not change graphics, audio, game initialization, menu, or gameplay statuses.

- [ ] **Step 6: Commit documentation once**

```bash
git add docs/PROGRESS.md docs/validation/2026-09-07-game-input-v0.md
git commit -m "docs: record Game Input v0 validation"
```

- [ ] **Step 7: Verify the exact documentation head**

Run/fetch Windows CI for the documentation commit and confirm all of these against the exact commit SHA:

```text
workflow status    completed
workflow conclusion success
CTest failures     0
Win32 input tests  PASS
pacing telemetry   PASS
pacing probe       PASS
package validation PASS
```

Only then report Game Input v0 as complete and `CI_VALIDATED`.

---

## Self-Review

- Spec coverage: canonical model, keyboard bindings, XInput mappings, radial deadzones, trigger thresholds, merge semantics, controller selection/reconnect, runtime frame polling, CI, and documentation are all assigned to Tasks 1–4.
- Scope check: PS2 PAD/SIO2/libpad, pressure mode, vibration, remapping, Raw Input/HID, multiple players, rendering, audio, initialization, menu, and gameplay remain excluded.
- Type consistency: `GameInputButton`, `GameInputState`, `WindowsGameInputApi`, `WindowsGameInput::poll()`, callback types, and `active_controller_` use one definition throughout the plan.
- Placeholder scan: no implementation or evidence placeholder fields remain; final documentation is explicitly populated from observed Git/GitHub values during execution.
