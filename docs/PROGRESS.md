# Progress

Status date: 2026-09-07

Completion rule: `implemented -> compiled -> executed/tested -> validated`.

This file is the active engineering snapshot. Detailed milestone history remains in Git and in dated records under `docs/validation/`.

## Current milestone

- Branch: `feature/ps2-pad-binding-discovery-v0`
- Milestone: **PS2 PAD Binding Discovery v0**
- Implementation head before documentation: `2437d2be82ae7464e0b0c2c3c9456c780e258c91`
- Implementation-head Windows CI: **#867** (`34180474186`), job `101918367155`
- CTest on #867: **72/72 PASS**
- Documentation-head validation: **PENDING**
- Game/runtime status: the project still does **not** boot Burnout 3, render the game, reach menus, produce game audio, or provide gameplay.

## Current engineering status

| Component | Status | Evidence / next gate |
|---|---|---|
| Repository / CMake bootstrap | DONE | C++20, CMake, VS2022 / Windows x64 |
| Win32 bootstrap/window | CI_VALIDATED | Window lifecycle/smoke coverage |
| QPC / 120 Hz frame pacing | CI_VALIDATED | #867 telemetry/probe still at 120 Hz target |
| Crash handler / minidump | CI_VALIDATED | Controlled Windows CI crash path |
| PS2 ELF loader | CI_VALIDATED | Strict ELF32 LE MIPS/PT_LOAD validation unchanged |
| EE main RAM / typed guest memory | CI_VALIDATED | 32 MiB RAM and typed LE accesses |
| R5900 decoder / IR / reference executor | IN_PROGRESS | Validated startup subset; broader game coverage still required |
| Windows x86-64 backend | IN_PROGRESS | Current integer/load/store/control-flow subset validated |
| Native dispatcher/cache | CI_VALIDATED | On-demand native blocks and guest-call interception |
| Host syscall HLE | IN_PROGRESS | SetupThread/SetupHeap/CreateSema validated; broader kernel coverage pending |
| External next-boundary probe | CI_VALIDATED | Real next boundary still requires a complete lawful user ELF |
| Static/binary recompiler | IN_PROGRESS | Continue from evidence-backed real boundaries |
| Game input | CI_VALIDATED | Keyboard + XInput acquisition |
| PS2 PAD report adapter v0 | CI_VALIDATED | Active-low buttons + deterministic analog bytes |
| PS2 PAD guest bridge v0 | CI_VALIDATED | Generic guest-call HLE + minimal libpad-facing service |
| PS2 PAD binding discovery v0 | CI_VALIDATION_PENDING | Implementation #867 is green; exact documentation-head CI still required |
| PS2 PAD runtime confirmation v0 | NEXT | Confirm discovered candidate PCs against live guest calls before runtime activation |
| Graphics / GS / VU | TODO | No game rendering path yet |
| IOP / SPU2 / audio | TODO | No game audio path yet |
| Game initialization | TODO | Not reached |
| Menu / gameplay | TODO | Not reached |

## PS2 PAD pipeline

The validated/reviewed input stack now contains four layers:

1. `GameInputState`: host-neutral keyboard/XInput state.
2. `Ps2PadReport`: portable PS2-style active-low buttons and four analog bytes.
3. `Ps2PadHleService`: guest-facing libpad-style lifecycle/read bridge, activated only by explicit bindings.
4. **PAD Binding Discovery v0**: analysis-only evidence that can propose or trust guest PCs without activating the bridge.

### Binding discovery behavior

`Burnout3Analyze --pad-bindings` is opt-in. Without the flag, existing analyzer output remains unchanged. With the flag, one deterministic `PAD_BINDINGS_V0` section is appended.

Evidence rules:

