# PS2 PAD Binding Discovery v0 Validation

Status: `CI_VALIDATED`
Date: 2026-09-07
Branch: `feature/ps2-pad-binding-discovery-v0`
Design: `docs/superpowers/specs/2026-09-07-ps2-pad-binding-discovery-v0-design.md`
Plan: `docs/superpowers/plans/2026-09-07-ps2-pad-binding-discovery-v0.md`
Implementation head before documentation: `2437d2be82ae7464e0b0c2c3c9456c780e258c91`
Implementation-head Windows CI: #867, run `34180474186`, job `101918367155`
Documentation prevalidation head: `35517939d845df23008f00e718e27fba368e5709`
Documentation-head Windows CI: #871, run `34180765161`, job `101919246799`

## Scope validated

This milestone adds analysis-only discovery of the six PS2 libpad-facing guest binding PCs required by the existing `Ps2PadHleService`:

- `padInit`
- `padPortOpen`
- `padGetState`
- `padRead`
- `padPortClose`
- `padEnd`

Discovery does not activate HLE and does not change `Ps2PadHleService` runtime bindings.

### Optional ELF metadata

A separate analysis-only ELF32 metadata parser reads section headers, string tables, `SHT_SYMTAB` and `SHT_DYNSYM` only after the existing PS2 ELF loader has accepted the file. It distinguishes `Available`, `Absent` and `Malformed` metadata and uses checked range arithmetic. Optional metadata cannot make an invalid ELF loadable and does not weaken PT_LOAD validation.

### Exact symbol evidence

Only the canonical libpad names and their one-leading-underscore aliases are accepted. Matching is exact and case-sensitive. Eligible symbols must have a nonzero guest value, a compatible function/NOTYPE type and lie in file-backed executable `PF_X PT_LOAD` memory.

One unique exact symbol PC becomes `Trusted`. ELF symbol evidence uses the fixed diagnostic score `1000`. Multiple distinct exact-symbol PCs remain `Unresolved` with no selected PC.

### Static fingerprint evidence

The static scanner operates on existing decoded R5900 reachability/control-flow data. It uses only public PS2SDK/libpad semantic constants and synthetic test fixtures. It contains no Burnout-specific instruction sequence, function hash, proprietary bytes or hardcoded game PC.

Static fingerprints are always `Candidate` at most. A score can never promote static evidence to `Trusted`. Multiple static PCs remain candidate evidence with `guest_pc = nullopt` and report `pc=none`.

### Deterministic report

`Burnout3Analyze --pad-bindings` appends one deterministic `PAD_BINDINGS_V0` section to the normal report. Without the flag, the previous analyzer output remains unchanged.

The report provides canonical function order; `trusted`, `candidate`, or `unresolved` confidence; lowercase eight-digit selected PCs or `pc=none`; evidence kind/PC/score/detail; deterministic evidence and diagnostic ordering; and explicit ambiguity rather than a guessed winner.

A synthetic end-to-end ELF with an executable exact `padInit` symbol is validated as:

```text
PAD_BINDING function=padInit confidence=trusted pc=0x00100000 evidence_count=1 max_score=1000
PAD_BINDING_EVIDENCE function=padInit kind=elf_symbol pc=0x00100000 score=1000 detail=padInit
```

All addresses in tests are synthetic fixture addresses, not Burnout 3 addresses.

## TDD evidence

```text
Task 1 metadata RED       CI #842  missing analysis/elf32_metadata.h
Task 1 metadata GREEN     CI #843  full workflow PASS
Task 1 hardening          CI #844  full workflow PASS

Task 2 symbol/merge RED   CI #845  missing PAD binding discovery production API
Task 2 symbol/merge GREEN CI #846  full workflow PASS
Task 2 hardening          CI #847  full workflow PASS

Task 3 fingerprint RED    CI #848  missing analysis/ps2_pad_fingerprint.h
Task 3 fingerprint GREEN  CI #849  full workflow PASS

Symbol-score contract RED CI #861  71/72; only ps2_elf_analysis_tests failed because ELF symbol score was not 1000
Symbol-score GREEN        CI #863  full workflow PASS
Task 4 report gate        CI #864  full workflow PASS

Task 5 analyzer RED       CI #865  71/72; only burnout3_analyze_app_tests failed because --pad-bindings section was absent
Task 5 analyzer GREEN     CI #866  full workflow PASS
Trusted-symbol gate       CI #867  full workflow PASS
Documentation gate        CI #871  full workflow PASS
```

Intermediate CI #859 is intentionally not counted as a valid RED because it failed during Build from an inconsistent concurrent branch state before reaching the intended assertion.

## Implementation-head CI evidence

Windows CI #867 (`34180474186`), job `101918367155`, exact SHA `2437d2be82ae7464e0b0c2c3c9456c780e258c91`:

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

Frame pacing telemetry on #867 remained at the 120 Hz target with 240 samples, mean/P95/P99 `8.333 / 8.333 / 8.333 ms`, zero samples above 9/10/12 ms and high-resolution timer enabled. The one-second probe likewise reported 120 frames at the 120 Hz target with no >9/10/12 ms outliers.

## Documentation-head CI evidence

Windows CI #871 (`34180765161`), job `101919246799`, exact SHA `35517939d845df23008f00e718e27fba368e5709`:

```text
Configure                         PASS
Build                             PASS
Test                              PASS
Frame pacing telemetry            PASS
120 Hz pacing probe               PASS
Analyzer package validation       PASS
Pacing package validation         PASS
```

This validates the production implementation together with the analyzer/progress/validation documentation before the final status-only documentation update.

The only compiler warnings observed throughout the milestone are the pre-existing MSVC C4834 `[[nodiscard]]` warnings in existing x64 tests. No new warning class was introduced by this milestone.

## Final audit

- no proprietary Burnout 3 bytes or assets were added;
- no Burnout-specific guest PC was invented or hardcoded;
- authoritative ELF/PT_LOAD validation was not weakened;
- `Ps2PadHleService` is not auto-activated;
- static `Candidate` evidence is never promoted to `Trusted` by score;
- default analyzer output remains unchanged without `--pad-bindings`;
- ambiguous evidence never chooses an arbitrary winner;
- complete Windows Configure/Build/Test/pacing/package gates passed on the documentation head.

## Explicit non-claims

This milestone does not claim:

- that the real Burnout 3 executable contains any particular PAD address;
- that a static candidate is a confirmed runtime entry point;
- automatic `Ps2PadHleBindings` activation;
- live per-frame Burnout 3 input consumption;
- SIF/PADMAN/IOP/SIO2 emulation;
- game boot, rendering, audio, menus or gameplay.

A complete lawful user-supplied ELF is still required to obtain real game-specific binding evidence. `PS2 PAD Runtime Confirmation v0` is the next milestone for confirming candidate PCs against observed guest-call behavior before any runtime activation.
