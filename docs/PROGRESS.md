# Progress

Status date: 2026-09-08

Completion rule: `implemented -> compiled -> executed/tested -> validated`.

This file is the active engineering snapshot. Detailed milestone history remains in Git and in dated records under `docs/validation/`.

## Current milestone

- Branch: `feature/ps2-pad-runtime-confirmation-v0`
- Milestone: **PS2 PAD Runtime Confirmation v0**
- Base: `d7b9fc436805dc7e5908d277409eed208ded8f32`
- Implementation head before documentation: `b4ea8d1fc874d8f92e1a210bd76b441bcc60b565`
- Implementation-head Windows CI: **#893** (`34186019018`), job `101934385601`
- Documentation prevalidation head: `976bd6627e85f70f4da77090fa643a58d9a7a9a1`
- Documentation prevalidation Windows CI: **#898** (`34186399407`), job `101935488093`
- CTest gate: **73/73 PASS**
- Milestone status: **CI_VALIDATED**
- Next milestone: **PS2 PAD Runtime Activation**
- Real Burnout 3 PAD confirmation: **PENDING_EXTERNAL_VALIDATION** until a complete lawful user-supplied ELF reaches evidence-backed calls.
- Game/runtime status: the project still does **not** boot Burnout 3, render the game, reach menus, produce game audio, or provide gameplay.

## Current engineering status

| Component | Status | Evidence / next gate |
|---|---|---|
| Repository / CMake bootstrap | DONE | C++20, CMake, VS2022 / Windows x64 |
| Win32 bootstrap/window | CI_VALIDATED | Window lifecycle/smoke coverage |
| QPC / 120 Hz frame pacing | CI_VALIDATED | #893/#898 remain at the 120 Hz target |
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
| PS2 PAD runtime confirmation v0 | CI_VALIDATED | #893 implementation + #898 documentation prevalidation green |
| PS2 PAD runtime activation | NEXT | Must remain separate from discovery/confirmation |
| Graphics / GS / VU | TODO | No game rendering path yet |
| IOP / SPU2 / audio | TODO | No game audio path yet |
| Game initialization | TODO | Not reached |
| Menu / gameplay | TODO | Not reached |

## PS2 PAD pipeline

The current stack contains five validated layers plus one future activation boundary:

1. `GameInputState`: host-neutral keyboard/XInput state.
2. `Ps2PadReport`: portable PS2-style active-low buttons and four analog bytes.
3. `Ps2PadHleService`: guest-facing libpad-style lifecycle/read bridge, activated only by explicit bindings.
4. `PAD_BINDINGS_V0`: analysis-only static/symbol evidence for the six libpad-facing guest entry points.
5. **PAD Runtime Confirmation v0**: read-only observation of completed R5900 `JAL/JALR` calls and ABI classification against evidence-backed PCs.
6. **PAD Runtime Activation**: not implemented; must be designed separately before confirmed evidence can populate runtime bindings.

Runtime confirmation does **not** create, populate or mutate `Ps2PadHleBindings`.

### R5900 call observer

`IR5900CallObserver` receives a read-only snapshot only after a supported guest call completes successfully:

- `call_pc`;
- actual `target_pc`;
- architectural `return_pc`;
- direct/indirect flag;
- low64 snapshots of `$a0..$a3` after the delay slot.

`J`/`JR`, branches, failed blocks, syscalls and HLE interception itself do not emit call observations. Cold, cache and fast-cache paths emit one observation per completed call without changing dispatcher stop reasons or counters.

### PAD runtime confirmation

Evidence PCs are taken from `PadBindingDiscoveryResult`; no game address is invented. ABI classification uses low32 arguments and the existing 32 MiB EE RAM map:

- `padInit`: `a0 == 0`;
- `padPortOpen`: port/slot 0/0, non-null 64-byte-aligned area, full 256-byte span backed;
- `padGetState`: port/slot 0/0;
- `padRead`: port/slot 0/0, non-null full 32-byte destination backed;
- `padPortClose`: port/slot 0/0;
- `padEnd`: no v0 argument restriction.

Runtime statuses are independent of static confidence:

```text
Unobserved
ObservedIncompatible
RuntimeConfirmed
RuntimeAmbiguous
```

