# PS2 PAD Report Adapter v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a portable, deterministic adapter from `b3r::input::GameInputState` to a PS2/DualShock-style `Ps2PadReport` with exact active-low digital buttons, byte-encoded analog sticks, and explicit virtual-pad connection state.

**Architecture:** Extend the existing `b3r_input` portable library only. `WindowsGameInput` remains the host producer; `Ps2PadReport` becomes a reusable intermediate representation for future libpad/PADMAN/SIO2 work. No guest memory, runtime, recompiler, Win32, pressure-mode, or Burnout-specific behavior is introduced.

**Tech Stack:** C++20, CMake 3.25+, MSVC/Visual Studio 2022 Windows x64 CI, CTest.

**Spec:** `docs/superpowers/specs/2026-09-07-ps2-pad-report-v0-design.md`

## Global Constraints

- Base implementation on `feature/game-input-v0` head `c07a855ac9ac1b5c01dc22f85fd9eb0973623c15` plus the approved design documents.
- Keep the adapter entirely under `src/input/`; it must not include Windows, XInput, R5900, EE-memory, RPC, PADMAN, libpad, or SIO2 headers.
- Preserve the exact PS2/libpad button bit positions from the approved spec.
- `buttons_active_low` neutral value is exactly `0xffff`; pressed buttons clear bits.
- Analog neutral is exactly `0x80`; endpoint mapping is exact; vertical axes invert host Y.
- `virtual_pad_connected` is independent from `GameInputState::gamepad_connected`.
- All encoding functions are deterministic, allocation-free, `noexcept`, and sanitize non-finite axis input to neutral.
- Do not serialize pressure bytes, guest ABI structs, rumble, ports/slots, or game-specific data.
- Every production change follows TDD RED -> GREEN and receives a reviewer gate before the next task.
- Final status may become `CI_VALIDATED` only after full Windows CI is green on the exact final documentation head.
- Do not add proprietary Burnout 3 data.

---

## File Structure

### New production files

- `src/input/ps2_pad_report.h` — public `Ps2PadButton`, `Ps2PadReport`, and encoding function declarations.
- `src/input/ps2_pad_report.cpp` — portable button/stick/report encoding implementation.

### New test file

- `tests/ps2_pad_report_tests.cpp` — exhaustive digital mapping, analog quantization, field-ordering, finite/clamp, and connection tests.

### Modified files

- `CMakeLists.txt` — add `ps2_pad_report.cpp` to `b3r_input`; add/register `ps2_pad_report_tests`.
- `docs/PROGRESS.md` — mark `PS2 PAD Report Adapter v0 = CI_VALIDATED` only after implementation CI is green.
- `docs/validation/2026-09-07-ps2-pad-report-v0.md` — record RED/GREEN/final CI evidence and scope boundaries.

---

### Task 1: Exact active-low PS2 digital report

**Files:**
- Create: `tests/ps2_pad_report_tests.cpp`
- Modify: `CMakeLists.txt`
- Create on GREEN: `src/input/ps2_pad_report.h`
- Create on GREEN: `src/input/ps2_pad_report.cpp`

**Interfaces:**
- Consumes: `b3r::input::GameInputState`, `GameInputButton`, `is_button_pressed()` from `src/input/game_input.h`.
- Produces:

```cpp
enum class Ps2PadButton : std::uint16_t;

struct Ps2PadReport {
    bool connected{};
    std::uint16_t buttons_active_low{0xffffu};
    std::uint8_t right_x{0x80u};
    std::uint8_t right_y{0x80u};
    std::uint8_t left_x{0x80u};
    std::uint8_t left_y{0x80u};
};

[[nodiscard]] Ps2PadReport encode_ps2_pad_report(
    const GameInputState& state,
    bool virtual_pad_connected = true) noexcept;
```

- [ ] **Step 1: Write the failing digital-contract test**

Create `tests/ps2_pad_report_tests.cpp` with a small `fail/expect` harness consistent with existing repository tests and this exact mapping table:

```cpp
#include "input/game_input.h"
#include "input/ps2_pad_report.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

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
} // namespace
```

Initially call only `test_default_and_digital_buttons()` from `main()` and print `ps2_pad_report_tests: PASS` on success.

- [ ] **Step 2: Register the test and missing source to force a controlled RED**

Modify the existing `b3r_input` target:

```cmake
add_library(b3r_input
  src/input/game_input.cpp
  src/input/ps2_pad_report.cpp
)
```

Inside `if(B3R_BUILD_TESTS)`, immediately after `game_input_tests`, add:

