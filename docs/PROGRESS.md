# Progress

Status date: 2026-09-06

Completion rule: `implemented -> compiled -> executed/tested -> validated`.

This document is the current engineering snapshot. Older milestone-by-milestone CI detail remains available in Git history; the active file keeps only evidence that is still useful for deciding the next gate.

## Current status

| Component | Status | Evidence / next gate |
|---|---|---|
| Repository / CMake bootstrap | DONE | C++20, CMake 3.25+, Visual Studio 2022 / Windows x64 workflow is established |
| Win32 bootstrap/window | READY_FOR_INTERACTIVE_VALIDATION | CI creates and closes a real HWND; physical Windows 10/11 visual validation remains |
| QPC / 120 Hz frame pacing | CI_VALIDATED | Current DADDU milestone CI passes 240-sample telemetry and a 120-frame/1-second probe; physical 60-second desktop capture remains required |
| Crash handler / minidump | CI_VALIDATED | Controlled Windows CI crash path produces diagnostics/minidump |
| PS2 ELF loader | CI_VALIDATED | ELF32 little-endian MIPS parsing and PT_LOAD metadata tests pass |
| EE main RAM v0 | CI_VALIDATED | Runtime owns zero-filled 32 MiB `0x00000000..0x01ffffff`; ELF PT_LOAD bytes are copied into RAM while `regions()` remains ELF metadata only; reads/writes outside PT_LOAD but inside EE RAM are valid |
| Typed guest memory | CI_VALIDATED | Little-endian u8/u16/u32/u64/u128 reads/writes; complete-range checks prevent partial wide writes |
| R5900 decoder | CI_VALIDATED | Startup integer/MMI/COP1/control-flow/store subset includes `SQ`, `SD` and `DADDU`; broader ISA remains incremental |
| R5900 IR v0 | CI_VALIDATED | Provenance-carrying instruction IR plus typed control-transfer terminators; `SQ -> Store128`, `SD -> Store64`, `DADDU -> Add64` |
| R5900 IR reference executor | CI_VALIDATED | Executes modeled EE state plus memory callbacks; `Add64` is modulo-2^64 and preserves destination high64 |
| Windows x86-64 backend | CI_VALIDATED | Native code generation for current startup subset including `Add64`, control transfers, `Store64` and `Store128`; reference/native differential tests pass |
| Native block dispatcher/cache | CI_VALIDATED | On-demand analysis/lowering/native compile, exact guest-word cache validation, fast replay, boundary-prefix protection, host-syscall boundaries, guest-memory store adapters and DADDU eligibility |
| `SQ / Store128` | CI_VALIDATED | Low32 base + signed imm16 with 32-bit wrap, existing v0 16-byte alignment-down behavior, full 128-bit source write |
| `SD / Store64` | CI_VALIDATED | Low32 base + signed imm16 with 32-bit wrap; stores only source `low64`; requires 8-byte alignment; exact PC/address/width-8 failure reporting; no address rounding |
| `DADDU / Add64` | CI_VALIDATED | Unsigned/modulo-2^64 low64 addition with no overflow trap; destination high64 is preserved; `rd==0` lowers to provenance `Nop`; native/cache differential coverage passes |
| `BEQ` / `BNE` | CI_VALIDATED | Ordinary delay-slot branches execute native and preserve pre-slot predicate semantics |
| `BEQL` / `BNEL` | CI_VALIDATED | Branch-likely annulment: delay executes only on taken path |
| `J` / `JAL` | CI_VALIDATED | Direct transfers and PC+8 link semantics validated |
| `JR` / `JALR` | CI_VALIDATED | Runtime target snapshot, arbitrary link GPR, `rd==rs`, `rd==0`, high64 preservation validated |
| BSS clear startup loop | CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION | Synthetic/native fast-cache loop is validated; lawful external harness historically proves the real BSS clear through `SetupThread`, but newer HLE/SD/DADDU extension is not yet externally Windows-native validated |
| Host syscall service | CI_VALIDATED | `SYSCALL` remains outside generated x64; null/handled/unsupported/fault outcomes have deterministic accounting |
| `SetupThread` HLE `0x3c` | CI_VALIDATED | Explicit-stack mode records context and returns stack top in `v0`; automatic-stack remains unsupported |
| `SetupHeap` HLE `0x3d` | CI_VALIDATED | Burnout `heap_size=-1` convention resolves heap end from thread stack base; success changes only host metadata |
| Startup through `DADDU` | CI_VALIDATED | Synthetic/native path executes the post-SetupHeap call chain through `SD`, enters `0x00114ed0`, executes `ADDIU/ADDIU/SD/DADDU`, then stops at the next synthetic boundary |
| Lawful real-ELF next-boundary diagnosis | DIAGNOSTIC_VALIDATED | Fresh local out-of-repository diagnosis executes the established prefix through `DADDU @ 0x00114edc` and identifies the next current unsupported instruction as `SW @ 0x00114ee0`; this is not `EXTERNALLY_VALIDATED` Windows-native execution |
| Static/binary recompiler | IN_PROGRESS | Current concrete next ISA target is `SW @ 0x00114ee0`; subsequent real boundaries will be measured iteratively |
| Graphics | TODO | No game GS/rendering path yet |
| Audio | TODO | No game IOP/SPU2 audio path yet |
| Input | TODO | No game input path yet |
| Game initialization | TODO | Startup execution has not reached full game initialization |
| Menu/frontend | TODO | Blocked by game initialization / rendering |
| Test race / gameplay | TODO | Game does not boot or reach gameplay yet |

