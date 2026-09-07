# PS2 PAD Report Adapter v0 Design

Status: approved architecture, pending implementation plan
Date: 2026-09-07
Base: `feature/game-input-v0` @ `c07a855ac9ac1b5c01dc22f85fd9eb0973623c15`
Design branch: `design/ps2-pad-report-v0`

## Goal

Add the first portable guest-facing representation of controller input: a deterministic adapter from the existing platform-neutral `b3r::input::GameInputState` to a PS2/DualShock-style PAD report.

This milestone deliberately stops before SIO2, PADMAN, libpad HLE, guest memory exposure, pressure mode, and rumble. Its purpose is to establish one reusable, tested encoding contract that later guest-facing PAD work can consume without knowing anything about Win32 or XInput.

No proprietary Burnout 3 data is required or added.

## Architectural approach

Keep the adapter inside the existing portable input layer:

```text
Windows keyboard + XInput
          |
          v
    GameInputState
          |
          v
 encode_ps2_pad_report()
          |
          v
     Ps2PadReport
          |
          v
 future libpad / PADMAN / SIO2 bridge
          |
          v
       Burnout 3
```

The adapter must not include Windows, XInput, R5900, EE-memory, RPC, or SIO2 headers. It only depends on `GameInputState` and fixed-width integer types.

This isolates three responsibilities:

1. host acquisition remains in `WindowsGameInput`;
2. host-to-PS2 input encoding lives in the portable input library;
3. connection state machines, guest ABI calls, RPC/SIO2 transport, and memory writes remain future milestones.

## Public model

Add:

```text
src/input/ps2_pad_report.h
src/input/ps2_pad_report.cpp
```

The public contract is:

