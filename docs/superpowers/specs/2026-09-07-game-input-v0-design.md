# Game Input v0 Design

Status: approved architecture, pending implementation plan
Date: 2026-09-07
Base: `feature/win32-window-validation-v0` @ `cb9ec4a0685271dfa0fd32837cd137a9f496975f`
Design branch: `design/game-input-v0`

## Goal

Add the first reusable host-input subsystem for the Windows x64 Burnout 3 runtime without pretending that PS2 PAD emulation already exists.

The milestone acquires keyboard and XInput controller state once per frame, normalizes it into a platform-neutral `GameInputState`, and integrates polling into the existing Win32 runtime loop. The resulting state is intentionally suitable for a later DualShock 2 / PS2 PAD adapter, but this milestone does not expose guest PAD registers, SIO2, libpad HLE, pressure-mode protocol, or game-visible controller data.

## Architectural approach

Use two layers:

1. `b3r::input` owns the canonical host-game-input model and platform-neutral helpers.
2. `b3r::platform::windows::WindowsGameInput` owns Win32/XInput acquisition and converts host state into the canonical model.

The canonical layer must not include `<windows.h>` or `<Xinput.h>`. Windows-specific headers and APIs remain confined to `src/platform/windows/`.

The runtime flow is:

```text
keyboard GetAsyncKeyState + XInputGetState
                  |
                  v
        WindowsGameInput::poll()
                  |
                  v
            GameInputState
                  |
                  v
      future PS2 PAD adapter/HLE
                  |
                  v
              Burnout 3
```

No third-party input library is introduced.

## Canonical input model

Add `src/input/game_input.h` and `src/input/game_input.cpp`.

`GameInputState` contains:

- a digital button bitmask;
- left stick `x/y`, each normalized to `[-1.0, +1.0]`;
- right stick `x/y`, each normalized to `[-1.0, +1.0]`;
- left trigger normalized to `[0.0, 1.0]`;
- right trigger normalized to `[0.0, 1.0]`;
- `gamepad_connected`, indicating whether an XInput controller contributed to the current sample.

Digital buttons are named by PS2-style control intent so the later PAD adapter has a direct semantic source:

- D-pad Up/Down/Left/Right;
- Start, Select;
- Cross, Circle, Square, Triangle;
- L1, R1, L2, R2;
- L3, R3.

The canonical model is host-facing, not guest-ABI-facing. In v0 it does not model DualShock 2 pressure-sensitive face/shoulder bytes. A later PAD adapter may synthesize full pressure for digital buttons and/or extend the host model if measured game behavior requires analog pressure.

## State invariants

A default-constructed `GameInputState` is fully neutral:

- no buttons pressed;
- all stick axes `0.0`;
- both triggers `0.0`;
- `gamepad_connected == false`.

Every published axis must be finite and clamped to its documented range. Opposite keyboard directions on the same axis cancel to zero.

The canonical layer provides small helpers for button tests, clamping/normalization, and deterministic state merge. It contains no polling and no global mutable state.

## Windows acquisition API

Add:

```text
src/platform/windows/windows_game_input.h
src/platform/windows/windows_game_input.cpp
```

`WindowsGameInput` exposes:

```cpp
GameInputState poll() noexcept;
```

For deterministic CI tests, the class receives a small injectable `WindowsGameInputApi` containing function pointers compatible with:

- `XInputGetState`;
- `GetAsyncKeyState`.

The default constructor uses the real Windows APIs. Tests can provide fake callbacks without controller hardware or desktop keyboard input.

The backend keeps at most one active XInput controller. It polls the previously active controller first; if that controller is disconnected, it scans controller indices `0..XUSER_MAX_COUNT-1` and adopts the first connected device. If no controller is connected, polling still returns keyboard state and `gamepad_connected == false`. Reconnection is therefore detected on a later poll without reconstructing the input object.

## XInput mapping

Map XInput buttons to the canonical PS2-style names by physical/semantic position:

- A -> Cross;
- B -> Circle;
- X -> Square;
- Y -> Triangle;
- D-pad -> matching D-pad buttons;
- START -> Start;
- BACK -> Select;
- LEFT_SHOULDER -> L1;
- RIGHT_SHOULDER -> R1;
- LEFT_THUMB -> L3;
- RIGHT_THUMB -> R3.

Triggers are normalized to `[0,1]`; after the trigger threshold they also set the corresponding digital L2/R2 button.

Sticks use radial deadzone removal with the standard XInput deadzone constants. Values inside the deadzone produce exact zero. Values outside the deadzone are direction-preserving, remapped across the remaining magnitude range, and clamped to `[-1,+1]`.

Trigger normalization uses the standard XInput trigger threshold. Values at or below the threshold produce exact zero; larger values are linearly remapped to `[0,1]` and clamped.

## Keyboard fallback mapping

Keyboard input is polled every frame through `GetAsyncKeyState` high-bit state. V0 uses fixed development bindings:

