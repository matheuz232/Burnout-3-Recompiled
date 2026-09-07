# Progress

Status date: 2026-09-06

Completion rule: `implemented -> compiled -> executed/tested -> validated`.

This file is the active engineering snapshot. Detailed history remains in Git.

## Current status

| Component | Status | Evidence / next gate |
|---|---|---|
| Repository / CMake bootstrap | DONE | C++20, CMake 3.25+, Visual Studio 2022 / Windows x64 workflow |
| Win32 bootstrap/window | READY_FOR_INTERACTIVE_VALIDATION | CI creates/closes an HWND; physical Windows visual validation remains |
| QPC / 120 Hz frame pacing | CI_VALIDATED | Current SW milestone passes telemetry + 120-frame probe; physical 60-second desktop validation remains |
| Crash handler / minidump | CI_VALIDATED | Controlled Windows CI crash path |
| PS2 ELF loader | CI_VALIDATED | ELF32 little-endian MIPS parsing and PT_LOAD tests |
| EE main RAM v0 | CI_VALIDATED | Zero-filled 32 MiB `0x00000000..0x01ffffff`; PT_LOAD copied into RAM |
| Typed guest memory | CI_VALIDATED | Little-endian u8/u16/u32/u64/u128 reads/writes |
| R5900 decoder / IR | CI_VALIDATED | Incremental startup subset; `SQ -> Store128`, `SD -> Store64`, `SW -> Store32`, `DADDU -> Add64` |
| R5900 reference executor | CI_VALIDATED | Modeled EE state plus typed memory callbacks including write32/write64/write128 |
| Windows x86-64 backend | CI_VALIDATED | Native Store32/Store64/Store128 and current startup integer/control-flow subset; native/reference differential coverage |
| Native dispatcher/cache | CI_VALIDATED | On-demand lowering/native compile, exact guest-word validation, fast replay, boundary-prefix protection and store callbacks |
| `SQ / Store128` | CI_VALIDATED | Existing v0 full-128 store semantics |
| `SD / Store64` | CI_VALIDATED | Low64 source, 32-bit address wrap, strict 8-byte alignment, width-8 fault provenance |
| `DADDU / Add64` | CI_VALIDATED | Modulo-2^64 low64 add; destination high64 preserved |
| `SW / Store32` | CI_VALIDATED | Low32 source, 32-bit address wrap, strict 4-byte alignment, no align-down, exact width-4 fault provenance |
| `BEQ/BNE`, `BEQL/BNEL`, `J/JAL`, `JR/JALR` | CI_VALIDATED | Native control transfers and architectural delay-slot semantics |
| BSS clear startup loop | CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION | Synthetic/native loop validated; historical external harness reaches SetupThread |
| Host syscall service | CI_VALIDATED | Host boundary outside generated x64; deterministic handled/unsupported/fault accounting |
| `SetupThread` HLE `0x3c` | CI_VALIDATED | Explicit-stack mode |
| `SetupHeap` HLE `0x3d` | CI_VALIDATED | Automatic-size `-1` convention resolved from thread stack base |
| Startup through `SW @ 0x00114ee0` | CI_VALIDATED | Synthetic/native startup-shaped path executes `ADDIU/ADDIU/SD/DADDU/SW` and validates stack memory/state |
| Lawful next-boundary diagnosis | DIAGNOSTIC_VALIDATED | Legal ELF diagnosis continues beyond the first SW and reaches `SYSCALL @ 0x0010be24`, selector `0x40` in `v1`; not externally Windows-native validated |
| Static/binary recompiler | IN_PROGRESS | Next concrete startup/HLE boundary: syscall selector `0x40 @ 0x0010be24`; semantics still need design/diagnosis |
| Graphics / GS / VU | TODO | No game rendering path yet |
| IOP / SPU2 / audio | TODO | No game audio path yet |
| Game input | TODO | Not implemented |
| Game initialization | TODO | Not reached |
| Menu / gameplay | TODO | Game does not boot or reach gameplay |

## R5900 SW / Store32 v0

### Scope

`SW rt, imm(rs)` is modeled as:

```text
base32  = low32(GPR[rs].low64)
offset  = sign_extend16(imm)
address = uint32(base32 + offset)
value   = low32(GPR[rt].low64)
```

Contract:

- 32-bit effective-address arithmetic wraps modulo 2^32;
- strict 4-byte alignment;
- no alignment-down;
- exactly four bytes are written;
- source bits above bit 31 are ignored;
- CPU register state is preserved;
- failure stops later guest instructions;
- fault records exact guest PC/address and width `4`.

The memory callback ABI now contains `write32`, `write64`, and `write128`. The dispatcher wires all three in cold/exact-cache and fast-cache paths.

### TDD evidence

Representative RED/GREEN evidence:

```text
Task 1 RED   bd9310e1...  Store32 IR contract failed because opcode did not exist
Task 1 GREEN f598ed3d...  SW lowering + Store32 validation; CI #734 green
Task 2 RED   1e9c825f...  executor contract failed because write32 did not exist
Task 2 GREEN 6883c3c2...  write32 ABI + reference execution; CI #737 green
Task 3 RED   e80bd83a...  59/60 PASS; only native Store32 unsupported
Task 3 GREEN 2bf39108...  x64 Store32 helper/emitter; CI #739 green
Task 4 RED   4a9e857e...  60/62 PASS; only two dispatcher SW gates failed at SW boundary
Task 4 GREEN 751ff620...  dispatcher write32 + SW eligibility; CI #743 62/62 green
```

