# Burnout 3 Recompiled

Experimental native Windows x86-64 recompilation/port project for **Burnout 3: Takedown**.

## Current milestone

`Burnout 3 Recompiled - Test Build 0.1` now has native R5900 startup execution through the first real post-`SetupHeap` stack prologue store. The modeled path includes the real startup-shaped sequence:

```text
SetupThread @ 0x001001c8
SetupHeap   @ 0x001001e4
JAL         0x00115108
ADDIU       sp,sp,-16
SD          ra,0(sp)
JAL         0x00114ed0
```

The project still does **not** boot the game. The next real unsupported guest instruction identified from the legally supplied ELF is `DADDU a0,sp,zero` at `0x00114edc` (raw `0x03a0202d`). That boundary is diagnostic evidence from the external lawful ELF, not a claim that the expanded path has been externally native-validated on Windows.

## Current runtime/recompiler capabilities

- C++20 / CMake / Visual Studio 2022 Windows x64 project;
- native Win32 bootstrap, logging, crash/minidump support and 120 Hz QPC-based frame pacing;
- PS2 ELF32 little-endian MIPS loading and conservative control-flow analysis;
- **32 MiB EE main RAM backing** for `0x00000000..0x01ffffff`, zero-filled outside loaded data;
- `Ps2MemoryMap::regions()` remains ELF `PT_LOAD` metadata only, while `translate()` represents physical EE RAM availability;
- little-endian typed 8/16/32/64/128-bit reads/writes;
- R5900 decoder and startup IR for the currently required EE/MMI/COP1 subset;
- `SQ` lowered as `Store128` and `SD` lowered as `Store64`;
- interpreted `Store64` with exact low64 source semantics, 32-bit effective-address wrap, required 8-byte alignment and deterministic width-8 memory-fault provenance;
- Windows x86-64 native `Store64` and `Store128` helper paths using the shared execution context;
- ordinary `BEQ`/`BNE`, branch-likely `BEQL`/`BNEL`, direct `J`/`JAL`, indirect `JR`/`JALR` and architectural delay slots;
- cached native blocks with byte-exact guest-word verification and fast replay;
- a boundary-prefix rule that prevents supported transfers after an unsupported instruction from executing prematurely;
- an injectable host-syscall boundary outside generated x64;
- production explicit-stack `SetupThread` (`0x3c`) and main-thread `SetupHeap` (`0x3d`) HLE;
- deterministic runtime `MemoryAccessFailure` propagation with exact guest-PC/address/width accounting.

### `SD / Store64` contract

For `SD rt, imm(rs)`:

```text
address = uint32(low32(GPR[rs].low64) + sign_extend16(imm))
value   = GPR[rt].low64
```

The address must be 8-byte aligned. Unlike the existing `SQ` v0 path, `SD` is **not** rounded down. A failed store stops before later guest instructions and reports width `8` at the exact guest PC/address.

The startup-shaped native dispatcher regression proves:

```text
initial sp                  0x02000000
first JAL return address    0x001001f0
sp after ADDIU              0x01fffff0
mem64[0x01fffff0]           0x00000000001001f0
final RA after second JAL   0x00115118
next_pc                     0x00114ed0
```

It also verifies that the adjacent eight stack bytes remain unchanged and that a second execution fast-replays both compiled blocks without recompilation.

## Validation status

Current SD/EE-RAM implementation head before documentation: `35235fedf14dc1f0f1997500bcf575b16f9130b8`.

Windows CI run `34061027697`, job `101561461996`, on Windows Server 2022 / MSVC 19.44 completed successfully with:

- **59/59 CTest passed**;
- `r5900_ir_store64_tests` passed;
- `r5900_ir_store64_executor_tests` passed;
- `r5900_x64_store64_windows_tests` passed;
- `r5900_block_dispatcher_store64_windows_tests` passed;
- `r5900_block_dispatcher_sd_startup_windows_tests` passed;
- frame-pacing telemetry passed: 240 samples, 8.333 ms mean, 0 samples above 9/10/12 ms;
- one-second pacing probe passed: 120/120 frames at 120 Hz, 8.333 ms mean;
- analyzer and pacing-probe package validation passed.

Hosted CI timing is smoke evidence only; physical Windows desktop pacing validation remains required.

## Lawful external ELF evidence

No proprietary Burnout 3 executable or assets are committed to this repository. Out-of-repository inspection of the user-supplied legal ELF confirms the startup path through `SetupThread`, `SetupHeap`, the two calls above, and the stack stores.

At the second call target, the real code begins:

```text
0x00114ed0  ADDIU sp,sp,-0x50
0x00114ed4  ADDIU v0,zero,1
0x00114ed8  SD    ra,0x40(sp)
0x00114edc  DADDU a0,sp,zero   <- next unsupported boundary
```

Entering `0x00114ed0` with `sp=0x01fffff0` and `ra=0x00115118`, the currently modeled prefix would produce:

```text
sp                  0x01ffffa0
v0                  0x0000000000000001
mem64[0x01ffffe0]   0x0000000000115118
```

This establishes the **next implementation target: R5900 `DADDU`**. It does not upgrade the expanded startup path to `EXTERNALLY_VALIDATED`; the existing Windows external-ELF harness still requires extension and execution through the newer HLE/SD path.

## External startup validation harness

The Windows startup dispatcher test can optionally consume a user-supplied ELF locally:

```powershell
.\build\Release\r5900_block_dispatcher_startup_windows_tests.exe "D:\Games\Burnout3\SLUS_210.50"
```

Its existing external mode validates the full real BSS clear through the `SetupThread` boundary and does not commit or upload the supplied game file. The newer `SetupHeap`/`SD` path is currently covered by synthetic/native CI plus the lawful local diagnostic described above, not by a completed external Windows-native run.

## Analyze an external PS2 ELF

After a Release build:

```powershell
Burnout3Analyze.exe --elf "D:\Games\Burnout3\SLUS_210.50" --output "burnout3-analysis.txt"
```

Console output:

```powershell
Burnout3Analyze.exe --elf "D:\Games\Burnout3\SLUS_210.50"
```

The analyzer performs static analysis only. It does not emulate a PS2 or execute the game.

## Build on Windows 10/11 x64

Requirements:

- Visual Studio 2022 with Desktop development with C++;
- CMake 3.25+;
- Windows 10/11 SDK.

Debug:

```powershell
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug
```

Release:

```powershell
cmake --preset vs2022-release
cmake --build --preset vs2022-release
ctest --preset vs2022-release
```

## Runtime bootstrap options

`Burnout3Recompiled_Test.exe` currently accepts:

```text
--debug
--verbose
--windowed
--fullscreen
--game-data <path>
--log-level <level>
--disable-audio
--frame-stats
```

Some flags are accepted before their corresponding subsystem exists. Missing functionality is documented rather than silently simulated.

## 120 FPS policy

The target presentation cadence is exactly **120.000 FPS** (`8.333333 ms` per frame). The current bootstrap validates schedule/pacing infrastructure only. The original game's simulation rate is not assumed; simulation timing will be chosen from runtime evidence.

## Legal data policy

No proprietary Burnout 3 executable, assets, audio, textures, symbols, dumps, or game data are included in this repository. Game-data analysis and external validation use files supplied externally by the owner from a legally obtained copy. Do not commit those files.

See `docs/PROGRESS.md` for the authoritative current engineering status and evidence.