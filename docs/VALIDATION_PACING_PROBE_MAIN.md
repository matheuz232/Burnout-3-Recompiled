# Burnout3PacingProbe Main Publication Validation

Status date: 2026-09-04

This addendum records the post-merge `main` validation and first published Windows artifact for `Burnout3PacingProbe`.

## Merge evidence

- PR #17: `tools: add Windows frame pacing probe`.
- Validated feature HEAD: `31205fb10973a18dd9b52e7d2a30d5d4556805ec`.
- PR merge-ref: `215795a269034573f031fa0f14df2826ce2013c9`.
- PR workflow run `33834545021`: Windows Server 2022 / VS2022 / MSVC 19.44 passed 23/23 tests, the visible one-second pacing-probe smoke, analyzer package staging/validation, and pacing-probe package staging/validation. Both artifact uploads were correctly skipped for the pull-request event.
- Merged `main` commit: `212997302b2026c9447d5386c728339442a9c9c7`.
- Post-merge `main` workflow run `33834646204`: 23/23 tests passed; frame-pacing telemetry, pacing-probe smoke, analyzer package staging/validation, and pacing-probe package staging/validation all passed. Both artifact upload steps succeeded.

## Published pacing-probe artifact

`Burnout3PacingProbe-windows-x64`:

- artifact ID: `9922874193`;
- ZIP size: 27,369 bytes;
- SHA-256: `6ed6a3e8f0906056ec57a35b5e60b6ba0f0199035301b0b4587b37c2ab84a4ec`;
- source run: `33834646204`;
- source commit: `212997302b2026c9447d5386c728339442a9c9c7`;
- expires: 2026-12-03.

The downloaded ZIP was inspected after publication. It contains exactly:

```text
Burnout3PacingProbe.exe
PACING-PROBE-USAGE.txt
```

`Burnout3PacingProbe.exe` is 62,976 bytes and begins with the expected `MZ` PE signature. `PACING-PROBE-USAGE.txt` is 1,588 bytes. The locally calculated ZIP SHA-256 matches the GitHub artifact digest above.

The same `main` run also republished `Burnout3Analyze-windows-x64` as artifact ID `9922873755`.

## Remaining physical validation gate

CI publication does not close the long-duration physical-desktop pacing gate. Run the published probe on a normal Windows 10/11 desktop for at least 60 seconds:

```text
Burnout3PacingProbe.exe --seconds 60 --output pacing-60s.txt
```

Review/share the generated text report. Hosted CI smoke data remains integration evidence, not physical-desktop performance certification.
