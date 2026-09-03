# Architecture

## Current bootstrap runtime

```text
Burnout3Recompiled_Test
  -> RuntimeOptions
  -> Log
  -> CrashHandler (Windows)
  -> Win32Window (Windows)
  -> QpcClock (Windows)
  -> WindowsFramePacer
       -> FrameSchedule (portable)
  -> FrameStats (portable)
```

## Current static-analysis pipeline

```text
Burnout3Analyze
  -> Burnout3AnalyzeOptions
  -> Burnout3AnalyzeApp
       -> Ps2ElfAnalysis
            -> Ps2ElfImage
            -> Ps2MemoryMap
            -> R5900 decoder
            -> basic-block analysis
            -> reachable CFG
            -> deterministic analysis report
```

`Burnout3Analyze` reads externally supplied files only. The executable and the analysis libraries never require proprietary game data to be present in the repository.

### `src/core/`

Portable deterministic runtime primitives. These are deliberately independent from Win32 so timing math can be tested on any development host.

### `src/platform/windows/`

Windows 10/11 x64 process/window/timer/crash glue. No cross-platform windowing or graphics abstraction is introduced because the shipping target is Windows only.

### `src/debug/`

Structured logging and explicit stub diagnostics.

### `src/recompiler/`

Guest executable parsing and instruction-level R5900 decoding primitives. Despite the directory name, native code generation does not exist yet; unsupported instruction families remain explicit rather than being emulated silently.

### `src/runtime/`

Guest-runtime state independent from Win32. `Ps2MemoryMap` centralizes PS2 virtual-address translation into owned native backing for validated ELF load regions. Full PS2 hardware address-space modeling remains evidence-driven.

### `src/analysis/`

Read-only static-analysis stages. They build conservative basic blocks, reachable control flow and deterministic reports. They do not execute guest instructions, infer register-indirect destinations, or assign unsupported semantics.

`Ps2ElfAnalysis` is the composition boundary that turns external ELF bytes into a deterministic analysis report.

### `src/tools/`

Thin user-facing tooling around tested libraries. `Burnout3Analyze` separates command-line parsing, file I/O and the console `main`; guest-analysis semantics stay in `src/analysis/`.

## Planned boundaries

The repository will expand with focused modules rather than one monolithic translation unit:

```text
src/core/
src/platform/windows/
src/game/
src/recompiler/
src/runtime/
src/analysis/
src/tools/
src/renderer/
src/audio/
src/input/
src/assets/
src/debug/
```

The recompiler/runtime boundary must keep original PS2-address semantics explicit. Hardware compatibility layers are allowed during development, but the final architecture must favor native reconstructed/recompiled code rather than a complete PS2 emulator.

## Frame timing

`FrameSchedule` owns a monotonic deadline timeline. After every frame it advances by exact `1 / target_hz` increments. If execution misses one or more deadlines, it advances to the next future slot instead of setting `next = now + period`; this prevents long-term drift.

`WindowsFramePacer` performs:

1. coarse wait using a high-resolution waitable timer when available;
2. fallback waitable timer with 1 ms timer-resolution request if necessary;
3. short spin using QPC near the deadline;
4. schedule advancement after the observed deadline.

Simulation is intentionally absent from this milestone. Render cadence and simulation cadence are separate architectural concerns.
