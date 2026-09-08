# PS2 PAD Runtime Activation v0 Validation

Date: 2026-09-08

Status: **CI_VALIDATED**

## Scope

Milestone: **PS2 PAD Runtime Activation v0**

Base implementation SHA:
`63f644a60d965eb32425dfa3a5b01aba6bc82712`

Design spec:
`docs/superpowers/specs/2026-09-08-ps2-pad-runtime-activation-v0-design.md`

Implementation plan:
`docs/superpowers/plans/2026-09-08-ps2-pad-runtime-activation-v0.md`

Implementation head before documentation:
`9ddeac5983579cf40d0f444cf2269d20adf44ab8`

Documentation prevalidation head:
`cb36f5886614a954fd5cb790e3172cebef5b2669`

This milestone adds a pure activation-decision layer between PAD runtime confirmation and the existing `Ps2PadHleBindings`, plus a deterministic activation report and a synthetic proof that a complete decision can drive the existing PAD HLE lifecycle.

It does not identify or activate real Burnout 3 PAD addresses.

## Design invariants validated

Per canonical PAD function, activation requires:

1. discovery slot identity matches the canonical function;
2. runtime-result slot identity matches the canonical function;
3. runtime `static_confidence` equals discovery `confidence`;
4. runtime status is `RuntimeConfirmed`;
5. selected PC exists and is nonzero;
6. the same numerical PC exists in discovery evidence for that same function.

Static confidence remains a separate dimension. `Trusted`, `Candidate`, and `Unresolved` may all be individually eligible after runtime/provenance checks pass.

Global activation is all-or-nothing:

```text
6/6 eligible + six nonzero pairwise-distinct PCs -> Ready + complete bindings
anything else                              -> NotReady + no bindings
```

No partial binding set is materialized. Duplicate selected PCs across functions block global readiness. No ordering inside `Ps2PadHleService` is used to break a tie.

## Production changes

Created:

- `src/analysis/ps2_pad_activation.h`
- `src/analysis/ps2_pad_activation_report.h`

`ps2_pad_activation.h` is header-only and pure with respect to discovery/runtime inputs and guest execution state. It does not instantiate/call `Ps2PadHleService`, access guest RAM, or mutate the dispatcher.

`ps2_pad_activation_report.h` formats decisions only; it does not change eligibility/readiness.

## Test-placement rulings

The approved plan preferred existing targets and prohibited editing the large `CMakeLists.txt`. Under the GitHub connector's no-textual-patch constraint, three fixtures were placed in already-linked targets rather than creating new targets:

- activation-policy tests: `tests/ps2_pad_runtime_report_tests.cpp`;
- activation-report tests: `tests/burnout3_analyze_options_tests.cpp`;
- Windows HLE activation characterization: `tests/r5900_block_dispatcher_createsema_windows_tests.cpp`.

These are test-layout deviations only. `CMakeLists.txt` and production dispatcher/HLE lifecycle code remain unchanged.

## TDD / CI ledger

### Task 1 — Atomic activation policy

RED:

- commit: `3e4218a09f7192e8cb39cda880496e5dbfd8a7c0`
- Windows CI: **#907**
- run: `34195094999`
- job: `101960970718`
- Configure: PASS
- Build: FAIL as required
- causal error: `C1083` for missing `analysis/ps2_pad_activation.h`

GREEN:

- commit: `bed7f05403a26bde393e21bf114767a0047ae3ef`
- Windows CI: **#908**
- run: `34195278792`
- job: `101961521951`
- full workflow: PASS

Hardening:

- commit: `6f3f31db4fc81b6483d9db4c2b43102746ca3e87`
- Windows CI: **#909**
- run: `34195522113`
- job: `101962273249`
- full workflow: PASS

Hardening covers non-confirmed statuses, missing/zero PC, all static-confidence levels, discovery/runtime identity mismatch, runtime/discovery confidence mismatch, same-function discovery provenance, duplicate PC groups, incomplete 5/6 activation, deterministic diagnostics, no partial bindings, purity and input immutability.

### Task 2 — Deterministic activation report

RED:

- commit: `de028d2215ea18b25f9b7304db54e93aa6172dae`
- Windows CI: **#910**
- run: `34195766997`
- job: `101963022087`
- Configure: PASS
- Build: FAIL as required
- causal error: `C1083` for missing `analysis/ps2_pad_activation_report.h`

GREEN:

- commit: `fcf87021cdd17f1a290de04b8965e1734464c1c8`
- Windows CI: **#911**
- run: `34195933605`
- job: `101963537024`
- full workflow: PASS