- exact accepted ELF symbol name + compatible symbol type + file-backed executable `PF_X PT_LOAD` PC => `Trusted`;
- trusted ELF symbol evidence has fixed diagnostic score `1000`;
- static public-semantics fingerprint => `Candidate` only;
- multiple static candidate PCs => `Candidate` with `pc=none` and all candidate evidence retained;
- multiple distinct exact-symbol PCs => `Unresolved` with `pc=none`;
- trusted symbol + conflicting static fingerprint => trusted symbol remains selected and conflict is diagnosed;
- no static score can self-promote to `Trusted`.

Accepted exact symbol spellings are only:

```text
padInit        _padInit
padPortOpen    _padPortOpen
padGetState    _padGetState
padRead        _padRead
padPortClose   _padPortClose
padEnd         _padEnd
```

The static scanner uses decoded R5900/control-flow structure plus public PS2SDK/libpad constants. It contains no Burnout-specific byte signature, proprietary function hash, copied game function body or hardcoded game address.

### Important boundary

Discovery is **not runtime confirmation**. It does not populate or activate `Ps2PadHleBindings`, and it does not prove that Burnout 3 actually calls a reported candidate. The next milestone is **PS2 PAD Runtime Confirmation v0**, which must observe evidence-backed candidate PCs in real guest execution using a complete lawful user-supplied ELF before runtime binding is considered.

## Current CI evidence

Implementation head `2437d2be82ae7464e0b0c2c3c9456c780e258c91`, Windows CI #867 (`34180474186`), job `101918367155`:

```text
Configure                         PASS
Build                             PASS
CTest                             72/72 PASS
burnout3_analyze_options_tests    PASS
burnout3_analyze_app_tests        PASS
ps2_elf_analysis_tests            PASS
r5900_analysis_report_tests       PASS
r5900_reachability_tests          PASS
Frame pacing telemetry            PASS
120 Hz pacing probe               PASS
Analyzer package validation       PASS
Pacing package validation         PASS
```

Pacing remained stable on #867:

```text
240-sample telemetry mean/P95/P99  8.333 / 8.333 / 8.333 ms
telemetry >9 / >10 / >12 ms        0 / 0 / 0
probe target / frames              120 Hz / 120
probe mean/P95/P99                 8.333 / 8.333 / 8.333 ms
probe >9 / >10 / >12 ms            0 / 0 / 0
```

Only the pre-existing MSVC C4834 `[[nodiscard]]` warnings in existing x64 tests were observed.

## TDD evidence for current milestone

```text
Task 1 metadata RED / GREEN          #842 -> #843/#844
Task 2 symbol/merge RED / GREEN      #845 -> #846/#847
Task 3 fingerprint RED / GREEN       #848 -> #849
ELF symbol score contract RED/GREEN  #861 -> #863
Task 4 report gate                   #864 PASS
Task 5 analyzer RED / GREEN          #865 -> #866
Trusted-symbol end-to-end gate       #867 PASS
```

See `docs/validation/2026-09-07-ps2-pad-binding-discovery-v0.md` for details.

## Remaining Test Build gates

1. Pass full Windows CI on the exact documentation head for this milestone.
2. Use a complete lawful user-supplied Burnout 3 ELF to obtain real PAD binding evidence.
3. Design and implement **PS2 PAD Runtime Confirmation v0** before activating any discovered candidate.
4. Continue R5900/kernel/HLE coverage from measured real boundaries.
5. Implement GS/VU rendering, IOP/SPU2/audio and remaining runtime services before any boot/playability claim.
6. Perform longer physical Windows 120 Hz release-certification captures after the runtime reaches meaningful game execution.

## Guardrails

- No proprietary Burnout 3 bytes or assets in the repository.
- No invented or hardcoded Burnout 3 PAD addresses.
- No weakening of authoritative ELF/PT_LOAD validation.
- No automatic HLE activation from static discovery.
- No static Candidate promoted to Trusted.
- No PCSX2 runtime dependency.
- Do not claim boot, rendering, audio, menus or gameplay without direct evidence.
