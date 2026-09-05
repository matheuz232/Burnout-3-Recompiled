# Burnout 3 Recompiled

Experimental native Windows x86-64 recompilation/port project for **Burnout 3: Takedown**.

## Current milestone

`Burnout 3 Recompiled - Test Build 0.1` bootstrap, static-analysis, and first native R5900 startup-execution infrastructure.

The current source tree contains:

- C++20/CMake project structure;
- native Win32 window bootstrap;
- structured logging and Windows minidump plumbing;
- QueryPerformanceCounter clock and 120 Hz Windows frame pacer;
- validated PS2 ELF32/MIPS structural loading;
- PT_LOAD-backed guest memory mapping;
- an R5900 decoder with the narrow EE/MMI/COP1 startup subset required by the first Burnout 3 entry-point prefix, including `SYNC`, `MTSAH`, `MTHI1`, `MTLO1`, `PADDUW`, `MTC1`, `CTC1`, and `ADDA.S`;
- provenance-carrying R5900 IR with lowering for the straight-line startup execution subset, including the existing NOP/ADDU/ADDIU/ORI path plus ANDI, LUI, special HI/LO/SA writes, PADDUW, COP1 moves/accumulator add, and SYNC semantics;
- deterministic R5900 IR reference execution over all 32 128-bit EE GPRs plus HI/LO/HI1/LO1, SA, 32 raw FPRs, FCR31, and FP accumulator state;
- a Windows x86-64 machine-code backend for the same startup subset, with executable-page ownership and W^X allocation/protection;
- a Windows R5900 native block dispatcher that analyzes straight-line guest prefixes, compiles them on demand, executes them through the x86-64 backend, applies a block budget, caches native blocks by guest PC, and rejects stale cache entries after guest-code changes;
- differential Windows tests comparing native x86-64 execution against the reference executor for both integer/MMI and COP1 state;
- a synthetic 74-instruction startup-shaped dispatcher test that executes natively from `0x00100008` to the control-flow boundary at `0x00100130` and deliberately stops before the branch/delay slot;
- an optional external-ELF validation mode for the startup dispatcher test, allowing a legally supplied `SLUS_210.50` to be tested locally without committing or uploading game data;
- conservative basic-block and reachable-CFG analysis;
- deterministic analysis reports;
- `Burnout3Analyze`, a console tool for analyzing an externally supplied PS2 ELF without executing guest code;
- portable/unit tests plus Windows-specific integration tests;
- Windows CI pinned to Visual Studio 2022.

The startup execution state models all 32 EE GPRs as 128-bit values split into low/high 64-bit halves and additionally models HI/LO/HI1/LO1, SA, raw 32-bit FPR values, FCR31, and the floating-point accumulator. Current integer write semantics preserve the modeled upper halves where required and GPR zero is normalized explicitly.

The Windows x86-64 backend emits callable native machine code for the current startup subset and is differentially checked against the reference executor. PADDUW uses alias-safe source capture and four-lane unsigned saturating addition; COP1 `MTC1`/`CTC1` preserve raw 32-bit payloads and the current `ADDA.S` implementation operates on the modeled raw single-precision values under the explicit v0 floating-point contract. Executable memory is allocated writable, populated, changed to execute/read, and instruction-cache-flushed before execution.

`R5900BlockDispatcher` bridges the existing basic-block analyzer to lowering and the x86-64 backend for the current straight-line startup subset. It supports bounded multi-block sequential dispatch, per-run progress/cache accounting, exact guest-word cache validation using a deterministic FNV-1a fingerprint plus byte-exact word comparison, automatic recompilation of changed supported code, and fail-fast stops before control flow, traps, or unsupported instructions. Architectural delay slots are deliberately not executed by dispatcher v0.

A legally supplied Burnout 3 ELF was inspected out-of-repository. Its entry point is `0x00100008`; static inspection identifies 74 supported straight-line instructions before a `BEQ` at `0x00100130`, followed by a NOP delay slot. The observed 74-instruction mix is 29 PADDUW, 32 MTC1, MTHI/MTHI1/MTLO/MTLO1/MTSAH, ADDA.S, SYNC, CTC1, 2 LUI, 2 ADDIU, and ANDI. This real-file inspection is **not** claimed as native external execution: the Windows external-ELF dispatcher path is compiled and CI-tested without game data, but the supplied ELF has not yet been run through that Windows executable in this environment.

The game still does **not** boot. Native branch/jump/call execution and architectural delay-slot semantics, guest loads/stores and broader memory behavior, syscall/HLE integration, graphics, audio, input, menus, and gameplay remain unimplemented.

## Legal data policy

No proprietary Burnout 3 executable, assets, audio, textures, symbols, dumps, or game data are included in this repository. Game-data analysis and external execution validation use files supplied externally by the owner from a legally obtained copy. Do not commit those files.

## Analyze an external PS2 ELF

After a Release build:

```powershell
Burnout3Analyze.exe --elf "D:\\Games\\Burnout3\\SLUS_210.50" --output "burnout3-analysis.txt"
```

To write the report directly to the console:

```powershell
Burnout3Analyze.exe --elf "D:\\Games\\Burnout3\\SLUS_210.50"
```

The reachable-CFG worklist is bounded. The default is 4096 blocks and can be overridden explicitly:

```powershell
Burnout3Analyze.exe --elf "D:\\Games\\Burnout3\\SLUS_210.50" --max-blocks 8192
```

This tool performs static analysis only. It does not execute PS2 instructions, emulate a PS2, infer register-indirect targets, or invoke the native x86-64 recompilation backend. See `docs/ANALYSIS_TOOL.md` for the output contract and current limitations.

## Validate the startup prefix against an external ELF

The Windows startup dispatcher test can optionally consume a user-supplied ELF. The file is read locally at runtime and is never required by CI:

```powershell
.\build\Release\r5900_block_dispatcher_startup_windows_tests.exe "D:\\Games\\Burnout3\\SLUS_210.50"
```

A successful real-file run must print:

```text
REAL_ELF_STARTUP_VALIDATED start=0x00100008 stop=0x00100130 instructions=74
```

Until that command has been executed successfully against the supplied ELF on Windows, the milestone remains **ready for external validation**, not externally native-validated.

## Build on Windows 10/11 x64

Requirements:

- Visual Studio 2022 with Desktop development with C++;
- CMake 3.25+;
- Windows 10/11 SDK.

From a Developer PowerShell:

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

The Visual Studio multi-config executables are normally under the selected configuration directory, including:

```text
Burnout3Recompiled_Test.exe
Burnout3Analyze.exe
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

Some flags are accepted before their corresponding subsystem exists. Missing functionality remains documented rather than silently simulated.

## 120 FPS policy

The target presentation cadence is exactly **120.000 FPS**, corresponding to **8.333333 ms** per frame. The current bootstrap validates schedule math independently from the Windows waiting mechanism. The Windows backend uses QPC, a waitable timer, and a short spin phase to avoid relying exclusively on `Sleep()`.

This is only the presentation/frame-pacing foundation. The original game's simulation rate is **not assumed** to be 30 or 60 Hz; simulation timing will be chosen only after binary/runtime evidence.

See `docs/PROGRESS.md` for the authoritative status.
