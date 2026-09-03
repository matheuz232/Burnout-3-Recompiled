# Burnout 3 Recompiled

Experimental native Windows x86-64 recompilation/port project for **Burnout 3: Takedown**.

## Current milestone

`Burnout 3 Recompiled - Test Build 0.1` bootstrap infrastructure.

The current source tree contains:

- C++20/CMake project structure;
- native Win32 window bootstrap;
- structured logging;
- Windows unhandled-exception/minidump handler;
- QueryPerformanceCounter clock;
- high-resolution waitable-timer + spin 120 Hz frame pacer;
- 120-sample frame statistics;
- command-line option parsing;
- portable unit tests plus Windows-only QPC/pacer integration tests;
- Windows CI definition for Visual Studio 2022.

It **does not yet** contain PS2 ELF loading, MIPS recompilation, graphics, audio, input, game initialization, menu, or gameplay.

## Legal data policy

No proprietary Burnout 3 executable, assets, audio, textures, symbols, dumps, or game data are included in this repository. Future game-data loading must use files supplied externally by the owner from a legally obtained copy.

Example future/current bootstrap invocation:

```powershell
Burnout3Recompiled_Test.exe --windowed --frame-stats --game-data "D:\Games\Burnout3\data"
```

At this milestone, `--game-data` is accepted and logged but the data is not parsed yet.

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

The Visual Studio multi-config executable is normally under:

```text
build/vs2022-debug/Debug/Burnout3Recompiled_Test.exe
```

## Runtime options

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

This is only the presentation/frame-pacing foundation. The original game's simulation rate is **not assumed** to be 30 or 60 Hz; simulation timing will be chosen only after binary/runtime analysis.

See `docs/PROGRESS.md` for the authoritative status.
