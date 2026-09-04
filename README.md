# Burnout 3 Recompiled

Experimental native Windows x86-64 recompilation/port project for **Burnout 3: Takedown**.

## Current milestone

`Burnout 3 Recompiled - Test Build 0.1` bootstrap and static-analysis infrastructure.

The current source tree contains:

- C++20/CMake project structure;
- native Win32 window bootstrap;
- structured logging and Windows minidump plumbing;
- QueryPerformanceCounter clock and 120 Hz Windows frame pacer;
- validated PS2 ELF32/MIPS structural loading;
- PT_LOAD-backed guest memory mapping;
- an initial R5900 decoder, now including the narrow EE/MMI/COP1 startup subset `SYNC`, `MTSAH`, `MTHI1`, `MTLO1`, `PADDUW`, `MTC1`, `CTC1`, and `ADDA.S` identified from an externally supplied legal game ELF;
- initial provenance-carrying R5900 IR v0 with explicit lowering for NOP, ADDU, ADDIU, and ORI;
- deterministic R5900 IR reference execution for the current `Nop`, `AddWordSignExtend`, and `Or64` subset;
- an initial Windows x86-64 machine-code backend for the same IR subset, with executable-page ownership and W^X allocation/protection;
- an initial Windows R5900 native block dispatcher that analyzes straight-line guest prefixes, compiles them on demand, executes them through the x86-64 backend, applies a block budget, caches native blocks by guest PC, and rejects stale cache entries after guest-code changes;
- differential Windows tests comparing native x86-64 execution against the reference executor, including synthetic decoder -> IR -> native and analyzer -> dispatcher -> native execution paths;
- conservative basic-block and reachable-CFG analysis;
- deterministic analysis reports;
- `Burnout3Analyze`, a console tool for analyzing an externally supplied PS2 ELF without executing guest code;
- portable/unit tests plus Windows-specific integration tests;
- Windows CI pinned to Visual Studio 2022.

The initial IR has a deterministic reference executor for `Nop`, `AddWordSignExtend`, and `Or64`. It models all 32 EE GPRs as 128-bit values split into low/high 64-bit halves, preserves upper halves for current integer writes, enforces GPR zero, and rejects malformed IR explicitly.

A first Windows x86-64 backend now compiles that same IR subset into callable native machine code. The generated blocks operate on the explicit EE GPR state, preserve `high64` for the current integer writes, normalize GPR zero, support full 64-bit `Or64` immediates, and are differentially checked against the reference executor. Executable memory is allocated writable, populated, changed to execute/read, and instruction-cache-flushed before execution.

On Windows, `R5900BlockDispatcher` now bridges the existing basic-block analyzer to the current lowering and x86-64 backend for straight-line NOP/ADDU/ADDIU/ORI prefixes. It supports bounded multi-block sequential dispatch, per-run progress/cache accounting, exact guest-word cache validation using a deterministic FNV-1a fingerprint plus byte-exact word comparison, automatic recompilation of changed supported code, and fail-fast stops before control flow, traps, or unsupported instructions. Architectural delay slots are deliberately not executed by dispatcher v0.

An externally supplied legal Burnout 3 ELF has now been used out-of-repository to identify the first real startup decoder blockers. The narrow startup decoder extension is CI-validated, but the project **does not yet execute those real startup instructions natively**: the IR/backend still lack the required MMI/COP1/special-register semantics, guest loads/stores, native branch/jump/call handling and delay slots. The game does not boot and graphics, audio, input, menus, and gameplay remain unimplemented.

## Legal data policy

No proprietary Burnout 3 executable, assets, audio, textures, symbols, dumps, or game data are included in this repository. Game-data analysis uses files supplied externally by the owner from a legally obtained copy. Do not commit those files.

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
