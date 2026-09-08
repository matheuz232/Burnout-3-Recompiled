# PS2 PAD Runtime Confirmation v0 Validation

Status: `PENDING_FINAL_EXACT_HEAD_CI`
Date: 2026-09-08
Branch: `feature/ps2-pad-runtime-confirmation-v0`
Base: `d7b9fc436805dc7e5908d277409eed208ded8f32`
Design: `docs/superpowers/specs/2026-09-08-ps2-pad-runtime-confirmation-v0-design.md`
Plan: `docs/superpowers/plans/2026-09-08-ps2-pad-runtime-confirmation-v0.md`
Implementation head before documentation: `b4ea8d1fc874d8f92e1a210bd76b441bcc60b565`
Implementation-head Windows CI: #893, run `34186019018`, job `101934385601`

## Scope validated

This milestone adds a read-only runtime-confirmation layer between static PAD binding discovery and any future runtime activation.

The implemented flow is:

```text
PadBindingDiscoveryResult
        ↓
evidence-backed guest PCs
        ↓
R5900BlockDispatcher
        ↓
IR5900CallObserver
        ↓
Ps2PadRuntimeConfirmation
        ↓
PAD_RUNTIME_CONFIRMATION_V0
```

There is intentionally no edge from runtime confirmation to `Ps2PadHleBindings` in this milestone.

## Generic R5900 call observation

`IR5900CallObserver` receives one immutable snapshot after a supported `JAL` or `JALR` has completed successfully. The observation contains call PC, actual target PC, architectural return PC, direct/indirect classification, and `$a0..$a3` low64 values after the delay slot.

The dispatcher stores call metadata with the compiled block so cold execution, ordinary cached execution and fast-cache replay use the same emission helper. The observer does not add a stop reason or counter and does not receive mutable register or memory references.

Validated negative boundaries include `J`, `JR`, failed execution and HLE interception itself: these do not create an additional call observation.

## PAD ABI confirmation

`Ps2PadRuntimeConfirmation` consumes every evidence PC associated with each canonical PAD function. It preserves static confidence and classifies observations independently per `(function, guest_pc)`.

ABI checks mirror the existing HLE-facing v0 contract using low32 argument semantics:

- `padInit`: mode 0;
- `padPortOpen`: port 0, slot 0, non-null 64-byte-aligned area, complete 256-byte EE RAM span;
- `padGetState`: port 0, slot 0;
- `padRead`: port 0, slot 0, non-null complete 32-byte EE RAM destination;
- `padPortClose`: port 0, slot 0;
- `padEnd`: no v0 argument restriction.

Observation is read-only with respect to guest RAM. Counter updates saturate at `size_t::max()`.

Runtime statuses are:

```text
Unobserved
ObservedIncompatible
RuntimeConfirmed
RuntimeAmbiguous
```

Exactly one compatible guest PC becomes `RuntimeConfirmed`. More than one compatible PC remains `RuntimeAmbiguous`; static score is never used as a runtime tie-breaker. Runtime confirmation does not promote `Candidate` to `Trusted` or otherwise mutate discovery confidence.

## Deterministic report

`format_ps2_pad_runtime_confirmation()` emits `PAD_RUNTIME_CONFIRMATION_V0` in canonical six-function order.

Each summary includes:

- static confidence;
- runtime status;
- selected PC only when status is `RuntimeConfirmed`;
- observed/compatible/incompatible counters.

Per-PC records are sorted by guest PC and use lowercase eight-digit hex. `Unobserved`, `ObservedIncompatible` and `RuntimeAmbiguous` always render `pc=none`, even if presented with an inconsistent optional PC in the input structure.

The report intentionally excludes argument dumps, guest RAM/code bytes, static scores and HLE activation fields.

## Synthetic dispatcher integration

The implementation-head test constructs only synthetic ELF/R5900 data and proves:

