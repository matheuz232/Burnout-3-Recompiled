# PS2 PAD Guest Bridge v0 Validation

Status: `CI_VALIDATED`
Date: 2026-09-07
Branch: `feature/ps2-pad-guest-bridge-v0`
Plan head: `7d52b9a4a6391afb308e3a5ed18c666edf7d2d81`
Implementation head before documentation: `ce15accedf38a4e80c80100a9d944aad6b4aac9b`

## Scope validated

This milestone validates a reusable guest-function HLE boundary plus a minimal port-0/slot-0 PS2 PAD/libpad-compatible service that exposes the previously validated `Ps2PadReport` to EE guest memory.

Validated dispatcher behavior:

- a generic `IR5900GuestCallService` is queried before native-cache lookup and before block analysis/compilation;
- `Handled` increments only `guest_calls_handled`, resumes at low32(`$ra`) and consumes no native block or guest-instruction count;
- an intercepted handled PC does not create a dispatcher cache entry;
- `NotHandled` preserves the pre-existing dispatcher path;
- `Fault` stops as `GuestCallFailure` at the intercepted guest PC with the service message preserved;
- an explicit guest-call handler wins before analysis even when the intercepted word would otherwise be unsupported;
- the existing host `SYSCALL` service remains independent and continues to operate when the guest-call service returns `NotHandled`.

Validated PAD HLE behavior:

- guest entry points are supplied only through explicit `Ps2PadHleBindings`; zero bindings remain unbound;
- `padInit(0)` initializes and returns 1; unsupported init modes fault without mutating initialization state;
- v0 supports only port 0 / slot 0;
- `padPortOpen` requires initialization, a non-null 64-byte-aligned guest pad area and a fully backed 256-byte EE RAM region;
- failed open/re-open attempts are transactional and preserve any prior valid open state;
- `padGetState` reports `PAD_STATE_DISCONN` (`0x00`) unless the port is open and the virtual report is connected, in which case it reports `PAD_STATE_STABLE` (`0x06`);
- `padPortClose` closes the port, clears the stored pad-area address and is idempotent in v0;
- `padEnd` resets lifecycle state while the host-side report snapshot can be supplied again across reinitialization;
- low32 return values preserve the existing high64 half of `v0`.

Validated `padRead` guest-memory ABI:

- disconnected report: returns 0 and performs no destination write;
- connected report: requires initialized/open port 0/slot 0 and a fully backed 32-byte destination;
- the complete destination span is validated before any write, so a failed read cannot partially modify guest memory;
- a local 32-byte payload is built and copied once;
- byte 0 (`ok`) is `0x00` in this v0 contract;
- byte 1 (`mode`) is `0x79` for the DualShock-style analog payload contract used by this bridge;
- bytes 2-3 contain the active-low PS2 button word in little-endian order;
- bytes 4-7 contain right-X, right-Y, left-X, left-Y;
- bytes 8-31 are zero-filled; pressure/rumble semantics are not claimed;
- connected success returns 32.

The public PS2SDK `libpad.h` independently confirms the 32-byte `padButtonStatus` field order, `PAD_STATE_DISCONN = 0x00`, `PAD_STATE_STABLE = 0x06`, and the 256-byte/64-byte-aligned `padPortOpen` buffer requirement. The exact Burnout 3 guest entry addresses remain intentionally unresolved rather than guessed.

## Explicit non-claims

This milestone does not validate or implement:

- real Burnout 3 `libpad` entry-point bindings or automatic symbol/signature discovery;
- SIF RPC, PADMAN/IOP scheduling, SIO2 register/serial emulation or DMA timing;
- pressure-sensitive buttons, actuator/rumble behavior, multitap, port 1 or multiplayer;
- a live connection from the Win32 frame loop into a real Burnout 3 call path;
- game boot, rendering, audio, menus or gameplay;
- any proprietary Burnout 3 executable/data bytes in the repository.

The bridge is guest-facing in API/ABI terms, but Burnout 3 cannot be claimed to consume it until its actual guest call path is measured from a complete lawful user-supplied executable.

## TDD evidence

### Generic guest-call boundary RED

Commit: `932b9c57127524d638ca6abe72885e3e07b9ae05`

Windows CI **#828** failed at Build for the intended reason: the newly registered dispatcher test referenced `r5900_guest_call_service.h` before production implementation existed.

### Generic guest-call boundary GREEN

Production commit: `d83c18babce907caf2bbf5a39765c1bdde5f5c40`

The first GREEN run exposed a test-fixture assumption unrelated to the hook. The fixture was corrected without relaxing production semantics in commit `9605c48bb4b592a541925c5e677430695f669ec3`.

Windows CI **#830** then succeeded with **71/71 PASS**, plus pacing and package gates.

### PAD lifecycle RED

Commit: `8a343218770955a1e7efb4f3d767a348fab65ca3`

Windows CI **#831** failed at Configure exactly because `src/runtime/ps2_pad_hle_service.cpp` did not yet exist.

### PAD lifecycle GREEN

Commit: `3382cb9914fd825318efec131a6837ff879e3aeb`

Windows CI **#832** succeeded with the full test, pacing and package workflow.

### `padRead` ABI RED

Commit: `c9ebabf504903c5412f0308fed157ec3bf6c9d0d`

Windows CI **#833** built successfully and produced exactly one failure out of 72 tests:

```text
ps2_pad_hle_service_tests: FAIL: disconnected padRead must return zero successfully
71/72 tests passed
```

All other tests passed, including the dispatcher guest-call regression suite.

### `padRead` ABI GREEN

Commit: `ce15accedf38a4e80c80100a9d944aad6b4aac9b`

Windows CI **#834** (`34170912217`), job `101890883239`, succeeded:

```text
Configure                                      PASS
Build                                          PASS
CTest                                          72/72 PASS
ps2_pad_hle_service_tests                      PASS
r5900_block_dispatcher_guest_call_windows_tests PASS
Frame pacing telemetry                         PASS
120 Hz pacing probe                            PASS
Analyzer package validation                    PASS
Pacing package validation                      PASS
```

## Pacing regression evidence

CI #834 frame-pacer telemetry:

```text
samples               240
mean                   8.333 ms
min                    8.331 ms
max                    8.333 ms
stddev                 0.000 ms
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
min                    8.331 ms
max                    8.333 ms
P50/P95/P99            8.333 / 8.333 / 8.333 ms
>9 ms                  0
>10 ms                 0
>12 ms                 0
high-resolution timer  YES
```

## Final documentation-head gate

The commit containing this validation record and the corresponding `docs/PROGRESS.md` update is the final milestone head only after the complete Windows CI workflow succeeds on that exact SHA. The record intentionally cites the implementation-head CI above; no follow-up documentation edit is required merely to self-reference the final documentation-head run.

## Next boundary

The next input gate is evidence-backed binding to the guest path actually used by Burnout 3. A complete lawful user-supplied ELF should be used to identify the relevant `libpad` entry addresses or equivalent call path. Only then should the live per-frame `Ps2PadReport` snapshot be attached to `Ps2PadHleService` for real game execution. No address should be guessed.
