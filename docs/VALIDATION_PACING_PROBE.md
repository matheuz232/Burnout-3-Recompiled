# Burnout3PacingProbe Validation

Status date: 2026-09-04

This addendum records implementation and Windows CI evidence for the standalone `Burnout3PacingProbe` developer tool. It supplements `docs/VALIDATION.md` without changing game execution, simulation timing, R5900 decoder/recompiler semantics, graphics, audio, or input behavior.

## Contract

`Burnout3PacingProbe` is Windows x64 only and reuses the production `WindowsFramePacer` plus `QpcClock` timing path at a fixed target of 120 Hz.

```text
Burnout3PacingProbe [--seconds <positive-integer>] [--output <path>]
Burnout3PacingProbe --help
```

The default duration is 60 seconds, which requests exactly 7200 frame intervals. The target rate cannot be overridden.

A successful report begins with:

```text
B3R_PACING_PROBE 1
TARGET_HZ 120
REQUESTED_SECONDS <N>
FRAMES <N*120>
```

It then embeds the existing deterministic `B3R_FRAME_PACING_TELEMETRY 1` summary and ends with `HIGH_RESOLUTION_TIMER YES/NO`.

The executable has no timing-quality PASS/FAIL threshold. Operational errors, invalid CLI arguments, duration overflow, output-file open failures, or output-file write failures are errors; observed desktop jitter itself is evidence, not a process failure condition.

## TDD evidence

- Parser RED commit `71a7be795d313308761e120d374b6c9e9e19a13a`, run `33833425537`: configure succeeded and MSVC failed compilation exactly because `tools/burnout3_pacing_probe_options.h` did not exist.
- Parser GREEN commit `8043bc9c0826d0486d5148fcafc584e6cf86569c`, run `33833561650`: Windows/MSVC passed 21/21 tests, including default 60 seconds, explicit seconds/output, help, invalid zero, missing values, duplicate options and unknown options.
- Executable-smoke RED commit `196481192501deb4661429913a5c6d8f35eb94b0`, run `33833683371`: 21/22 tests passed; only `burnout3_pacing_probe_smoke` was Not Run because `Burnout3PacingProbe.exe` did not exist.
- Executable GREEN commit `1bf5b3924cc56669785569cb946b26fdaf49a5a1`, run `33833827722`: 22/22 tests passed; the real Windows probe completed a 1-second/120-frame capture using the production pacer.
- File-output RED commit `fc789919318d9b3b365526981080cdbcbc59febd`, run `33833988815`: 22/23 tests passed; only `burnout3_pacing_probe_output` failed because the executable explicitly reported that file output was not available yet.
- File-output GREEN commit `87e91990f057bdabc784d94999ede37a9e35d905`, run `33834065915`: 23/23 passed. The test verified a real one-second output file containing probe metadata, `SAMPLES 120`, telemetry markers and timer mode, with no duplicate report on stdout.
- Package RED commit `3e53a73f8d448c342466604cee55baa61eb55d85`, run `33834158018`: 23/23 tests, frame-pacing telemetry, visible probe smoke and analyzer staging/validation passed; only `Stage pacing probe package` failed because `docs/PACING-PROBE-USAGE.txt` did not exist.
- Package GREEN commit `5c8e46bf1c6125367eb0b51660e362cbb311d6b7`, run `33834235339`: 23/23 plus visible probe smoke and both analyzer/probe staging/validation passed. Artifact uploads were correctly skipped because this was a feature-branch push.

## CI smoke scope

The dedicated CI step runs:

```text
Burnout3PacingProbe.exe --seconds 1
```

It surfaces 120 real paced intervals in the workflow log. This proves executable integration, report generation and use of the Windows pacing path; it is deliberately not used as a physical-desktop benchmark.

## Distribution boundary

The dedicated package is named `Burnout3PacingProbe-windows-x64` and contains only:

```text
Burnout3PacingProbe.exe
PACING-PROBE-USAGE.txt
```

Its workflow upload condition matches the analyzer distribution policy: publish on pushes to `main` and on manual workflow dispatch; feature-branch and pull-request runs only stage/validate it.

## Remaining physical validation gate

Run a 60-second or longer capture on a normal physical Windows 10/11 desktop session, preferably with the system in an ordinary usage state rather than an artificial idle/CI environment:

```text
Burnout3PacingProbe.exe --seconds 60 --output pacing-60s.txt
```

The generated report is the evidence needed to review sustained mean/P50/P95/P99, outliers, threshold counts and timer mode. Until such a desktop report exists, the project's long-duration physical pacing gate remains open.
