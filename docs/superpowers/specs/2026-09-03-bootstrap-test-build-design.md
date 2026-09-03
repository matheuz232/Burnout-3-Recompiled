# Burnout 3 Recompiled Test Build 0.1 — Bootstrap Design

## Scope

This design covers only the first infrastructure milestone from the user specification. It does not attempt PS2 ELF analysis, MIPS translation, rendering, audio, input, or game code recompilation yet.

## Target

- Windows 10/11 x86-64 only for the shipping/runtime target.
- Visual Studio 2022 + CMake.
- C++20.
- Native Win32 process; no PCSX2 runtime dependency.
- Render/game-loop presentation target: exactly 120.000 Hz (8.333333 ms target frame interval).

## Bootstrap architecture

- `src/platform/windows/`: Win32 entry point, window creation, QPC clock, waitable-timer pacing, crash handling.
- `src/core/`: platform-independent frame schedule/statistics and application lifecycle contracts.
- `src/debug/`: structured logging and frame-stat reporting.
- `tests/`: host-portable deterministic tests for frame scheduling/math; Windows-specific runtime validation remains a Windows build step.

The frame limiter separates scheduling math from the Windows waiting mechanism. A monotonic target timeline is maintained to avoid cumulative drift. The Windows backend sleeps/waits for the coarse portion of a frame and spin-waits only near the deadline. Missed deadlines advance the schedule to the next future slot rather than accumulating delay.

## Initial executable behavior

1. Start `Burnout3Recompiled_Test`.
2. Initialize logger and crash handler.
3. Create a Win32 window.
4. Run a message pump and 120 Hz paced loop.
5. Print rolling FPS/frame-time metrics when `--frame-stats` is enabled.
6. Exit cleanly on window close/Escape.

## Error handling

Fatal bootstrap failures are logged with `[ERROR]` and return non-zero. Unhandled Windows exceptions are captured with `SetUnhandledExceptionFilter`; a minidump is attempted with DbgHelp, and diagnostic state includes the last known PS2/native execution markers (initially zero placeholders until recompilation begins).

## Testing

Deterministic tests cover target period calculation, schedule monotonicity, missed-frame recovery, and statistics calculations. The Linux host used during scaffolding builds and runs those tests. The Win32 executable itself must be compiled/run on Windows before the milestone can be marked fully DONE.

## Completion rule

A component is only marked DONE after implementation, compilation, execution/test, and validation. Because this environment lacks a Windows SDK/toolchain, Windows-only items remain `READY_FOR_WINDOWS_VALIDATION` rather than `DONE`.
