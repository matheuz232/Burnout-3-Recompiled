# Progress

Status date: 2026-09-08

Completion rule: `implemented -> compiled -> executed/tested -> validated`.

This file is the active engineering snapshot. Detailed milestone history remains in Git and in dated records under `docs/validation/`.

## Current milestone

- Branch: `feature/ps2-pad-runtime-activation-v0`
- Milestone: **PS2 PAD Runtime Activation v0**
- Base: `63f644a60d965eb32425dfa3a5b01aba6bc82712`
- Implementation head before documentation: `9ddeac5983579cf40d0f444cf2269d20adf44ab8`
- Implementation-head Windows CI: **#913** (`34196538933`), job `101965410971`
- CTest gate: **73/73 PASS**
- Milestone status: **PENDING_FINAL_EXACT_HEAD_CI**
- Real Burnout 3 PAD activation: **PENDING_EXTERNAL_VALIDATION** until a complete lawful user-supplied ELF produces six evidence-backed, uniquely runtime-confirmed bindings.
- Game/runtime status: the project still does **not** boot Burnout 3, render the game, reach menus, produce game audio, or provide gameplay.

## Current engineering status

| Component | Status | Evidence / next gate |
|---|---|---|
| Repository / CMake bootstrap | DONE | C++20, CMake, VS2022 / Windows x64 |
| Win32 bootstrap/window | CI_VALIDATED | Window lifecycle/smoke coverage |
| QPC / 120 Hz frame pacing | CI_VALIDATED | #913 remains at the 120 Hz target |
| Crash handler / minidump | CI_VALIDATED | Controlled Windows CI crash path |
| PS2 ELF loader | CI_VALIDATED | Strict ELF32 LE MIPS/PT_LOAD validation unchanged |
| EE main RAM / typed guest memory | CI_VALIDATED | 32 MiB RAM and typed LE accesses |
| R5900 decoder / IR / reference executor | IN_PROGRESS | Validated startup subset; broader game coverage still required |
| Windows x86-64 backend | IN_PROGRESS | Current integer/load/store/control-flow subset validated |
| Native dispatcher/cache | CI_VALIDATED | On-demand native blocks, guest-call interception and read-only call observation |
| Host syscall HLE | IN_PROGRESS | SetupThread/SetupHeap/CreateSema validated; broader kernel coverage pending |
| External next-boundary probe | CI_VALIDATED | Real next boundary still requires a complete lawful user ELF |
| Static/binary recompiler | IN_PROGRESS | Continue from evidence-backed real boundaries |
| Game input | CI_VALIDATED | Keyboard + XInput acquisition |
| PS2 PAD report adapter v0 | CI_VALIDATED | Active-low buttons + deterministic analog bytes |
| PS2 PAD guest bridge v0 | CI_VALIDATED | Generic guest-call HLE + minimal libpad-facing service |
| PS2 PAD binding discovery v0 | CI_VALIDATED | Analysis-only trusted/candidate guest-PC evidence |
| PS2 PAD runtime confirmation v0 | CI_VALIDATED | Read-only completed-call observation + ABI confirmation |
| PS2 PAD runtime activation v0 | PENDING_FINAL_EXACT_HEAD_CI | #913 implementation head green; documentation/final exact-head gates remain |
| Graphics / GS / VU | TODO | No game rendering path yet |
| IOP / SPU2 / audio | TODO | No game audio path yet |
| Game initialization | TODO | Not reached |
| Menu / gameplay | TODO | Not reached |

## PS2 PAD pipeline

The validated/implemented architecture is now:

1. `GameInputState`: host-neutral keyboard/XInput state.
2. `Ps2PadReport`: portable PS2-style active-low buttons and four analog bytes.
3. `Ps2PadHleService`: guest-facing libpad-style lifecycle/read bridge, activated only by explicit bindings.
4. `PAD_BINDINGS_V0`: analysis-only static/symbol evidence for the six libpad-facing guest entry points.
5. `PAD_RUNTIME_CONFIRMATION_V0`: read-only observation of completed R5900 `JAL/JALR` calls and ABI classification against evidence-backed PCs.
6. `PAD_ACTIVATION_V0`: pure policy that materializes `Ps2PadHleBindings` only when all six functions are independently eligible and globally safe.

Runtime activation does not rewrite discovery confidence and does not hot-swap a dispatcher during `run()`.

### Atomic activation policy

`make_ps2_pad_activation_decision(discovery, runtime)` is a pure snapshot transform. For each canonical function it requires:

- discovery slot identity matches the canonical function;
- runtime slot identity matches the canonical function;
- runtime `static_confidence` matches discovery `confidence`;
- runtime status is `RuntimeConfirmed`;
- selected guest PC exists and is nonzero;
- the exact selected PC exists in discovery evidence for the same function.

