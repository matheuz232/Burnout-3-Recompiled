# Progress

Status date: 2026-09-07

Completion rule: `implemented -> compiled -> executed/tested -> validated`.

This file is the active engineering snapshot. Detailed history remains in Git and in dated validation records under `docs/validation/`.

## Current branch

- Milestone branch: `feature/r5900-ld-v0`
- Implementation/test head before documentation: `ad31381582d865be814727fddf761eeafefdf4b3`
- Latest validated Windows CI before documentation: run **#783** (`34092035181`)
- CTest on #783: **67/67 PASS**
- Game/runtime status: the project still does **not** boot Burnout 3, render the game, reach menus, or provide gameplay.

## Current engineering status

| Component | Status | Evidence / next gate |
|---|---|---|
| Repository / CMake bootstrap | DONE | C++20, CMake 3.25+, Visual Studio 2022 / Windows x64 workflow |
| Win32 bootstrap/window | READY_FOR_INTERACTIVE_VALIDATION | CI HWND smoke exists; physical Windows visual validation remains |
| QPC / 120 Hz frame pacing | CI_VALIDATED | #783 pacing telemetry and 120-frame probe passed; long physical-desktop capture remains |
| Crash handler / minidump | CI_VALIDATED | Controlled Windows CI crash path |
| PS2 ELF loader | CI_VALIDATED | ELF32 little-endian MIPS parsing and PT_LOAD validation |
| EE main RAM v0 | CI_VALIDATED | Zero-filled 32 MiB `0x00000000..0x01ffffff`; PT_LOAD copied into RAM |
| Typed guest memory | CI_VALIDATED | Little-endian u8/u16/u32/u64/u128 reads/writes |
| R5900 decoder / IR | CI_VALIDATED | Startup subset now includes `LD -> Load64`, `SW -> Store32`, `SD -> Store64`, `SQ -> Store128`, `DADDU -> Add64` |
| R5900 reference executor | CI_VALIDATED | `read64`, write32/write64/write128 callbacks and precise load/store faults |
| Windows x86-64 backend | CI_VALIDATED | Native `Load64`, Store32/64/128 plus current integer/control-flow subset; native/reference differential tests |
| Native dispatcher/cache | CI_VALIDATED | On-demand lowering/compile, exact guest-word validation, cold/exact/fast replay, `read64` + store adapters, precise load/store fault diagnostics |
| `LD / Load64` | CI_VALIDATED | 32-bit wrapped effective address, strict 8-byte alignment, transactional load, destination high64 preservation, observable `LD $zero` |
| `SW / Store32` | CI_VALIDATED | Low32 source, strict 4-byte alignment, exact width-4 store faults |
| `SD / Store64` | CI_VALIDATED | Low64 source, strict 8-byte alignment, exact width-8 store faults |
| `SQ / Store128` | CI_VALIDATED | Existing v0 full-128 store semantics |
| `BEQ/BNE`, `BEQL/BNEL`, `J/JAL`, `JR/JALR` | CI_VALIDATED | Native control transfers and architectural delay-slot semantics |
| Host syscall service | CI_VALIDATED | Host boundary outside generated x64; deterministic handled/unsupported/fault accounting |
| `SetupThread` HLE `0x3c` | CI_VALIDATED | Explicit-stack mode |
| `SetupHeap` HLE `0x3d` | CI_VALIDATED | Automatic-size `-1` convention resolved from thread stack base |
| `CreateSema` HLE `0x40` | CI_VALIDATED | Per-service IDs, validated descriptor copy, transactional failures, native continuation/cache |
| Synthetic startup through previous `LD @ 0x00114f08` boundary | CI_VALIDATED | Startup-shaped test executes the LD, restores RA low64, preserves high64, and stops only at the following synthetic sentinel |
| Real external next boundary | PENDING_EXTERNAL_VALIDATION | Requires a complete user-supplied lawful ELF; available historical local copy is truncated and production loader correctly rejects it |
| Static/binary recompiler | IN_PROGRESS | Continue from the next boundary measured with a complete external ELF |
| Graphics / GS / VU | TODO | No game rendering path yet |
| IOP / SPU2 / audio | TODO | No game audio path yet |
| Game input | TODO | Not implemented |
| Game initialization | TODO | Not reached |
| Menu / gameplay | TODO | Not reached |

## R5900 LD / Load64 v0

### Semantics

`LD rt, imm(rs)` is modeled as:

```text
base32  = low32(GPR[rs].low64)
offset  = sign_extend16(imm)
address = uint32(base32 + offset)
value   = read64(address)
```

Contract:

- 32-bit effective-address arithmetic wraps modulo 2^32;
- effective address must be 8-byte aligned; no align-down is performed;
- guest memory access is made through `R5900GuestRead64Fn`;
- a successful load writes only `GPR[rt].low64` and preserves `high64`;
- destination register zero still performs the memory access and can fault;
- on any alignment/unmapped/callback failure, destination state is unchanged;
- fault provenance records access kind `Load`, exact guest PC, exact address and width `8`;
- later IR and later control-flow effects are not executed after a failing load.

