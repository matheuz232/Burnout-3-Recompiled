# R5900 DIRECT_CALL_TARGETS Validation

Status date: 2026-09-03

This addendum records validation evidence for deterministic `DIRECT_CALL_TARGETS` aggregation in the R5900 analysis report. It supplements `docs/VALIDATION.md` without changing decoder, control-flow, reachability, CLI, or guest-execution semantics.

## Contract

For every resolved direct-call record in the reachable analysis graph, the report groups evidence by guest target address and emits:

```text
DIRECT_CALL_TARGETS <unique-target-count>
  TARGET 0x........ CALL_SITES <static-reference-count>
```

Targets are ordered by guest address through a deterministic ordered map. `CALL_SITES` counts static call records in the analyzed graph; it is not execution frequency and does not imply that a target executed at runtime.

Indirect calls and direct calls without a resolved target are excluded from this aggregation, but remain visible through the ordinary `CALL` records. A grouped target is not labeled as a function and the aggregation does not infer function boundaries.

## Synthetic fixture

The report test uses five call records:

- direct call at `0x00001008` -> `0x00002000`;
- direct call at `0x00001018` -> `0x00002000`;
- indirect call at `0x00001024` with unresolved target;
- direct call at `0x00001034` -> `0x00003000`;
- direct call at `0x00001044` with unresolved target.

The expected aggregation therefore contains exactly two targets: `0x00002000` with `CALL_SITES 2` and `0x00003000` with `CALL_SITES 1`. The graph call container is reversed in a second rendering and must still produce byte-identical report text.

## TDD / CI evidence

- RED commit `8ddc508b779d5328e1c620945b1b9f3641a40406`, run `33828724619`: configure/build succeeded and 19/20 tests passed. Only `r5900_analysis_report_tests` failed with `report must aggregate resolved direct call targets by static call-site count`, proving the aggregation was absent.
- Renderer commit `4b6efe985b6f40edf0e768b5e28125108f5b3624`, run `33828875766`: `r5900_analysis_report_tests` passed, proving grouping, filtering, ordering, and deterministic rendering. The only remaining failure was the older byte-exact `ps2_elf_analysis_tests` expectation, which did not yet include `DIRECT_CALL_TARGETS 0`.
- End-to-end contract commit `066cc93d27bd15efbc12fe416c6dacf5fd3380d8`, run `33828970350`: Windows Server 2022 / Visual Studio 2022 / MSVC 19.44 passed 20/20 tests. Frame-pacing telemetry, analyzer package staging, and package validation also passed. Artifact upload was correctly skipped because this was a feature-branch push.

## Scope boundary

No R5900 instruction was newly decoded. No control-flow or reachability behavior changed. `--follow-direct-calls` behavior is unchanged. No indirect target is guessed, no target is promoted to a function, and no runtime profile is inferred from static call-site counts.

All test data is synthetic and non-proprietary. The next evidence gate remains a legally obtained external Burnout 3 executable analyzed outside the repository, using the generated text report—especially `UNKNOWN_PRIMARY_OPCODES`, `UNKNOWN_SITES`, and `DIRECT_CALL_TARGETS`—to choose further decoder/recompiler work from actual coverage.