- a synthetic `JAL` to evidence-backed `padRead` PC with ABI-compatible arguments becomes `RuntimeConfirmed`;
- the selected runtime PC is the synthetic evidence PC;
- the caller consumes one guest block and no guest-call HLE;
- the observed 32-byte guest buffer remains byte-identical;
- a second run through fast-cache adds exactly one compatible observation;
- a partially backed 32-byte destination becomes `ObservedIncompatible` without changing guest execution;
- a `JAL` to a non-evidence target leaves `padRead` `Unobserved`;
- an unrelated guest-call service can coexist without changing PAD confirmation.

All PCs and instruction words in this integration are synthetic test fixtures.

## TDD evidence

### Task 1 — R5900 Call Observer

RED:

- commit `deb987488e9f74d077cf5742af48f3bb4605d493`
- Windows CI #879, run `34183237305`, job `101926395532`
- Configure PASS
- Build failed exactly because `recompiler/r5900_call_observer.h` did not exist.

GREEN / hardening:

- implementation commit `6ce1049d63d7ec7a5842f90108788f80cd9a7f3e`
- Windows CI #882, run `34183511828`, job `101927184634`: full workflow PASS
- hardened head `2d16fcd9b68715773601fac3cbc635919aefa2ca`
- Windows CI #884, run `34183762077`, job `101927899702`: full workflow PASS

Coverage includes JAL/JALR, post-delay `$a0..$a3`, direct/indirect metadata, J/JR negatives, cold/fast-cache, HLE-target coexistence and unchanged dispatcher counters.

### Task 2 — PAD Runtime Confirmation

RED:

- commit `367f16afc41f7f0768864f35fb8206d81ea4b0ab`
- Windows CI #885, run `34184014460`, job `101928617678`
- Build failed exactly because `analysis/ps2_pad_runtime_confirmation.h` did not exist.

Implementation:

- commit `2b03f05904c881b6d5617504ea953424a64cc684`
- Windows CI #886, run `34184175589`, job `101929082577`

Expanded matrix diagnostic:

- commit `3e8f18f6ea2a75da3da659dd414e7f3d72c57923`
- Windows CI #887, run `34184315368`, job `101929480792`
- Build failure was isolated to the test fixture `auto shared{}` type deduction under MSVC, not production behavior.

Final GREEN:

- test-only fixture correction `d6afd417493cd190f49ee2f8c2bf2b7b0e8a8e41`
- Windows CI #888, run `34184497081`, job `101929998786`
- full workflow PASS.

The final matrix covers all six ABI contracts, low32 semantics, 31/32- and 255/256-byte RAM boundaries, duplicate evidence, same numerical PC for different functions, first compatible/incompatible evidence, runtime ambiguity, score independence and saturating counters.

### Task 3 — Deterministic Runtime Report

RED:

- commit `40938dbfde2381b281a0cfab8cb51fa86042e502`
- Windows CI #889, run `34185168559`, job `101931936358`
- Build failed exactly because `analysis/ps2_pad_runtime_report.h` did not exist.

Initial GREEN:

- commit `034587ec630cba68ed43965c949c86413d90e260`
- Windows CI #890, run `34185340913`, job `101932446930`
- full workflow PASS.

Hardening RED:

- commit `6687f16a00fa5a2651386600ab53bef118479369`
- Windows CI #891, run `34185483920`, job `101932856545`
- Build PASS; CTest `72/73`; only `ps2_pad_runtime_report_tests` failed because a non-confirmed status exposed an inconsistent optional PC instead of `pc=none`.

Final GREEN:

- commit `5fc85566926cf226900c965374597d4a2fb01ffe`
- Windows CI #892, run `34185621271`, job `101933262081`
- full workflow PASS.

### Task 4 — Dispatcher → PAD Confirmation Integration

Characterization commit:

- `b4ea8d1fc874d8f92e1a210bd76b441bcc60b565`
- Windows CI #893, run `34186019018`, job `101934385601`
- full workflow PASS
- CTest `73/73 PASS`.