`Trusted`, `Candidate`, and `Unresolved` confidence remain distinct and can all be activation-eligible after the runtime/provenance checks pass.

Global readiness is atomic:

```text
6/6 eligible
AND all six PCs nonzero
AND all six PCs pairwise distinct
    -> Ready + complete Ps2PadHleBindings

otherwise
    -> NotReady + no bindings
```

Duplicate numerical PCs across functions never become bindings. Fewer than six eligible functions never produce a partial binding set.

### Deterministic activation report

`PAD_ACTIVATION_V0` renders:

- readiness and eligible/required counts;
- exactly six canonical function lines;
- static confidence, runtime status, eligibility and reason;
- a PC only for an eligible function;
- complete bindings only for an internally consistent `Ready` decision;
- sorted/deduplicated diagnostics;
- lowercase eight-digit hexadecimal PCs.

The report contains no argument dumps, RAM/code bytes, static scores, proprietary game data, or invented Burnout addresses.

### Synthetic HLE activation proof

The implementation gate uses six synthetic evidence-backed PCs only. A `Ready` decision is used to construct `Ps2PadHleService`, then a fresh dispatcher with `guest_calls = &pad_service` executes the existing HLE lifecycle:

```text
padInit -> padPortOpen -> padGetState -> padRead -> padPortClose -> padEnd
```

The test verifies lifecycle return values, the 32-byte PAD report, and that an unbound synthetic PC remains `NotHandled`. No production dispatcher hot-swap API or `WinMain` game loop was added.

## CI evidence

Implementation head `9ddeac5983579cf40d0f444cf2269d20adf44ab8`, Windows CI #913 (`34196538933`), job `101965410971`:

```text
Configure                         PASS
Build                             PASS
CTest                             73/73 PASS
ps2_pad_runtime_report_tests      PASS
burnout3_analyze_options_tests    PASS
r5900_block_dispatcher_createsema_windows_tests PASS
Frame pacing telemetry            PASS
120 Hz pacing probe               PASS
Analyzer package validation       PASS
Pacing package validation         PASS
```

Pacing on #913:

```text
telemetry samples / mean / P95 / P99   240 / 8.333 / 8.333 / 8.333 ms
telemetry >9 / >10 / >12 ms            0 / 0 / 0
high-resolution timer                   YES
probe target / frames                   120 Hz / 120
probe mean / P95 / P99                  8.333 / 8.333 / 8.333 ms
probe >9 / >10 / >12 ms                0 / 0 / 0
```

Only the pre-existing MSVC C4834 `[[nodiscard]]` warnings in existing x64 tests and the existing Node 20 deprecation warning from GitHub Actions were observed.

## TDD / characterization evidence

```text
Task 1 policy RED                    #907  missing analysis/ps2_pad_activation.h
Task 1 policy GREEN                  #908  full workflow PASS
Task 1 policy hardening              #909  full workflow PASS

Task 2 activation report RED         #910  missing analysis/ps2_pad_activation_report.h
Task 2 activation report GREEN       #911  full workflow PASS
Task 2 report hardening              #912  full workflow PASS

Task 3 synthetic HLE activation      #913  73/73 + pacing/packages PASS
```

Build-layout rulings: to keep `CMakeLists.txt` unchanged under the connector constraints, policy tests were hosted in `ps2_pad_runtime_report_tests`, activation-report tests in `burnout3_analyze_options_tests`, and the Windows HLE characterization in `r5900_block_dispatcher_createsema_windows_tests`. These are test-placement deviations only; production architecture is unchanged.

See `docs/validation/2026-09-08-ps2-pad-runtime-activation-v0.md` for exact SHAs, run/job IDs, scope and integrity details.

## Remaining engineering gates

1. Run full Windows CI on the exact documentation SHA.
2. If green, update only `docs/PROGRESS.md` and the validation ledger to `CI_VALIDATED` and run full Windows CI again on that exact status SHA.
3. Obtain a complete lawful Burnout 3 ELF and real evidence-backed PAD call observations before any game-specific activation claim.
4. Continue broader R5900/kernel coverage and later GS/VU, IOP/SPU2/audio and game initialization.

## Guardrails

- No proprietary Burnout 3 bytes, assets or hashes in the repository.
- No invented or hardcoded Burnout 3 PAD addresses.
- No weakening of authoritative ELF/PT_LOAD validation.
- Static confidence is not promoted by runtime confirmation or activation.
- Runtime activation is atomic; no partial auto-binding.
- Duplicate guest PCs never become active bindings.
- No dispatcher guest-call hot-swap API was added.
- `WinMain`, `Ps2PadHleService`, dispatcher production code, CMake and ELF loader remain unchanged by this milestone.
- No PCSX2 runtime dependency.
- Do not claim real Burnout 3 input consumption, boot, rendering, audio, menus or gameplay without direct evidence.
