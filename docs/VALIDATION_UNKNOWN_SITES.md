# R5900 UNKNOWN_SITES Validation

Status date: 2026-09-03

This addendum records validation evidence for the deterministic `UNKNOWN_SITES` section added to the R5900 analysis report. It supplements `docs/VALIDATION.md` without changing any decoder or guest-execution semantics.

## Contract

For every reachable instruction site whose decoded instruction remains `Unknown`, the report emits one `UNKNOWN_SITES` record containing:

- guest `PC`;
- 32-bit `RAW` instruction word;
- six-bit `PRIMARY` field;
- five-bit raw `RS`, `RT`, `RD`, and `SA` bit positions;
- six-bit raw `FUNCT` field.

Architectural delay-slot sites are included because they are part of the reachable analysis graph. Records are ordered by guest PC, with the raw word as a deterministic tie-breaker.

These fields are diagnostic bit positions only. Their semantic meaning depends on the instruction format and opcode family; the report does not claim that every `RS/RT/RD/SA/FUNCT` field represents a register, shift amount, or function selector for an unsupported instruction.

## Synthetic fixture

The deterministic report test uses two explicit `Unknown` sites:

- delay slot at `0x00001004`, raw `0x4BEF1234`, expected `PRIMARY=0x12 RS=0x1F RT=0x0F RD=0x02 SA=0x08 FUNCT=0x34`;
- normal instruction at `0x00001010`, raw `0x712A4CC1`, expected `PRIMARY=0x1C RS=0x09 RT=0x0A RD=0x09 SA=0x13 FUNCT=0x01`.

The same graph evidence is rendered again with container ordering reversed and must produce byte-identical text.

## TDD / CI evidence

- RED commit `1768c53843ef03c952a452a1f6854c754e86678f`, run `33817785688`: configure/build succeeded and 19/20 tests passed. Only `r5900_analysis_report_tests` failed with `report must include deterministic unknown-site bitfield evidence including delay slots`, proving the new report contract was absent.
- Renderer commit `6be4cef4c7fa72ecfd8ad209bb9d788d41423b0d`, run `33817933590`: `r5900_analysis_report_tests` passed, proving `UNKNOWN_SITES` field extraction/order/delay-slot behavior. The only remaining failure was the pre-existing byte-exact `ps2_elf_analysis_tests` expectation, which did not yet include `UNKNOWN_SITES 0`.
- End-to-end contract commit `24e3f9f149c2212dfb3652c8a160507d9c5da32b`, run `33818075117`: Windows Server 2022 / Visual Studio 2022 / MSVC 19.44 passed 20/20 tests. Frame-pacing telemetry, analyzer package staging, and package validation also passed. Artifact upload was correctly skipped because this was a feature-branch push.

Observed pacing smoke from the final GREEN run remained healthy: 240 samples, 8.333 ms mean, 8.333 ms P50/P95/P99, 8.333 ms max after three-decimal rounding, zero samples above 9/10/12 ms, and the high-resolution timer path active. This remains hosted-runner evidence rather than physical-desktop performance certification.

## Scope boundary

No R5900 instruction was newly decoded. No MMI/COP/SPECIAL semantics were inferred. Reachability, `--follow-direct-calls`, CLI options, guest execution, graphics, audio, input, and simulation cadence are unchanged.

All test data is synthetic and non-proprietary. The next evidence gate remains a legally obtained external Burnout 3 executable analyzed outside the repository, using the generated text report—especially `UNKNOWN_PRIMARY_OPCODES` and `UNKNOWN_SITES`—to choose the next decoder/recompiler work from actual coverage.