Because all production units were already implemented under RED→GREEN gates, Task 4 adds end-to-end characterization only; no production behavior was changed.

## Implementation-head CI evidence

Windows CI #893 (`34186019018`), job `101934385601`, exact SHA `b4ea8d1fc874d8f92e1a210bd76b441bcc60b565`:

```text
Configure                         PASS
Build                             PASS
CTest                             73/73 PASS
ps2_pad_runtime_report_tests      PASS
r5900_analysis_report_tests       PASS
r5900 direct transfer tests       PASS
r5900 indirect transfer tests     PASS
r5900 guest-call tests            PASS
Frame pacing telemetry            PASS
120 Hz pacing probe               PASS
Analyzer package validation       PASS
Pacing package validation         PASS
```

Frame pacing telemetry on #893:

```text
samples       240
mean          8.333 ms
P95           8.333 ms
P99           8.333 ms
>9/10/12 ms   0 / 0 / 0
hi-res timer  YES
```

One-second pacing probe:

```text
target        120 Hz
frames        120
mean          8.333 ms
P95           8.333 ms
P99           8.333 ms
>9/10/12 ms   0 / 0 / 0
```

The only warnings observed are the pre-existing MSVC C4834 `[[nodiscard]]` warnings in existing x64 tests.

## Build-layout deviations from the written plan

The behavioral design and test gates were preserved, but this connector environment does not provide a safe textual patch/worktree workflow for large CMake edits. To minimize risk:

- Task 1 observer coverage was added to the existing direct/indirect dispatcher Windows tests rather than a new dedicated observer executable;
- Task 2 confirmation is header-only and its portable matrix is exercised from the existing analysis-report test executable rather than a dedicated confirmation target;
- Task 3 uses a dedicated `ps2_pad_runtime_report_tests` target and the formatter is header-only;
- Task 4 characterization is hosted in the existing guest-call dispatcher Windows test rather than a separate integration executable.

These are build/test-layout differences only. The runtime architecture remains the approved observer → confirmation → report separation.

## Integrity audit

Diff from base `d7b9fc436805dc7e5908d277409eed208ded8f32` to implementation head `b4ea8d1fc874d8f92e1a210bd76b441bcc60b565` shows no change to:

- `src/runtime/ps2_pad_hle_service.h`
- `src/runtime/ps2_pad_hle_service.cpp`
- `src/platform/windows/win_main.cpp`
- `src/recompiler/ps2_elf.h`
- `src/recompiler/ps2_elf.cpp`

No Burnout 3 guest PCs were hardcoded.
No proprietary ELF/code/RAM bytes were committed.
Runtime confirmation does not create or mutate `Ps2PadHleBindings`.
`Ps2PadHleService` behavior is unchanged.
WinMain production wiring is outside this milestone.
Authoritative ELF/PT_LOAD validation is unchanged.

## Explicit non-claims

This milestone does not claim:

- that any synthetic PC is a Burnout 3 address;
- that the real Burnout 3 executable has reached any PAD call;
- that Burnout 3 currently consumes keyboard/XInput through this runtime path;
- automatic or manual runtime activation of discovered bindings;
- game boot, rendering, audio, menus or gameplay.

Real Burnout 3 runtime confirmation remains `PENDING_EXTERNAL_VALIDATION` until a complete lawful user-supplied ELF executes evidence-backed calls.

## Remaining validation gates

1. Run full Windows CI on the exact documentation commit containing this ledger and `docs/PROGRESS.md` with status `PENDING_FINAL_EXACT_HEAD_CI`.
2. If green, update only these status documents to `CI_VALIDATED` and record the successful documentation run.
3. Run full Windows CI again on that exact final status SHA.
4. Only after the second documentation-head run is completely green may this milestone be reported `CI_VALIDATED`.