## R5900 DADDU / Add64 v0

### Scope

This milestone implements the minimum 64-bit integer addition required by the first real unsupported instruction after the SD startup milestone.

`DADDU rd, rs, rt` is modeled as:

```text
result = (GPR[rs].low64 + GPR[rt].low64) mod 2^64
```

The instruction does not trap on overflow. A nonzero `rd` replaces only `GPR[rd].low64`; `GPR[rd].high64` is preserved. `rd==0` lowers to a provenance-carrying `Nop`, consistent with the existing zero-register policy.

`Add64` validation is deliberately narrower than `AddWordSignExtend`: exactly one GPR destination, `Low64PreserveUpper64`, and exactly two GPR inputs. Immediate and FPR sources are rejected.

### Synthetic/native startup acceptance

The dispatcher regression uses only public ISA encodings and synthetic instruction words. It models the established state on entry to `0x00114ed0` and executes:

```text
0x00114ed0  ADDIU sp,sp,-0x50
0x00114ed4  ADDIU v0,zero,1
0x00114ed8  SD    ra,0x40(sp)
0x00114edc  DADDU a0,sp,zero
0x00114ee0  synthetic unsupported sentinel
```

Acceptance state:

```text
initial sp                  0x01fffff0
initial ra                  0x00115118
sp after frame allocation   0x01ffffa0
v0.low64                    0x0000000000000001
a0.low64                    0x0000000001ffffa0
mem64[0x01ffffe0]           0x0000000000115118
selected guest instructions 4
next boundary               0x00114ee0
```

The same Windows test also executes a separate synthetic `DADDU + J + NOP` block twice. The first pass compiles/caches it; the second pass is a cache hit and fast-cache hit with no recompilation, while preserving the destination high64 half.

### Current Windows CI evidence

Implementation/test head before documentation:

`709c7cb0caf981f09985831b5f570d0d9df6b5ad`

Windows CI:

```text
run   34067189210 (#721)
host  Windows Server 2022
CTest 60/60 PASS
```

The run passed the complete suite plus frame pacing telemetry, pacing-probe smoke, analyzer package validation and pacing-probe package validation.

### TDD / branch evidence

The milestone is implemented on isolated branch `design/r5900-daddu-v0`.

Representative RED/GREEN commits include:

```text
f1534b2b76074ff91ddd8bbeb7bc6e1bed410b6c  test: specify R5900 Add64 lowering
caddfe8ee0445a0a173afb33146e44429e5585b9  feat: lower R5900 DADDU to Add64
a9701cc19681d2cda15b408b66d227af905c474d  test: specify R5900 Add64 execution
b7c361291f25303a754f9275ae11e941f0f13521  feat: execute Add64 in R5900 IR
f95bfcf7528280008a4819541d96ecfbdd51f905  test: specify native R5900 Add64 execution
7c92bf404e9553fab7ce04988a083f379c6f5a99  feat: emit R5900 Add64 on x64
2e4758b1ecf69be331111dcc5cb4b14a19cee057  test: specify R5900 DADDU dispatcher startup
709c7cb0caf981f09985831b5f570d0d9df6b5ad  feat: execute R5900 DADDU through dispatcher
```

No proprietary Burnout 3 data was added to tests or production code.

## R5900 SD / Store64 + EE Main RAM v0

### Scope

This milestone adds the minimum physical EE RAM and R5900 store support required by the first real stack prologue after `SetupHeap`.

EE RAM v0:

```text
base          0x00000000
size          0x02000000
exclusive end 0x02000000
```

`Ps2MemoryMap::regions()` still describes only ELF PT_LOAD metadata. `translate()` represents runtime EE RAM availability, so heap/stack addresses outside PT_LOAD are now backed and initially zero-filled.

`SD rt, imm(rs)`:

```text
address = uint32(low32(GPR[rs].low64) + sign_extend16(imm))
value   = GPR[rt].low64
```

The address must be 8-byte aligned. Misalignment, a missing callback, or rejected memory access returns `MemoryAccessFailure` with the exact guest PC, effective address, and width `8`. Later guest instructions do not execute after the failing store.

### Synthetic/native startup acceptance

The CI regression uses public ISA encodings at the real startup PCs:

```text
0x001001e8  JAL   0x00115108
0x001001ec  NOP
0x00115108  ADDIU sp,sp,-16
0x0011510c  SD    ra,0(sp)
0x00115110  JAL   0x00114ed0
0x00115114  NOP
```

Acceptance state:

```text
initial sp                  0x02000000
first JAL link              0x001001f0
sp after prologue           0x01fffff0
mem64[0x01fffff0]           0x00000000001001f0
final RA                    0x00115118
next_pc                     0x00114ed0
native blocks               2
selected guest instructions 6
```