Dedicated named Store32 gates were then split out without changing production code. Implementation/test head before documentation:

```text
491385fc6e12f7f7d1189948daafe5e89d0bfbb2
```

Windows CI run **#747** (`34073509755`) produced:

```text
host  Windows Server 2022
MSVC  19.44 / Visual Studio 2022
CTest 65/65 PASS
```

Required Store32 gates:

```text
r5900_ir_store32_tests                         PASS
r5900_ir_store32_executor_tests                PASS
r5900_x64_store32_windows_tests                PASS
r5900_block_dispatcher_store32_windows_tests   PASS
r5900_block_dispatcher_sw_startup_windows_tests PASS
```

Legacy Store64/Store128, control-flow, syscall, memory-map and startup tests also passed.

Frame-pacing telemetry on #747:

```text
samples         240
mean            8.333 ms
min             8.256 ms
max             8.384 ms
P95             8.333 ms
P99             8.351 ms
>9/10/12 ms     0 / 0 / 0
high-res timer  YES
```

One-second pacing probe:

```text
target          120 Hz
frames          120
mean            8.333 ms
min             8.331 ms
max             8.333 ms
>9/10/12 ms     0 / 0 / 0
high-res timer  YES
```

Analyzer and pacing-probe package validation both passed. Hosted CI pacing remains smoke evidence rather than physical-desktop performance certification.

### Synthetic/native startup acceptance

The SW startup gate uses only public ISA encodings and synthetic data, while retaining the real startup PCs:

```text
0x00114ed0  ADDIU sp,sp,-0x50
0x00114ed4  ADDIU v0,zero,1
0x00114ed8  SD    ra,0x40(sp)
0x00114edc  DADDU a0,sp,zero
0x00114ee0  SW    v0,0x28(sp)
```

Acceptance:

```text
initial sp                0x01fffff0
initial ra                0x00115118
sp                        0x01ffffa0
v0.low64                  0x0000000000000001
a0.low64                  0x0000000001ffffa0
a0.high64                 preserved
mem64[0x01ffffe0]         0x0000000000115118
mem32[0x01ffffc8]         0x00000001
adjacent 32-bit guards    unchanged
```

Separate dispatcher coverage proves:

- aligned success;
- misaligned/out-of-range `MemoryAccessFailure` width 4;
- faulting SW is not counted as completed;
- earlier prefix effects remain visible on SW failure;
- a compiled SW block survives cold fault -> corrected success -> cached fault with no recompilation;
- SW commits before a later unsupported boundary;
- a second successful `SW + J + NOP` execution fast-replays from cache.

## Lawful real-ELF diagnostic after SW

The user's lawful Burnout 3 ELF remains outside the repository. No real raw instruction words/bytes are copied into source, tests, or documentation.

Starting state at the known call target:

```text
PC = 0x00114ed0
sp = 0x01fffff0
ra = 0x00115118
```

The supported model advances through the first SW and the real path continues through additional already-supported stores and a direct call. Derived state at the next current host boundary:

```text
PC        0x0010be24
boundary  SYSCALL
selector  0x40 in v1

sp.low64  0x01ffffa0
v0.low64  0x0000000000000001
a0.low64  0x0000000001ffffa0
ra.low64  0x0000000000114ef4
v1.low64  0x0000000000000040

mem64[0x01ffffe0]  0x0000000000115118
mem32[0x01ffffc8]  0x00000001
mem32[0x01ffffa4]  0x00000001
mem32[0x01ffffa8]  0x00000001
mem32[0x01ffffc4]  0x00000001
```

The host syscall service currently recognizes only selectors `0x3c` (`SetupThread`) and `0x3d` (`SetupHeap`). Therefore selector `0x40` is the next empirically observed HLE boundary. Its semantics are **not** guessed here; the next milestone must diagnose/design them first.

This is `DIAGNOSTIC_VALIDATED`, not `EXTERNALLY_VALIDATED`. The existing external Windows startup harness has not yet been extended/executed end-to-end through SetupHeap/SD/DADDU/SW.

## Remaining Test Build 0.1 gates

1. Diagnose and design the observed EE syscall selector `0x40` boundary.
2. Extend the external Windows-native startup harness through the newer startup path using only a user-supplied local ELF.
3. Validate the Win32 executable interactively on a physical Windows 10/11 desktop.
4. Run a 60-second or longer physical-desktop 120 Hz pacing capture.
5. Continue R5900/kernel/HLE coverage iteratively from measured boundaries.
6. Implement the still-missing GS/VU, IOP/SPU2, input and game-runtime subsystems before any boot/playability claim.

## Guardrails

- `design/r5900-sw-v0` is the isolated milestone branch.
- Base/integration branch is `feature/r5900-or-v0`.
- Integration requires explicit user authorization after final branch verification.
- Never commit proprietary Burnout 3 data.
- Never claim boot/playability without direct evidence.