```cmake
add_executable(ps2_pad_report_tests tests/ps2_pad_report_tests.cpp)
target_link_libraries(ps2_pad_report_tests PRIVATE b3r_input)
add_test(NAME ps2_pad_report_tests COMMAND ps2_pad_report_tests)
```

Commit this RED before production implementation:

```bash
git add CMakeLists.txt tests/ps2_pad_report_tests.cpp
git commit -m "test: register PS2 PAD report contract"
```

- [ ] **Step 3: Verify RED in Windows CI**

Run the normal Windows CI for the RED commit.

Expected: Configure fails specifically because `src/input/ps2_pad_report.cpp` does not exist. Any unrelated CMake syntax or infrastructure failure is not an acceptable RED and must be corrected before production code is added.

- [ ] **Step 4: Add the minimal public header**

Create `src/input/ps2_pad_report.h`:

```cpp
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
```

- [ ] **Step 5: Implement only the digital mapping needed for Task 1**

Create `src/input/ps2_pad_report.cpp` with a private helper that clears PS2 bits for pressed canonical buttons. For Task 1, leave sticks neutral and preserve explicit connection handling:

```cpp
#include "input/ps2_pad_report.h"

#include <cmath>

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
```

- [ ] **Step 6: Verify Task 1 GREEN**

Run:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release -R "^(game_input_tests|ps2_pad_report_tests)$" --output-on-failure
```

Expected: both focused tests PASS. Then run full Windows CI; all prior 69 tests plus `ps2_pad_report_tests` must pass, for at least 70/70 total.

- [ ] **Step 7: Commit Task 1 GREEN**

```bash
git add src/input/ps2_pad_report.h src/input/ps2_pad_report.cpp
git commit -m "feat: encode PS2 PAD digital report"
```

Reviewer gate: verify exact 16-bit mapping, active-low semantics, neutral defaults, no platform dependencies, and no pressure/libpad/SIO2 behavior before Task 2.

---

### Task 2: Deterministic analog encoding and explicit connection semantics

**Files:**
- Modify: `tests/ps2_pad_report_tests.cpp`
- Modify: `src/input/ps2_pad_report.cpp`

**Interfaces:**
- Consumes the Task 1 API unchanged.
- Produces exact semantics for:

```cpp
[[nodiscard]] std::uint8_t encode_ps2_stick_axis(float axis) noexcept;
[[nodiscard]] std::uint8_t encode_ps2_stick_vertical(float axis) noexcept;
```

and completes `encode_ps2_pad_report()` by encoding `right_x`, `right_y`, `left_x`, and `left_y`.

- [ ] **Step 1: Add failing analog and connection tests**

Extend `tests/ps2_pad_report_tests.cpp` with `<cmath>` and `<limits>`, then add:

```cpp
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
```

Call both new tests from `main()`.

- [ ] **Step 2: Verify behavioral RED**

Commit only the test extension:

```bash
git add tests/ps2_pad_report_tests.cpp
git commit -m "test: define PS2 PAD analog encoding contract"
```

Run focused test or Windows CI.

Expected: `ps2_pad_report_tests` FAIL because Task 1 stick helpers return neutral `0x80` for nonzero finite axes. The failure must be in an analog/stick assertion; unrelated build failure is not an acceptable RED.

- [ ] **Step 3: Implement deterministic scalar quantization**

Replace the Task 1 placeholder helpers with:

```cpp
std::uint8_t encode_ps2_stick_axis(float axis) noexcept {
    if (!std::isfinite(axis)) {
        return 0x80u;
    }

    if (axis <= -1.0f) {
        return 0x00u;
    }
    if (axis >= 1.0f) {
        return 0xffu;
    }
    if (axis == 0.0f) {
        return 0x80u;
    }

    const float scaled = (axis + 1.0f) * 127.5f;
    return static_cast<std::uint8_t>(std::lround(scaled));
}