The test also proves the adjacent stack doubleword is unchanged. Re-running through the same dispatcher produces two cache hits / two fast-cache hits and no recompilation.

### Current Windows CI evidence

Implementation/test head before documentation:

`35235fedf14dc1f0f1997500bcf575b16f9130b8`

Windows CI:

```text
run   34061027697 (#701)
job   101561461996
host  Windows Server 2022
MSVC  19.44 / Visual Studio 2022
CTest 59/59 PASS
```

Focused gates that passed:

```text
r5900_ir_store64_tests
r5900_ir_store64_executor_tests
r5900_x64_store64_windows_tests
r5900_block_dispatcher_store64_windows_tests
r5900_block_dispatcher_sd_startup_windows_tests
```

The same run also passed the complete legacy suite, frame pacing, pacing-probe smoke, analyzer package validation and pacing-probe package validation.

Frame-pacing telemetry:

```text
samples       240
mean          8.333 ms
min           8.330 ms
max           8.333 ms
P50/P95/P99   8.333 ms
>9/10/12 ms   0 / 0 / 0
high-res timer YES
```

One-second probe:

```text
target         120 Hz
frames         120
mean           8.333 ms
min            8.331 ms
max            8.333 ms
>9/10/12 ms    0 / 0 / 0
high-res timer YES
```

Hosted-runner timing is smoke evidence, not physical-desktop performance certification.

### TDD / branch evidence

The SD milestone was implemented on isolated branch `design/r5900-sd-v0`.

Representative task commits include:

```text
07e0c3750501890e7170c2b48dacf3194503bab3  test: specify EE main RAM backing contract
8b6939746cc91e15993236265124a23fe3ee9f0c  feat: back 32 MiB EE main RAM
88c1a12a24e288caf1d35364a928fd15569015f8  test: specify dispatcher SD execution contract
a20ca61a1e45d8baa3e9ff9ca07b94378d565415  feat: execute R5900 SD through dispatcher
35235fedf14dc1f0f1997500bcf575b16f9130b8  test: cross Burnout startup prologue through SD
```

The complete branch history retains the intermediate RED/GREEN CI gates. No proprietary ELF bytes/assets are present in the branch delta.

## Lawful real-ELF diagnostic after DADDU

The user's legally supplied Burnout 3 ELF remains outside the repository. A fresh throwaway local diagnostic, using the already established state on entry to `0x00114ed0`, advances the current modeled prefix through `DADDU` without copying game instruction words into the repository.

Established incoming state:

```text
sp = 0x01fffff0
ra = 0x00115118
```

Result after the currently supported prefix:

```text
sp                  = 0x01ffffa0
v0.low64            = 0x0000000000000001
a0.low64            = 0x0000000001ffffa0
mem64[0x01ffffe0]   = 0x0000000000115118
```

The first current unsupported instruction after that prefix is:

```text
PC   0x00114ee0
ISA  SW v0,0x28(sp)
```

The project decoder already recognizes `SW` as a 32-bit store. The current IR/executor/x64/dispatcher execution subset does not implement it yet, so `SW` is the next concrete ISA milestone.

This is lawful external diagnostic evidence only. It is **not** a claim that the expanded `SetupThread -> SetupHeap -> SD -> DADDU` path has been executed end-to-end by the external Windows-native harness.

## Existing startup evidence

The legally supplied ELF has entry point `0x00100008`. Existing project evidence establishes:

- real BSS clear `0x004e2680..0x01ecea00`;
- 1,698,872 `SQ` iterations;
- `SetupThread` syscall at `0x001001c8`, selector `0x3c`;
- `SetupHeap` syscall at `0x001001e4`, selector `0x3d`;
- Burnout heap arguments `heap_start=0x01ecea00`, `heap_size=0xffffffff`;
- production SetupHeap context resolves `heap_end=0x01ff0000`.

The older optional external Windows harness validates the real BSS-clear path through `SetupThread`. Extending and executing that harness through SetupHeap/SD/DADDU remains a separate external-validation gate.

## Test Build 0.1 gates

Test Build 0.1 is **not complete**. Remaining high-level gates include:

1. implement the next empirically observed R5900 boundary, currently `SW @ 0x00114ee0`, then repeat the legal-ELF diagnostic cycle;
2. extend the external Windows-native startup harness through the newer SetupHeap/SD/DADDU path without committing game data;
3. visually validate the Win32 executable on a physical Windows 10/11 desktop;
4. run `Burnout3PacingProbe --seconds 60 --output <report>` (or longer) on a normal physical desktop;
5. continue kernel/HLE and EE instruction coverage only as required by the real executable;
6. implement the later graphics/GS/VU, IOP/audio, input and game-initialization paths required for the first rendered frame and gameplay.

## Integration policy

- `main` must not be changed implicitly.
- PR #22 must not be merged implicitly.
- Integration of `design/r5900-daddu-v0` into `feature/r5900-or-v0` requires explicit user authorization after final branch verification.
- Never commit proprietary Burnout 3 data.
- Never claim boot/playability without direct evidence.
