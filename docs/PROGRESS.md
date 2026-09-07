# Progress

Status date: 2026-09-07

Completion rule: `implemented -> compiled -> executed/tested -> validated`.

This file is the active engineering snapshot. Detailed milestone history remains in Git.

## Current status

| Component | Status | Evidence / next gate |
|---|---|---|
| Repository / CMake bootstrap | DONE | C++20, CMake 3.25+, Visual Studio 2022 / Windows x64 workflow |
| Win32 bootstrap/window | READY_FOR_INTERACTIVE_VALIDATION | CI creates/closes an HWND; physical Windows visual validation remains |
| QPC / 120 Hz frame pacing | CI_VALIDATED | Hosted telemetry + 120-frame probe; physical 60-second desktop validation remains |
| PS2 ELF loader / EE RAM | CI_VALIDATED | ELF32 little-endian MIPS + zero-filled 32 MiB EE main RAM |
| Typed guest memory | CI_VALIDATED | Little-endian u8/u16/u32/u64/u128 reads/writes |
| R5900 decoder / IR / reference executor | CI_VALIDATED | Incremental startup subset including `DADDU`, `SQ`, `SD`, `SW` |
| Windows x86-64 backend | CI_VALIDATED | Current startup integer/control-flow/store subset with native/reference coverage |
| Native dispatcher/cache | CI_VALIDATED | On-demand lowering/native compile, exact guest-word validation, fast replay, boundary-prefix protection |
| Host syscall service | CI_VALIDATED | Handled/unsupported/fault contract outside generated x64 |
| `SetupThread` HLE `0x3c` | CI_VALIDATED | Explicit-stack mode |
| `SetupHeap` HLE `0x3d` | CI_VALIDATED | Automatic-size `-1` convention derived from stack base |
| `CreateSema` HLE `0x40` | CI_VALIDATED | IDs, validation, registry, alignment and transactional faults; CI #763 |
| Startup through two observed `CreateSema` calls | DIAGNOSTIC_VALIDATED | Lawful local ELF diagnosis returns IDs 1/2 and reaches `LD @ 0x00114f08` |
| Static/binary recompiler | IN_PROGRESS | Next concrete R5900 boundary: `LD @ 0x00114f08` |
| Graphics / GS / VU | TODO | No game rendering path yet |
| IOP / SPU2 / audio | TODO | No game audio path yet |
| Game input | TODO | Not implemented |
| Game initialization | TODO | Not reached |
| Menu / gameplay | TODO | Game does not boot or reach gameplay |

## EE `CreateSema v0`

Observed EE syscall selector:

```text
selector 0x40
argument a0.low32 -> guest semaphore descriptor
```

The descriptor is interpreted as six little-endian 32-bit words. v0 uses `max_count`, `init_count`, `attr` and `option`; guest `count` and `wait_threads` are not authoritative host state.

Contract:

- descriptor pointer must be 4-byte aligned and fully readable;
- `max_count > 0`;
- `0 <= init_count <= max_count`;
- deterministic IDs are allocated as `1, 2, 3, ...`;
- host registry records `id`, `current_count = init_count`, `max_count`, `attr`, `option`;
- successful creation returns the ID in `v0.low64` and preserves `v0.high64`;
- unrelated architectural state and guest descriptor memory are preserved;
- a fault creates no registry entry and consumes no ID.

Scheduler/blocking semantics and the remaining semaphore syscalls are intentionally deferred until execution reaches them.

### TDD evidence

```text
#756 RED    successful CreateSema not yet handled
#757 GREEN  mapped basic CreateSema handling
#758 RED    deterministic second ID missing
#760 GREEN  monotonic IDs + max/init validation
#761 RED    registry/alignment contract absent
#762 RED    registry API exists but behavior still incomplete
#763 GREEN  registry state + alignment + attr/option + transactional commit
```

Implementation head before documentation:

```text
e7c618ebaa77538d8f9faccc932a0ef15b840ddc
```

Windows CI #763 (`34080792349`) completed successfully with the full test suite, frame-pacing telemetry, one-second 120 Hz pacing probe, analyzer-package validation and pacing-probe package validation.

Hosted CI pacing is smoke evidence only; it is not physical-desktop 120 FPS certification.

## Lawful real-ELF diagnostic after CreateSema

The user's lawful Burnout 3 ELF remains outside the repository. No real raw instruction words/bytes are copied into source, tests or documentation.

Starting from the already established startup state, the diagnostic advances through the observed `CreateSema` wrapper twice. Derived HLE results:

```text
CreateSema #1  id=1  current=1  max=1  attr=0  option=0
CreateSema #2  id=2  current=1  max=1  attr=0  option=0
```

The guest stores the first returned ID before invoking the second creation. After the second syscall returns, execution reaches the next unsupported R5900 instruction:

```text
PC        0x00114f08
boundary  LD
```

This is `DIAGNOSTIC_VALIDATED`, not `EXTERNALLY_VALIDATED`: the complete newer startup slice has not yet been certified end-to-end by a physical Windows-native run.

## Remaining Test Build 0.1 gates

1. Design and implement the measured `LD @ 0x00114f08` boundary with TDD.
2. Extend the external Windows-native startup harness through the newer startup path using only a user-supplied local ELF.
3. Validate the Win32 executable interactively on a physical Windows 10/11 desktop.
4. Run a 60-second or longer physical-desktop 120 Hz pacing capture.
5. Continue R5900/kernel/HLE coverage iteratively from measured boundaries.
6. Implement GS/VU, IOP/SPU2, input and game-runtime subsystems before any boot/playability claim.

## Guardrails

- Current isolated milestone branch: `design/r5900-create-sema-v0`.
- Base/integration branch: `feature/r5900-or-v0`.
- Integration requires explicit user authorization after final branch verification.
- Never commit proprietary Burnout 3 data.
- Never claim boot/playability without direct evidence.