std::uint8_t encode_ps2_stick_vertical(float axis) noexcept {
    if (!std::isfinite(axis)) {
        return 0x80u;
    }
    return encode_ps2_stick_axis(-axis);
}
```

This gives exact endpoints/neutral, clamps through the endpoint branches, and round-to-nearest deterministic intermediate values (`-0.5 -> 0x40`, `+0.5 -> 0xbf`).

- [ ] **Step 4: Populate the four report stick fields only when connected**

Before `return report;` in the connected path of `encode_ps2_pad_report()`, add:

```cpp
report.right_x = encode_ps2_stick_axis(state.right_x);
report.right_y = encode_ps2_stick_vertical(state.right_y);
report.left_x = encode_ps2_stick_axis(state.left_x);
report.left_y = encode_ps2_stick_vertical(state.left_y);
```

Do not inspect `state.gamepad_connected`. Do not serialize triggers.

- [ ] **Step 5: Verify Task 2 GREEN and full regression**

Run:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release -R "^ps2_pad_report_tests$" --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

Expected: focused test PASS; full suite PASS with at least 70 tests. Then require the normal Windows CI to pass Configure, Build, full CTest, frame pacing telemetry, 1-second 120 Hz pacing probe, analyzer package validation, and pacing package validation.

- [ ] **Step 6: Commit Task 2 GREEN**

```bash
git add src/input/ps2_pad_report.cpp
git commit -m "feat: encode PS2 PAD analog sticks"
```

Reviewer gate: verify quantization values, finite handling, vertical inversion, report field ordering, explicit connection semantics, keyboard-only compatibility, and absence of pressure/trigger serialization.

---

### Task 3: Final validation record and progress snapshot

**Files:**
- Create: `docs/validation/2026-09-07-ps2-pad-report-v0.md`
- Modify: `docs/PROGRESS.md`

**Interfaces:**
- Consumes the fully green Task 1/2 implementation and its exact Windows CI evidence.
- Produces no code/API changes; records the validated milestone and explicit next boundary.

- [ ] **Step 1: Capture implementation-head evidence**

From the first full Windows CI run on the completed implementation head, record:

- exact commit SHA;
- workflow run number, run ID, and job ID;
- Configure/Build/Test conclusions;
- exact CTest count and `ps2_pad_report_tests` PASS;
- frame pacing mean/min/max/P95/P99 and >9/>10/>12 ms counts;
- 1-second pacing probe target, frame count, and timing summary;
- analyzer and pacing package validation conclusions.

Do not mark the component `CI_VALIDATED` yet.

- [ ] **Step 2: Add the validation document**

Create `docs/validation/2026-09-07-ps2-pad-report-v0.md` with these fixed sections and fill them only with observed CI values:

```markdown
# PS2 PAD Report Adapter v0 Validation

Status: CI_VALIDATED
Date: 2026-09-07
Branch: `feature/ps2-pad-report-v0`

## Scope

Portable `GameInputState -> Ps2PadReport` encoding only: exact active-low PS2 digital buttons, four analog-stick bytes, and explicit virtual-pad connection semantics.

## TDD evidence

Record Task 1 RED/GREEN commit + CI evidence and Task 2 RED/GREEN commit + CI evidence.

## Final contract

- all 16 PS2 button positions exact and active-low;
- neutral buttons `0xffff`;
- neutral sticks `0x80`;
- horizontal `-1/0/+1 -> 0x00/0x80/0xff`;
- vertical sign inverted;
- non-finite axes neutralized;
- virtual connection explicit and independent of XInput metadata;
- disconnected reports fully neutral;
- no pressure, guest ABI, SIO2, PADMAN, libpad HLE, or Burnout-specific hooks.

## Windows CI

Record exact implementation-head run and exact final documentation-head run.

## Remaining boundary

The next guest-visible input milestone is a separate libpad/PADMAN/SIO2 bridge selected from measured Burnout 3 behavior; this adapter does not claim guest-visible controller support.
```

The literal instruction lines beginning with `Record ...` above are a construction template only: replace them with actual evidence before committing; they must not remain in the committed validation document.

- [ ] **Step 3: Update `docs/PROGRESS.md`**

Add `PS2 PAD Report Adapter v0 = CI_VALIDATED` alongside the other validated runtime/input components. Keep the larger input/gameplay status explicit: guest-visible PS2 PAD/libpad/PADMAN/SIO2 remains pending, and menu/gameplay is not claimed.

- [ ] **Step 4: Commit documentation atomically**

```bash
git add docs/PROGRESS.md docs/validation/2026-09-07-ps2-pad-report-v0.md
git commit -m "docs: record PS2 PAD report validation"
```

- [ ] **Step 5: Run final Windows CI on the exact documentation head**

Require:

```text
Configure PASS
Build PASS
CTest 100% PASS (>= 70 tests)
ps2_pad_report_tests PASS
frame pacing telemetry PASS
120 Hz pacing probe PASS
analyzer package validation PASS
pacing package validation PASS
```

If any code/test/pacing/package gate regresses, fix it through a new RED/GREEN cycle and update the evidence. Only after this exact documentation head is green is `PS2 PAD Report Adapter v0` complete.

- [ ] **Step 6: Final verification before completion claim**

Read the final workflow job/logs and confirm the checkout SHA equals the documentation head, all non-conditional gates above passed, and any skipped upload steps were skipped only by workflow conditions. Report the final head and CI evidence without claiming libpad/SIO2/gameplay support.