The Windows x64 helper returns the loaded `uint64_t` in RAX. The emitted block tests `context.memory_fault.active` before writing the destination. This deliberately avoids using `[rsp+0x30]` for the 64-bit load result because that stack slot is already used to preserve indirect-transfer targets across helper-containing delay slots.

### Dispatcher integration

The dispatcher now:

- accepts decoded `R5900Instruction::Ld` in the v0 native subset;
- wires `Ps2MemoryMap::read_u64` into both normal and fast-cache execution contexts;
- retains exact guest-word validation before fast replay;
- reports `load width 8 bytes` for Load64 faults and keeps existing `store` diagnostics for stores;
- uses precise fault guest PC to count only the successfully completed instruction prefix.

Coverage includes:

- native/reference success parity;
- signed offsets and modulo-32-bit address wrap;
- destination high64 preservation;
- `LD $zero` observable read semantics;
- misaligned, missing callback and rejected callback failures;
- transactional destination preservation;
- `SD + LD + JAL` in one native block;
- `SW + LD + J + NOP` in one native block;
- cold compile and fast-cache replay;
- exact load-fault PC/address/width;
- startup-shaped `CreateSema -> SW -> LD` continuation past the former boundary.

## TDD evidence for LD / Load64 v0

```text
Task 1 IR RED       7042d21a...  new Load64 IR contract absent
Task 1 IR GREEN     b124e0d5...  LD lowering + validation; Windows CI #774 green

Task 2 executor RED c9eae3b8...  read64/Load fault ABI absent
Task 2 executor GREEN
                    873dda77...  read64 ABI + reference Load64; Windows CI #776 green

Task 3 x64 RED      99f0659d...  Build green, CTest red because native Load64 unsupported
Task 3 x64 GREEN    f4af598d...  Windows x64 Load64 helper/emitter; Windows CI #778 green

Task 4 dispatcher RED
                    9bde223c...  66/67 pass; only SD+LD+JAL dispatcher gate fails
Startup RED         273150a8...  synthetic CreateSema path still fails at LD boundary
Task 4 dispatcher GREEN
                    a5530320...  LD eligibility/read64 adapters/load fault diagnostics
Test-contract cleanup
                    f7105f1f...  old syscall tests updated from unsupported-LD assumptions
                    ad313815...  old Store32 boundary test updated to supported SW+LD path
Integrated CI       #783         67/67 CTest PASS; pacing/package gates PASS
```

## Startup boundary status

The synthetic startup gate uses public ISA encodings and synthetic data only. It now crosses the previously observed `LD @ 0x00114f08` boundary. In the synthetic fixture, `LD ra,0x40(sp)` reloads the saved return address low64, preserves RA high64, and execution stops only at a deliberate unsupported sentinel after the LD.

The historical external input available in the development environment was incomplete:

```text
available file size                  3,589,632 bytes
load segment requires bytes through  4,073,344 bytes
```

The production loader correctly rejects that truncated input. Loader validation was not weakened, missing game bytes were not invented, and no proprietary game bytes are committed.

Therefore the next unsupported instruction on the real Burnout 3 path is currently **unknown** and must not be guessed. The optional external Windows test accepts a complete user-supplied lawful ELF, runs the production loader/dispatcher, requires execution to move beyond `0x00114f08`, verifies SetupThread/SetupHeap/two CreateSema calls, and prints the newly measured stop PC/diagnostic.

Example local invocation on a user's own complete ELF:

```powershell
.\build\Release\r5900_block_dispatcher_createsema_windows_tests.exe C:\Games\Burnout3\SLUS_210.50
```

CI contains no game file and runs synthetic mode only.

## Windows CI evidence

Windows CI #783 (`34092035181`) on commit `ad31381582d865be814727fddf761eeafefdf4b3`:

```text
Host             Windows Server 2022
Generator        Visual Studio 17 2022 x64
Compiler         MSVC 19.44
CTest            67/67 PASS
Frame telemetry  PASS
120 Hz probe     PASS
Analyzer package PASS
Pacing package   PASS
```

Representative hosted pacing data from #783:

```text
frame telemetry: 240 samples, mean 8.333 ms, >9/10/12 ms = 0/0/0
one-second probe: 120 frames, mean 8.333 ms, max 8.416 ms, >9/10/12 ms = 0/0/0
high-resolution timer: YES
```

Hosted CI pacing is smoke evidence, not a substitute for physical Windows desktop performance certification.

## Remaining Test Build 0.1 gates

1. Run the external startup harness with a complete user-supplied lawful ELF and record the first new real boundary after `0x00114f08`.
2. Continue R5900 instruction/kernel/HLE coverage from that measured boundary using the same RED -> GREEN process.
3. Validate the Win32 executable interactively on a physical Windows 10/11 desktop.
4. Run a 60-second or longer physical-desktop 120 Hz pacing capture.
5. Implement GS/VU rendering, IOP/SPU2/audio, input and remaining game-runtime services before any boot/playability claim.

## Guardrails

- Milestone branch: `feature/r5900-ld-v0`.
- Integration/base lineage remains the existing R5900 feature stack; do not merge blindly without branch review.
- No PCSX2 runtime dependency is introduced by this milestone.
- Never commit proprietary Burnout 3 data.
- Never claim boot, menu, rendering or gameplay without direct evidence.
