# Burnout3PacingProbe Validation

Status date: 2026-09-04

This addendum records validation evidence for the Windows x64 `Burnout3PacingProbe` tool. The probe exists to make the Test Build 0.1 physical-desktop pacing gate reproducible without changing the production `WindowsFramePacer` implementation or treating hosted CI as a desktop benchmark.

## Contract

`Burnout3PacingProbe` is Windows-only and uses the existing timing path:

```text
QpcClock::now_seconds()
  -> WindowsFramePacer(120.0)
  -> exact 120 * requested_seconds frame intervals
  -> summarize_frame_pacing()
  -> deterministic text telemetry report
```

The target is fixed at 120 Hz. `--seconds` is a positive integer and defaults to 60, so the default capture contains exactly 7200 intervals. `--output <path>` writes the report to a file; without it the report is written to stdout.

The report records `TARGET_HZ`, `REQUESTED_SECONDS`, `FRAMES`, `HIGH_RESOLUTION_TIMER`, and the existing `B3R_FRAME_PACING_TELEMETRY 1` metrics. The probe deliberately does not emit a performance PASS/FAIL classification. Operational/argument/output errors are failures; measured jitter itself is evidence.

## TDD / CI evidence

- Parser RED run `33832016872`: configure succeeded and MSVC failed exactly because `tools/burnout3_pacing_probe_options.h` did not exist.
- Parser GREEN run `33832153516`: Windows/MSVC passed 21/21 tests, including default 60 seconds, positive `--seconds`, `--output`, `--help`, duplicate/missing/invalid argument handling, and fixed-120-Hz usage text.
- App RED run `33832281693`: configure succeeded and MSVC failed exactly because `tools/burnout3_pacing_probe_app.h` did not exist.
- App GREEN run `33832432978`: Windows/MSVC passed 22/22 tests. The one-second app smoke completed in 1.01 seconds and verified exactly 120 frame intervals/samples, fixed 120 Hz metadata, high-resolution timer evidence, the existing telemetry report, no performance classification, and duration-overflow rejection.
- Executable RED run `33832644412`: build succeeded and 22/23 tests passed. Only `burnout3_pacing_probe_help` was not run because `Burnout3PacingProbe.exe` did not yet exist.
- Executable GREEN run `33832757211`: 23/23 tests passed, including `Burnout3PacingProbe --help` and the one-second app smoke.
- Package RED run `33832885053`: 23/23 tests passed and the visible one-second probe smoke reported `TARGET_HZ 120`, `FRAMES 120`, `SAMPLES 120`, and high-resolution timer evidence. Analyzer staging/validation passed; pacing-probe staging failed only because `docs/PACING-PROBE-USAGE.txt` was deliberately absent.
- Package GREEN run `33832975764`: 23/23 tests, visible pacing-probe smoke, existing frame-pacing telemetry, analyzer staging/validation, and pacing-probe staging/validation all passed. Both artifact uploads were correctly skipped because this was a feature-branch push.

## Hosted-CI scope

The one-second CI probe is an execution/report-structure smoke. GitHub-hosted Windows Server is not a physical Windows 10/11 desktop certification environment and its timing numbers must not be promoted to the Test Build 0.1 desktop pacing result.

The physical-desktop gate remains open until a 60-second or longer `Burnout3PacingProbe` capture is run in the desktop conditions being characterized and the generated text report is reviewed.

## Data boundary

The pacing report contains timing telemetry only. The tool does not load or inspect a Burnout 3 ELF, game asset, save, disc image, or any other proprietary game data. The generated text report is the intended evidence to share back.