Hardening:

- commit: `cf1064b15150d5ae57367f01bfb8ecc43b351b06`
- Windows CI: **#912**
- run: `34196180662`
- job: `101964290241`
- full workflow: PASS

The formatter proves canonical six-function order, stable readiness/eligibility/reason strings, lowercase eight-digit PCs, defensive `pc=none`, binding-line suppression for inconsistent decisions, sorted/deduplicated diagnostics, and byte-identical repeated rendering.

### Task 3 — Synthetic HLE activation

Implementation/characterization head:

- commit: `9ddeac5983579cf40d0f444cf2269d20adf44ab8`
- Windows CI: **#913**
- run: `34196538933`
- job: `101965410971`
- Configure: PASS
- Build: PASS
- CTest: **73/73 PASS**
- frame pacing telemetry: PASS
- 120 Hz pacing probe: PASS
- analyzer package validation: PASS
- pacing package validation: PASS

Synthetic integration proves:

```text
six evidence-backed RuntimeConfirmed entries
        -> activation decision Ready
        -> complete Ps2PadHleBindings
        -> Ps2PadHleService constructed from those bindings
        -> fresh R5900BlockDispatcher with guest_calls=&pad_service
        -> padInit
        -> padPortOpen
        -> padGetState
        -> padRead
        -> padPortClose
        -> padEnd
```

The fixture verifies HLE return values, lifecycle state, the 32-byte PAD report, and `NotHandled` for an unbound synthetic PC. All guest PCs are synthetic.

### Documentation prevalidation

- head: `cb36f5886614a954fd5cb790e3172cebef5b2669`
- Windows CI: **#914**
- run: `34248508910`
- job: `102136599091`
- Configure: PASS
- Build: PASS
- Test: PASS
- frame pacing telemetry: PASS
- 120 Hz pacing probe: PASS
- analyzer package validation: PASS
- pacing package validation: PASS

Pacing from #913:

```text
frame telemetry: 240 samples, mean/P95/P99 8.333/8.333/8.333 ms
>9 / >10 / >12 ms: 0 / 0 / 0
high-resolution timer: YES
probe: 120 Hz / 120 frames
probe mean/P95/P99: 8.333/8.333/8.333 ms
probe >9 / >10 / >12 ms: 0 / 0 / 0
```

## Integrity audit

Compare base `63f644a60d965eb32425dfa3a5b01aba6bc82712` to implementation head `9ddeac5983579cf40d0f444cf2269d20adf44ab8`:

Changed paths are limited to:

- activation design spec;
- activation implementation plan;
- `src/analysis/ps2_pad_activation.h`;
- `src/analysis/ps2_pad_activation_report.h`;
- activation-policy/report tests;
- synthetic Windows activation characterization.

Confirmed unchanged:

- `CMakeLists.txt`;
- `src/platform/windows/win_main.cpp`;
- `src/runtime/ps2_pad_hle_service.h`;
- `src/runtime/ps2_pad_hle_service.cpp`;
- `src/recompiler/windows/r5900_block_dispatcher.h`;
- `src/recompiler/windows/r5900_block_dispatcher.cpp`;
- `src/recompiler/ps2_elf.h`;
- `src/recompiler/ps2_elf.cpp`.

No proprietary Burnout 3 data, real guest addresses, game hashes, assets, or PCSX2 runtime dependency were added. ELF/PT_LOAD validation was not weakened. Static confidence is not promoted. No dispatcher hot-swap API or partial auto-activation exists.

The documentation prevalidation diff from implementation head to `cb36f588...` contains exactly two files: this validation ledger and `docs/PROGRESS.md`.

## Known baseline warnings

Only pre-existing warning classes were observed:

- MSVC C4834 for discarded `[[nodiscard]]` results in existing x64 tests;
- GitHub Actions warning that `actions/checkout@v4` targets deprecated Node 20 and is forced to Node 24.

Neither warning was introduced by this milestone.

## Final exact-head requirement

This status-only update records the successful documentation prevalidation #914. The resulting status SHA must itself pass the full Windows workflow. Only that exact-head success authorizes reporting the milestone as complete outside the repository.

## Claim boundary

Even with the activation mechanism CI-validated:

- real Burnout 3 PAD bindings remain **PENDING_EXTERNAL_VALIDATION**;
- real Burnout 3 PAD activation/input consumption is not claimed;
- boot, rendering, audio, menus and gameplay are not claimed.
