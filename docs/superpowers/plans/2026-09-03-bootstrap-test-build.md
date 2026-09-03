# Burnout 3 Recompiled Test Build 0.1 Bootstrap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the minimal Windows-native runtime scaffold with a stable 120 Hz pacing foundation, diagnostics, tests, and documentation.

**Architecture:** Keep deterministic frame scheduling/statistics in portable C++20 code and isolate Win32 dependencies behind `src/platform/windows`. The Windows pacer uses QueryPerformanceCounter plus a waitable timer and a short spin phase.

**Tech Stack:** C++20, CMake 3.25+, Win32 API, DbgHelp, CTest, Visual Studio 2022.

**Spec:** `docs/superpowers/specs/2026-09-03-bootstrap-test-build-design.md`

## Global Constraints

- Runtime target: Windows 10/11 64-bit, x86-64.
- Native build only; no PCSX2 runtime dependency.
- Target cadence: 120.000 FPS / 8.333333 ms.
- Do not bundle proprietary Burnout 3 assets.
- Missing game functions/subsystems must remain explicit.

---

### Task 1: Portable frame scheduling core

**Files:**
- Create: `src/core/frame_schedule.h`
- Create: `src/core/frame_schedule.cpp`
- Create: `tests/frame_schedule_tests.cpp`

**Interfaces:**
- Produces: `FrameSchedule::period_seconds()`, `FrameSchedule::next_deadline(double now)`, `FrameSchedule::advance_after_frame(double now)`.

- [x] Write tests for 120 Hz period, monotonic deadlines, and missed-frame recovery.
- [x] Configure CMake test target and confirm RED because the production API does not exist.
- [x] Implement the minimal scheduling core.
- [x] Run tests and confirm GREEN.

### Task 2: Frame statistics

**Files:**
- Create: `src/core/frame_stats.h`
- Create: `src/core/frame_stats.cpp`
- Create: `tests/frame_stats_tests.cpp`

**Interfaces:**
- Produces: rolling `fps`, `frame_time_ms`, `simulation_time_ms`, `render_time_ms`, `simulation_steps` snapshot.

- [x] Write statistics tests first and confirm RED.
- [x] Implement the minimal accumulator/snapshot API.
- [x] Run the complete test suite and confirm GREEN.

### Task 3: Windows runtime bootstrap

**Files:**
- Create: `src/platform/windows/win_main.cpp`
- Create: `src/platform/windows/win32_window.h/.cpp`
- Create: `src/platform/windows/qpc_clock.h/.cpp`
- Create: `src/platform/windows/windows_frame_pacer.h/.cpp`
- Create: `src/debug/log.h/.cpp`
- Create: `src/platform/windows/crash_handler.h/.cpp`

**Interfaces:**
- Consumes: `FrameSchedule`, `FrameStats`.
- Produces: `Burnout3Recompiled_Test` Win32 executable.

- [x] Add Windows-only CMake target and libraries (`dbghelp`, `winmm`).
- [x] Implement structured logging and fatal-error paths.
- [x] Implement crash filter + minidump attempt.
- [x] Implement Win32 window/message pump.
- [x] Implement QPC clock + high-resolution waitable-timer/spin pacer.
- [x] Run host CMake/tests; record that Win32 compilation is pending because Windows SDK/MSVC is unavailable in this environment.

### Task 4: Documentation and validation status

**Files:**
- Create: `README.md`
- Create: `docs/ARCHITECTURE.md`
- Create: `docs/PROGRESS.md`
- Create: `docs/RECOMPILATION.md`
- Create: `docs/PS2_MEMORY.md`
- Create: `docs/GRAPHICS.md`
- Create: `docs/FUNCTION_MAP.md`
- Create: `docs/function_map.csv`

- [x] Document build/run instructions and external game-data policy.
- [x] Mark only host-verified pieces DONE; mark Windows-only pieces READY_FOR_WINDOWS_VALIDATION.
- [x] Run configure/build/CTest from a clean build directory.
- [x] Commit the coherent bootstrap scaffold.