- Arrow keys -> D-pad;
- W/A/S/D -> left stick Up/Left/Down/Right;
- Enter -> Start;
- Backspace -> Select;
- X -> Cross;
- C -> Circle;
- Z -> Square;
- V -> Triangle;
- Q -> L1;
- E -> R1;
- `1` -> L2 and left trigger `1.0`;
- `3` -> R2 and right trigger `1.0`.

Keyboard v0 does not emulate a right analog stick. That is a non-goal for this milestone and can be added later without changing the canonical state ABI.

## Keyboard + gamepad merge

Polling produces one keyboard state and one gamepad state, then merges them deterministically:

- digital buttons are bitwise OR;
- for each left-stick axis, a nonzero keyboard axis overrides the gamepad axis; otherwise the gamepad axis is retained;
- right-stick axes come from gamepad in v0;
- each trigger is the maximum of keyboard and gamepad values;
- `gamepad_connected` reflects XInput connection only, not keyboard activity.

This permits simultaneous keyboard/gamepad use while ensuring a keyboard steering command is exact and predictable.

## Runtime integration

Add the portable input implementation to a dedicated `b3r_input` library, linked by `b3r_windows_runtime`.

Add `windows_game_input.cpp` to `b3r_windows_runtime` and link the Windows XInput import library.

In `win_main.cpp`, construct one `WindowsGameInput` after the window is created. Call `poll()` exactly once near the start of each frame, before the placeholder simulation section. The resulting state is retained as the current frame's immutable input sample.

Because game simulation and PS2 PAD integration are not yet implemented, the bootstrap does not claim that controller input affects Burnout 3. The poll integration exists to make the next PAD milestone a consumer rather than another host-platform redesign.

## Error and disconnect behavior

Input polling is non-fatal and `noexcept`.

- XInput `ERROR_SUCCESS`: consume the controller state.
- XInput device-not-connected or other failure: treat that controller sample as disconnected/neutral and continue scanning as appropriate.
- no connected XInput devices: return keyboard-only state.
- keyboard API returns no active high bit: key is not pressed.

No input acquisition failure may terminate the runtime loop.

## Test strategy

Development uses TDD RED -> GREEN.

### Portable canonical tests

Cover:

- default neutral state;
- button set/test behavior;
- clamping of stick and trigger ranges;
- deterministic button OR merge;
- keyboard-axis override semantics;
- trigger max semantics;
- neutral/opposite-axis cancellation where represented by canonical helpers.

### Windows backend tests with fake APIs

Use injected fake `XInputGetState` and `GetAsyncKeyState` callbacks to cover without hardware:

- no controller + no keys -> neutral state;
- every supported XInput digital mapping;
- left/right stick deadzone exact zero;
- stick sign/direction, saturation, and range;
- trigger threshold, normalization, saturation, and L2/R2 digital flags;
- keyboard mappings;
- simultaneous keyboard + XInput button fusion;
- keyboard left-stick override;
- gamepad right-stick retention;
- first-controller selection;
- active-controller reuse;
- disconnect and fallback to another controller;
- all-disconnected transition;
- reconnect on a later poll.

### Runtime/build regression

Existing Win32 window, QPC/pacing, crash-handler, recompiler, dispatcher, analyzer, and packaging tests remain mandatory. Windows CI must build and execute the complete CTest suite and existing pacing/package gates.

## Files expected to change

New:

```text
src/input/game_input.h
src/input/game_input.cpp
src/platform/windows/windows_game_input.h
src/platform/windows/windows_game_input.cpp
tests/game_input_tests.cpp
tests/windows_game_input_tests.cpp
```

Modified:

```text
CMakeLists.txt
src/platform/windows/win_main.cpp
docs/PROGRESS.md                     (after implementation validation)
docs/validation/<dated-input-record> (after implementation validation)
```

## Non-goals

This milestone does not implement:

- PS2 SIO2/PAD protocol;
- libpad HLE or game-visible PAD memory/registers;
- DualShock 2 pressure mode;
- vibration/force feedback;
- controller remapping UI or persistent bindings;
- Raw Input, DirectInput, HID, SDL, or DualSense-specific APIs;
- hot-plug notifications beyond polling/reconnect discovery;
- multiple simultaneous players;
- keyboard right-stick emulation;
- graphics, audio, game initialization, menu, or gameplay.

## Acceptance criteria

1. A platform-neutral canonical `GameInputState` exists with deterministic neutral defaults, PS2-style digital button intents, two sticks, two triggers, and XInput connection metadata.
2. The Windows backend polls keyboard and XInput through an injectable API and never requires physical controller hardware in CI.
3. XInput mappings, radial stick deadzones, trigger normalization, disconnect/reconnect, controller selection, and keyboard/gamepad merge semantics are covered by deterministic tests.
4. Keyboard fallback provides the documented fixed development bindings.
5. The Win32 runtime polls exactly once per frame before the simulation section without claiming guest-visible PAD behavior.
6. Existing Windows runtime/recompiler/dispatcher/pacing/package regressions remain green.
7. Final Windows CI is green on the exact documentation head before the component is marked `CI_VALIDATED`.
8. No proprietary Burnout 3 data is added to the repository.
