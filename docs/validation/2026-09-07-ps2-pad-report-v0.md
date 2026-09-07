# PS2 PAD Report Adapter v0 Validation

Status: `CI_VALIDATED`
Date: 2026-09-07
Branch: `feature/ps2-pad-report-v0`
Base: Game Input documentation head `c07a855ac9ac1b5c01dc22f85fd9eb0973623c15`
Implementation head before documentation: `37a73d1026452055ca3d9415f73636c56e7fadb0`

## Scope validated

This milestone validates only the portable conversion from `b3r::input::GameInputState` into `b3r::input::Ps2PadReport`.

Validated behavior:

- all 16 canonical buttons map to the public PS2/libpad bit positions;
- button representation is active-low: neutral `0xffff`, pressed buttons clear bits;
- simultaneous buttons combine deterministically and all 16 pressed produce `0x0000`;
- analog stick neutral is `0x80`;
- horizontal endpoints map `-1.0 -> 0x00` and `+1.0 -> 0xff`;
- vertical axes invert the canonical positive-Y/up convention;
- intermediate values are deterministically rounded after clamping;
- non-finite input returns neutral `0x80`;
- report fields preserve right-X/right-Y/left-X/left-Y ordering;
- virtual-pad connection is explicit and independent from XInput-only `gamepad_connected`;
- keyboard-originated input can therefore remain a connected virtual PS2 pad;
- an explicitly disconnected report is fully neutral and cannot expose stale input;
- L2/R2 are represented through canonical digital button bits only.

## Explicit non-claims

This milestone does not validate or implement:

- `padButtonStatus` guest-memory ABI serialization;
- `padInit`, `padPortOpen`, `padRead`, `padGetState`, or other libpad HLE;
- PADMAN RPC behavior;
- SIO2 registers or serial transactions;
- pressure-sensitive face/shoulder bytes or analog L2/R2 pressure;
- vibration/actuators;
- port/slot topology, multitap, or multiplayer;
- Burnout 3-specific addresses, hooks, signatures, or executable bytes;
- game boot, rendering, menu, audio, or gameplay.

`Ps2PadReport` is a portable intermediate representation, not a guest ABI struct.

## TDD evidence

### Digital RED

Commit:
`587389abecc8ea6f41a484af5821b59666e4b113`

Windows CI **#816** (`34162404409`) failed at Configure exactly as intended:

```text
Cannot find source file:
  src/input/ps2_pad_report.cpp
No SOURCES given to target: b3r_input
```

No production implementation existed at this gate.

### Digital GREEN

Commit:
`a6da1487354c2f6f2451f48b25540f21fac66ef0`

Windows CI **#817** (`34162532030`) succeeded with:

```text
Configure             PASS
Build                 PASS
CTest                 70/70 PASS
ps2_pad_report_tests  PASS
Frame telemetry       PASS
120 Hz probe          PASS
Analyzer package      PASS
Pacing package        PASS
```

This GREEN introduced only the portable report type and exact active-low digital mapping; stick helpers intentionally remained neutral for the next RED.

### Analog / connection RED

Commit:
`cad461d5d240d67a451bb8651de19c7258c9a3e3`

Windows CI **#818** (`34162667769`) built successfully and then produced exactly one failing test out of 70:

```text
ps2_pad_report_tests: FAIL: horizontal -1 endpoint
69/70 tests passed
```

All other tests passed. This proves the analog contract failed for the intended reason before production analog encoding was added.

### Analog / connection GREEN

Commit:
`37a73d1026452055ca3d9415f73636c56e7fadb0`

Windows CI **#819** (`34162871241`) succeeded:

```text
Configure             PASS
Build                 PASS
CTest                 70/70 PASS
ps2_pad_report_tests  PASS
windows_game_input    PASS
Frame telemetry       PASS
120 Hz probe          PASS
Analyzer package      PASS
Pacing package        PASS
```

CTest total: 70 tests, zero failures.

## Pacing regression evidence

CI #819 frame-pacer telemetry:

```text
samples               240
mean                   8.333 ms
min                    8.324 ms
max                    8.343 ms
stddev                 0.001 ms
P50/P95/P99            8.333 / 8.333 / 8.333 ms
>9 ms                  0
>10 ms                 0
>12 ms                 0
high-resolution timer  YES
```

One-second pacing probe:

```text
target                 120 Hz
frames                 120
mean                   8.333 ms
min                    8.330 ms
max                    8.333 ms
P95/P99                8.333 / 8.333 ms
>9 ms                  0
>10 ms                 0
>12 ms                 0
```

The adapter is portable and is not invoked by the frame-pacing path, but the full regression gate confirms no build/runtime timing regression was introduced.

## Final documentation-head gate

The commit containing this validation record and the corresponding `docs/PROGRESS.md` update is considered the final `CI_VALIDATED` milestone head only if the full Windows CI workflow succeeds on that exact commit SHA. No later code or documentation state inherits this validation automatically.

## Next boundary

The next input milestone is not more host acquisition or report encoding. It is a measured guest-facing bridge that consumes `Ps2PadReport` and exposes the controller through the PS2 path actually required by Burnout 3 (libpad/PADMAN/SIO2 or an evidence-backed equivalent), while keeping the portable report reusable across other PS2 titles.
