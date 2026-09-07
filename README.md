# Burnout 3 Recompiled

Experimental native Windows x86-64 recompilation/port project for **Burnout 3: Takedown**.

## Current milestone

`Burnout 3 Recompiled - Test Build 0.1` now has native R5900 startup execution through the first real post-`SetupHeap` 32-bit stack stores.

The modeled startup path includes:

```text
SetupThread @ 0x001001c8
SetupHeap   @ 0x001001e4
JAL         0x00115108
ADDIU       sp,sp,-16
SD          ra,0(sp)
JAL         0x00114ed0
ADDIU       sp,sp,-0x50
ADDIU       v0,zero,1
SD          ra,0x40(sp)
DADDU       a0,sp,zero
SW          v0,0x28(sp)
```

The project still does **not** boot the game.

A lawful out-of-repository diagnostic of the user-supplied ELF shows that real execution continues through additional supported `SW` stores and a `JAL`, then reaches the next current host boundary:

```text
0x0010be24  SYSCALL   selector 0x40 in v1
```

That observation is **DIAGNOSTIC_VALIDATED** only. It is not a claim that this expanded path has been executed end-to-end by the external Windows-native ELF harness.

## Current runtime/recompiler capabilities

- C++20 / CMake / Visual Studio 2022 Windows x64 project;
- native Win32 bootstrap, logging, crash/minidump support and QPC-based 120 Hz pacing infrastructure;
- PS2 ELF32 little-endian MIPS loading and conservative control-flow analysis;
- 32 MiB EE main RAM backing for `0x00000000..0x01ffffff`;
- typed little-endian 8/16/32/64/128-bit guest-memory access;
- incremental R5900 decoder/IR/reference-executor/x64 backend for the startup subset;
- `SQ -> Store128`, `SD -> Store64`, `SW -> Store32`;
- native/control-flow support for the currently required `BEQ/BNE`, `BEQL/BNEL`, `J/JAL`, `JR/JALR` and architectural delay slots;
- cached native blocks with exact guest-word validation and fast replay;
- boundary-prefix protection so a later transfer cannot execute past an earlier unsupported/faulting instruction;
- injectable host-syscall boundary outside generated x64;
- `SetupThread` (`0x3c`) and `SetupHeap` (`0x3d`) HLE;
- exact guest-PC/address/width memory-fault provenance.

## `SW / Store32` contract

For `SW rt, imm(rs)`:

```text
base32  = low32(GPR[rs].low64)
offset  = sign_extend16(imm)
address = uint32(base32 + offset)
value   = low32(GPR[rt].low64)
```

Properties:

- address arithmetic wraps modulo 2^32;
- the address must be **4-byte aligned**;
- there is no alignment-down;
- exactly the source low32 is written;
- source high bits are ignored;
- CPU register state is not modified by the store;
- failed stores stop later guest instructions;
- failure reports the exact guest PC/effective address with width `4`.

The dispatcher wires `write32` in both cold/exact-cache and fast-cache execution contexts. Faulting native code remains cacheable/reusable; changing guest register state from a bad address to a good one does not force recompilation.

## Startup-shaped SW acceptance

Synthetic/native CI crosses the real first-SW PC using only public ISA encodings and synthetic data:

```text
0x00114ed0  ADDIU sp,sp,-0x50
0x00114ed4  ADDIU v0,zero,1
0x00114ed8  SD    ra,0x40(sp)
0x00114edc  DADDU a0,sp,zero
0x00114ee0  SW    v0,0x28(sp)
```

Acceptance state:

```text
initial sp                0x01fffff0
initial ra                0x00115118
sp after frame allocation 0x01ffffa0
v0.low64                  0x0000000000000001
a0.low64                  0x0000000001ffffa0
mem64[0x01ffffe0]         0x0000000000115118
mem32[0x01ffffc8]         0x00000001
```

The test also verifies adjacent 32-bit guards remain unchanged and separately proves a cached `SW + J + NOP` block fast-replays without recompilation.

## Validation status

Implementation/test head before this documentation update:

```text
491385fc6e12f7f7d1189948daafe5e89d0bfbb2
```

Windows CI run **#747** (`34073509755`) on Windows Server 2022 / Visual Studio 2022 / MSVC 19.44 passed:

- **65/65 CTest**;
- `r5900_ir_store32_tests`;
- `r5900_ir_store32_executor_tests`;
- `r5900_x64_store32_windows_tests`;
- `r5900_block_dispatcher_store32_windows_tests`;
- `r5900_block_dispatcher_sw_startup_windows_tests`;
- all Store64/Store128 and legacy regression gates;
- 240-sample frame-pacing telemetry with 8.333 ms mean and no sample above 9/10/12 ms;
- one-second pacing probe: 120/120 frames at 120 Hz, 8.333 ms mean;
- analyzer package validation;
- pacing-probe package validation.

Hosted-runner timing is smoke evidence only; it is not physical-desktop 120 FPS certification.

## Lawful external ELF evidence

No proprietary Burnout 3 executable, raw instruction words, assets, audio, textures or game data are committed to this repository.

The legal ELF remains outside the repository. Starting from the established state at `0x00114ed0`, the diagnostic confirms the supported model produces:

```text
sp.low64                0x01ffffa0
v0.low64                0x0000000000000001
a0.low64                0x0000000001ffffa0
mem64[0x01ffffe0]       0x0000000000115118
mem32[0x01ffffc8]       0x00000001
```

Real execution then continues through more `SW` stores and a direct call. At the next current host boundary:

```text
PC          0x0010be24
instruction SYSCALL
selector    0x40 in v1
```

Relevant derived state at that boundary includes:

```text
sp.low64  0x01ffffa0
v0.low64  0x0000000000000001
a0.low64  0x0000000001ffffa0
ra.low64  0x0000000000114ef4
v1.low64  0x0000000000000040
```

The current host syscall service implements selectors `0x3c` (`SetupThread`) and `0x3d` (`SetupHeap`); selector `0x40` is therefore the next concrete startup/HLE boundary to design. No semantics are assumed yet.

## External startup validation harness

The optional Windows startup dispatcher test can consume a user-supplied ELF locally:

```powershell
.\build\Release\r5900_block_dispatcher_startup_windows_tests.exe "D:\Games\Burnout3\SLUS_210.50"
```

Its historical external mode validates the real BSS-clear path through `SetupThread`. The newer `SetupHeap -> SD -> DADDU -> SW` path is currently covered by synthetic/native CI plus lawful local diagnosis, not by a completed external Windows-native run.

## Analyze an external PS2 ELF

After a Release build:

```powershell
Burnout3Analyze.exe --elf "D:\Games\Burnout3\SLUS_210.50" --output "burnout3-analysis.txt"
```

The analyzer performs static analysis only. It does not emulate a PS2 or execute the game.

## Build on Windows 10/11 x64

Requirements:

- Visual Studio 2022 with Desktop development with C++;
- CMake 3.25+;
- Windows 10/11 SDK.

Release example:

```powershell
cmake --preset vs2022-release
cmake --build --preset vs2022-release
ctest --preset vs2022-release
```

## 120 FPS policy

The target presentation cadence is exactly **120.000 FPS** (`8.333333 ms` per frame). Current CI validates pacing infrastructure only. The original game's simulation rate is not assumed; simulation timing will be chosen from runtime evidence.

## Legal data policy

Only public ISA encodings and synthetic fixtures belong in the repository. Game-data analysis and external validation use files supplied externally by the owner from a legally obtained copy. Do not commit those files.

See `docs/PROGRESS.md` for the authoritative engineering snapshot.