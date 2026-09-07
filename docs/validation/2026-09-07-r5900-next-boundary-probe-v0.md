# R5900 external next-boundary probe v0 validation

Status date: 2026-09-07

Branch: `feature/r5900-next-boundary-probe-v0`

## Scope

The optional external startup mode in `r5900_block_dispatcher_createsema_windows_tests.exe` now emits one stable `BOUNDARY_PROBE` record at the first controlled unsupported boundary after the validated startup prefix.

The record includes stop reason, exact guest PC, one mapped 32-bit guest word, decoded instruction/class/opcode/register/immediate fields, and execution counters. If the stop PC is unmapped, the raw/decoder fields are reported as `UNMAPPED`.

Only `UnsupportedInstruction` and `UnsupportedSyscall` are accepted as discovered boundaries. Compile, analysis, lowering, memory and host-syscall failures remain failures.

No proprietary game bytes are committed. CI uses synthetic public ISA encodings and synthetic data only.

## TDD evidence

- Formatter RED: `acf0c2e5b7c7012bf90bf63a17a6ce2ddcdc3b73`; Windows CI #787 failed at link with missing `format_boundary_probe`.
- Formatter GREEN: `23e1cdcc29319900a63ee5ff4d518bde03824179`; Windows CI #788 passed 67/67 tests, pacing and packaging.
- Classification RED: `c0da282a1a5b4d88233a7289e298ab9df9b56f3a`; Windows CI #789 failed at link with missing `is_discovered_boundary`.
- Integrated GREEN: `3c0729a4fef7434154f215d68a5c55275f0d5af0`; Windows CI #790 passed Build, 67/67 CTest, pacing and package validation.

Synthetic reporter contract:

```text
BOUNDARY_PROBE reason=UnsupportedInstruction pc=0x00114ef8 raw=0x70000000 instruction=UNKNOWN ...
```

The raw word above is the synthetic sentinel owned by the test fixture, not game data.

## External usage

```powershell
.\build\Release\r5900_block_dispatcher_createsema_windows_tests.exe C:\Games\Burnout3\SLUS_210.50
```

A complete lawful ELF is required. The historical local copy is 3,589,632 bytes while its load segment requires bytes through 4,073,344, so the production loader correctly rejects it.

External mode was not run in this environment. The exact next real game boundary therefore remains `PENDING_EXTERNAL_VALIDATION` and must not be guessed.