Exactly one compatible PC yields `RuntimeConfirmed`. Two or more compatible PCs remain `RuntimeAmbiguous`; static score never breaks a runtime tie. Counters saturate at `size_t::max()`.

### Deterministic runtime report

`PAD_RUNTIME_CONFIRMATION_V0` renders the six functions in canonical order with static confidence, runtime status, selected PC only for `RuntimeConfirmed`, and observed/compatible/incompatible counters. Per-PC evidence is sorted by guest PC. The report contains no argument dumps, RAM/code bytes, static scores or HLE activation fields.

## CI evidence

Implementation head `b4ea8d1fc874d8f92e1a210bd76b441bcc60b565`, Windows CI #893 (`34186019018`), job `101934385601`:

```text
Configure                                      PASS
Build                                          PASS
CTest                                          73/73 PASS
ps2_pad_runtime_report_tests                   PASS
r5900_analysis_report_tests                    PASS
r5900_block_dispatcher_direct_transfer_tests   PASS
r5900_block_dispatcher_indirect_transfer_tests PASS
r5900_block_dispatcher_guest_call_tests        PASS
Frame pacing telemetry                         PASS
120 Hz pacing probe                            PASS
Analyzer package validation                    PASS
Pacing package validation                      PASS
```

Documentation prevalidation head `976bd6627e85f70f4da77090fa643a58d9a7a9a1`, Windows CI #898 (`34186399407`), job `101935488093`:

```text
Configure                         PASS
Build                             PASS
CTest                             73/73 PASS
Frame pacing telemetry            PASS
120 Hz pacing probe               PASS
Analyzer package validation       PASS
Pacing package validation         PASS
```

Pacing on #893 remained stable:

```text
telemetry samples / mean / P95 / P99   240 / 8.333 / 8.333 / 8.333 ms
telemetry >9 / >10 / >12 ms            0 / 0 / 0
high-resolution timer                   YES
probe target / frames                   120 Hz / 120
probe mean / P95 / P99                  8.333 / 8.333 / 8.333 ms
probe >9 / >10 / >12 ms                0 / 0 / 0
```

Only the pre-existing MSVC C4834 `[[nodiscard]]` warnings in existing x64 tests were observed.

## TDD / characterization evidence

```text
Task 1 observer RED                 #879  missing recompiler/r5900_call_observer.h
Task 1 observer GREEN               #882  full workflow PASS
Task 1 observer hardening           #884  full workflow PASS

Task 2 PAD confirmation RED         #885  missing analysis/ps2_pad_runtime_confirmation.h
Task 2 confirmation implementation #886  implementation gate
Task 2 expanded fixture issue       #887  test-only auto{} type-deduction failure
Task 2 final GREEN                  #888  full workflow PASS

Task 3 report RED                   #889  missing analysis/ps2_pad_runtime_report.h
Task 3 report GREEN                 #890  full workflow PASS
Task 3 pc=none hardening RED        #891  72/73; only report contract failed
Task 3 final GREEN                  #892  full workflow PASS

Task 4 dispatcher integration       #893  73/73 + pacing/packages PASS
Documentation prevalidation         #898  73/73 + pacing/packages PASS
```

See `docs/validation/2026-09-08-ps2-pad-runtime-confirmation-v0.md` for exact SHAs, run IDs and scope details.

## Remaining engineering gates

1. The current status-only documentation head must pass full Windows CI before this milestone is reported complete outside the repository.
2. Obtain a complete lawful Burnout 3 ELF and observe evidence-backed real calls before making any game-specific runtime-confirmation claim.
3. Design **PS2 PAD Runtime Activation** separately before connecting confirmed evidence to `Ps2PadHleBindings`.
4. Continue R5900/kernel coverage and later GS/VU, IOP/SPU2/audio and game initialization.

## Guardrails

- No proprietary Burnout 3 bytes or assets in the repository.
- No invented or hardcoded Burnout 3 PAD addresses.
- No weakening of authoritative ELF/PT_LOAD validation.
- Runtime confirmation is read-only with respect to guest state and memory.
- Runtime confirmation does not auto-activate HLE.
- Static confidence is not rewritten by runtime confirmation.
- No PCSX2 runtime dependency.
- Do not claim real Burnout 3 input consumption, boot, rendering, audio, menus or gameplay without direct evidence.