```cpp
namespace b3r::input {

enum class Ps2PadButton : std::uint16_t {
    Select   = 0x0001,
    L3       = 0x0002,
    R3       = 0x0004,
    Start    = 0x0008,
    DpadUp   = 0x0010,
    DpadRight= 0x0020,
    DpadDown = 0x0040,
    DpadLeft = 0x0080,
    L2       = 0x0100,
    R2       = 0x0200,
    L1       = 0x0400,
    R1       = 0x0800,
    Triangle = 0x1000,
    Circle   = 0x2000,
    Cross    = 0x4000,
    Square   = 0x8000,
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

Names may be adjusted only for consistency during implementation; the semantics below are fixed.

## PS2 button encoding

The `Ps2PadButton` values use the conventional PS2/libpad bit positions verified against the public `ps2dev/ps2sdk` `libpad.h` definitions.

`Ps2PadReport::buttons_active_low` follows the PS2 PAD convention:

- neutral: `0xffff`;
- pressed buttons clear their corresponding bits;
- multiple pressed buttons clear the union of their bits.

Mapping from `GameInputButton` is one-to-one by semantic name:

```text
DpadUp     -> 0x0010
DpadRight  -> 0x0020
DpadDown   -> 0x0040
DpadLeft   -> 0x0080
Start      -> 0x0008
Select     -> 0x0001
Cross      -> 0x4000
Circle     -> 0x2000
Square     -> 0x8000
Triangle   -> 0x1000
L1         -> 0x0400
R1         -> 0x0800
L2         -> 0x0100
R2         -> 0x0200
L3         -> 0x0002
R3         -> 0x0004
```

The public ps2sdk sample derives pressed buttons with `0xffff ^ buttons.btns`; therefore the adapter publishes the raw active-low representation rather than a host-friendly pressed-bit mask.

## Analog-stick encoding

PS2 analog-stick bytes use `0x80` as neutral and the full `0x00..0xff` byte domain.

Horizontal axes:

```text
canonical -1.0  -> 0x00
canonical  0.0  -> 0x80
canonical +1.0  -> 0xff
```

Vertical axes invert the canonical host sign because `GameInputState` uses positive-Y as up while the PAD byte increases downward:

```text
canonical +1.0 (up)    -> 0x00
canonical  0.0         -> 0x80
canonical -1.0 (down)  -> 0xff
```

Encoding rules:

- non-finite input becomes neutral;
- values are clamped to `[-1,+1]` before quantization;
- endpoints map exactly to `0x00` and `0xff`;
- exact zero maps to `0x80`;
- intermediate values are rounded to the nearest representable byte deterministically.

`right_x/right_y/left_x/left_y` follow the same ordering as `padButtonStatus` in public ps2sdk (`rjoy_h`, `rjoy_v`, `ljoy_h`, `ljoy_v`). `Ps2PadReport` is not itself the binary `padButtonStatus` ABI and must not be `reinterpret_cast` into guest memory.

## Connection semantics

PS2 virtual-pad connection is an explicit adapter input, not inferred from `GameInputState::gamepad_connected`.

This distinction is required because `gamepad_connected` means only that an XInput controller contributed to the host sample. Keyboard-only input is a valid source for the virtual PS2 pad and must continue to work when no XInput device is attached.

Rules:

- `virtual_pad_connected == true`: publish the encoded buttons and sticks and set `report.connected = true`;
- `virtual_pad_connected == false`: return a disconnected report with `connected = false`, `buttons_active_low = 0xffff`, and every stick byte `0x80`, ignoring the supplied state.

A future PADMAN/libpad/SIO2 layer owns port/slot topology and decides when to pass `virtual_pad_connected=false`.

## Trigger and pressure behavior

`GameInputState` exposes analog trigger magnitudes, but PS2 L2/R2 are pressure-sensitive buttons rather than dedicated trigger axes in the basic PAD report.

V0 therefore:

- maps L2/R2 only from the canonical digital `GameInputButton::L2/R2` bits;
- does not serialize `left_trigger` or `right_trigger` magnitudes;
- does not synthesize pressure bytes;
- does not add `ok`, `mode`, pressure fields, actuator state, or command state to `Ps2PadReport`.

The Windows host backend already converts trigger activity past its threshold into the canonical L2/R2 digital bits, so controller input remains usable without introducing pressure-mode semantics prematurely.

Pressure support can later extend the guest-facing serializer without changing the host acquisition contract.

## Error behavior and invariants

All adapter functions are deterministic, allocation-free, and `noexcept`.

A default-constructed `Ps2PadReport` is a neutral disconnected report.

For any input:

- every output stick field is a valid byte;
- no NaN/Inf can escape the adapter;
- disconnected reports never expose stale button or stick state;
- `GameInputState` is never mutated;
- encoding has no global state and no platform calls.

## Build integration

Add `src/input/ps2_pad_report.cpp` to the existing `b3r_input` target.

Add one portable test executable:

```text
tests/ps2_pad_report_tests.cpp
```

No Windows library or executable needs to change in this milestone. Existing Windows Game Input code remains the producer of `GameInputState`; the future guest PAD milestone becomes the consumer of `Ps2PadReport`.

## Test strategy

Development uses TDD RED -> GREEN.

### Button contract

Test all 16 canonical buttons independently against their exact active-low PS2 values, plus:

- neutral `0xffff`;
- simultaneous button combinations;
- all 16 pressed -> `0x0000`.

### Stick contract

Test both horizontal and vertical helpers for:

- `-1.0`, `0.0`, `+1.0` exact endpoints/neutral;
- representative intermediate positive and negative values;
- values below `-1.0` and above `+1.0` clamp correctly;
- NaN, positive infinity, and negative infinity become `0x80`;
- vertical sign inversion is exact;
- left/right stick fields are not swapped.

### Connection contract

Test:

- connected neutral state;
- connected state with buttons/sticks;
- keyboard-originated state remains a connected virtual pad even when `gamepad_connected == false`;
- disconnected adapter input neutralizes all data even when the source state contains active buttons/sticks;
- `gamepad_connected` alone never controls `Ps2PadReport::connected`.

### Regression gate

After focused GREEN, run the full Windows CI. The entire existing suite, pacing telemetry, pacing probe, analyzer-package validation, and pacing-package validation must remain green before marking this component `CI_VALIDATED`.

## Files expected to change

New:

```text
src/input/ps2_pad_report.h
src/input/ps2_pad_report.cpp
tests/ps2_pad_report_tests.cpp
docs/validation/2026-09-07-ps2-pad-report-v0.md   (after implementation validation)
```

Modified:

```text
CMakeLists.txt
docs/PROGRESS.md                                  (after implementation validation)
```

No changes are expected in `src/platform/windows/`, `src/recompiler/`, or `src/runtime/` for v0.

## Non-goals

This milestone does not implement:

- SIO2 registers or serial transactions;
- PADMAN IOP module behavior;
- libpad RPC/HLE functions such as `padInit`, `padPortOpen`, `padRead`, or `padGetState`;
- guest memory buffers or `padButtonStatus` binary serialization;
- controller port/slot management;
- pressure-sensitive face/shoulder button bytes;
- L2/R2 analog pressure serialization;
- vibration/actuators;
- mode negotiation or `PAD_MMODE_*` behavior;
- multitap or multiple players;
- Burnout 3-specific addresses, hooks, signatures, or executable bytes;
- graphics, audio, initialization, menu, or gameplay.

## Acceptance criteria

1. A portable `Ps2PadReport` exists with explicit connection state, active-low digital buttons, and four DualShock-style stick bytes.
2. All 16 `GameInputButton` values map to the exact PS2/libpad bit positions.
3. Neutral digital state is `0xffff`; pressed buttons clear their PS2 bits.
4. Stick axes deterministically map `[-1,+1]` to `[0x00,0xff]`, exact zero to `0x80`, with vertical sign inversion and defensive finite/clamp handling.
5. Virtual-pad connection is explicit and independent from the XInput-only `gamepad_connected` metadata so keyboard fallback remains usable.
6. Disconnected output is fully neutral and cannot leak stale source input.
7. No pressure-mode, SIO2, PADMAN, libpad HLE, guest-memory ABI, or Burnout-specific behavior is introduced.
8. Focused tests cover every button, axis boundaries, non-finite values, connection behavior, field ordering, and combinations.
9. Full Windows CI, pacing, and package gates remain green on the exact final documentation head before the component is marked `CI_VALIDATED`.
10. No proprietary Burnout 3 data is added to the repository.
