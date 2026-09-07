# Game Input v0 Validation

Status date: 2026-09-07
Branch: `feature/game-input-v0`
Implementation head: `9cb0f61bd8bf6eda27905a3ae7db6024bb795c47`

## Scope

Game Input v0 adds deterministic Windows host input acquisition without exposing input to guest PS2 state yet.

Delivered scope:

- platform-neutral `b3r::input::GameInputState`;
- canonical digital buttons, two sticks, two triggers and gamepad-connected metadata;
- Windows keyboard acquisition through `GetAsyncKeyState`;
- XInput acquisition through `XInputGetState`;
- injectable Win32/XInput callbacks for hardware-independent CI tests;
- radial stick deadzones and deterministic trigger normalization;
- keyboard + XInput merge semantics;
- active-controller reuse plus disconnect/fallback/reconnect discovery;
- one immutable host-input sample per runtime frame before simulation.

Explicitly excluded from this milestone: PS2 PAD/SIO2/libpad protocol, pressure mode, vibration, remapping UI, Raw Input/HID/DirectInput/SDL, multiple players and guest-visible game input.

## TDD evidence

Canonical portable state:

```text
RED   19fd00c6505df6d30f65738994263e59b34f8573
      Windows CI #802 / run 34158237405
      Configure failed as intended because src/input/game_input.cpp was absent.

GREEN 187590ed34e42f8ecc01242bbf6ee487830c96db
      Windows CI #803 / run 34158327198
      Build + CTest + pacing/package gates passed; 68/68 CTest PASS.
```

Windows keyboard/XInput mapping:

```text
RED   6f8c942b7f1208c65c6987edf4a18f0fb45eb94c
      Windows CI #805 / run 34158609034
      Configure failed as intended because src/platform/windows/windows_game_input.cpp was absent.

GREEN 3d98ff2a0542ba1f402e032918bdb6e7285c4cd9
      Windows CI #806 / run 34158786963
      69/69 CTest PASS; pacing/package gates PASS.
```

Controller selection/reconnect:

```text
RED   306ec80d37897a48c3a326eb0774ca6035450ecf
      Windows CI #807 / run 34159006298
      Build passed; 68/69 CTest PASS.
      Only failure: windows_game_input_tests: active controller reuse: trace size mismatch.

GREEN 8c8ef999c457047aa3222fa85f6af89d730ff25f
      Windows CI #808 / run 34159165347
      69/69 CTest PASS; pacing/package gates PASS.
```

Runtime integration:

```text
GREEN 9cb0f61bd8bf6eda27905a3ae7db6024bb795c47
      Windows CI #809 / run 34159434149
      69/69 CTest PASS; all workflow gates PASS.
```

## Windows mapping/deadzone coverage

XInput digital mapping is validated for:

- A -> Cross;
- B -> Circle;
- X -> Square;
- Y -> Triangle;
- D-pad directions;
- Start / Back -> Start / Select;
- shoulders -> L1 / R1;
- thumb clicks -> L3 / R3;
- analog triggers -> L2 / R2 once strictly above `XINPUT_GAMEPAD_TRIGGER_THRESHOLD`.

Keyboard mapping is validated for:

- arrows -> D-pad;
- W/A/S/D -> left stick;
- Enter / Backspace -> Start / Select;
- X/C/Z/V -> Cross/Circle/Square/Triangle;
- Q/E -> L1/R1;
- 1/3 -> L2/R2 with trigger value 1.0.

Stick behavior uses radial XInput deadzones. Magnitude at or below the native deadzone produces exact zero; values outside the deadzone are rescaled and clamped to [-1, 1]. Trigger values at or below the native threshold produce zero; full-scale 255 produces 1.0. Keyboard opposite directions cancel exactly.

Merge behavior is deterministic:

```text
buttons            keyboard OR gamepad
left stick         nonzero keyboard axis overrides matching gamepad axis
right stick        gamepad only
triggers           max(keyboard, gamepad)
gamepad_connected  gamepad source only
```

Null injected APIs are treated as unavailable and return neutral state rather than failing.

## Disconnect/reconnect coverage

The Windows backend retains an active XInput controller index between frames:

- first acquisition scans indices in ascending order and stops at the first connected device;
- subsequent frames poll the active controller first and stop on success;
- if the active controller fails, that index is not polled twice in the same frame;
- fallback scans the remaining indices in deterministic ascending order;
- all disconnected yields neutral gamepad state with `gamepad_connected=false`;
- a later connection is discoverable on the next poll;
- arbitrary non-success XInput results are treated as disconnected/non-fatal.

Tests validate both returned state and XInput call order/counts, preventing a full-scan implementation from falsely satisfying active-controller reuse.

## Runtime integration

`Burnout3Recompiled_Test.exe` owns one `WindowsGameInput` instance. Inside the existing 120 Hz loop it calls `game_input.poll()` exactly once after `frame_begin` and before the simulation section.

The sample is intentionally unused by guest runtime code in v0. There is no per-frame input logging and no PS2 PAD emulation in this milestone.

## Final Windows CI

Implementation head validation:

```text
Workflow           Windows CI #809
Run ID             34159434149
Exact head         9cb0f61bd8bf6eda27905a3ae7db6024bb795c47
Host               Windows Server 2022
Generator          Visual Studio 17 2022 x64
Compiler           MSVC 19.44
Configure          PASS
Build              PASS
CTest              69/69 PASS
windows_game_input PASS
Frame telemetry    PASS
120 Hz probe       PASS
Analyzer package   PASS
Pacing package     PASS
```

Frame pacing telemetry on #809:

```text
samples   240
mean      8.333 ms
min       8.285 ms
max       8.382 ms
stddev    0.005 ms
P95       8.333 ms
P99       8.334 ms
>9 ms     0
>10 ms    0
>12 ms    0
high-res  YES
```

120 Hz pacing probe on #809:

```text
frames    120
mean      8.333 ms
min       8.330 ms
max       8.333 ms
stddev    0.000 ms
P95       8.333 ms
P99       8.333 ms
>9 ms     0
>10 ms    0
>12 ms    0
high-res  YES
```

## Remaining boundary

Host input acquisition is now available and CI-validated, but Burnout 3 cannot observe these controls yet. The next guest-facing input milestone must implement a PS2 PAD/SIO2/libpad adapter (or equivalent verified game-facing boundary) that converts `GameInputState` into the protocol/state the game consumes.

This validation does not imply game boot, menus, rendering, audio or gameplay. No proprietary Burnout 3 data was added.