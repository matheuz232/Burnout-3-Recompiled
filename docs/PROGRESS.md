# Progress

Status date: 2026-09-03

Completion rule: `implemented -> compiled -> executed/tested -> validated`.

| Component | Status | Evidence / next gate |
|---|---|---|
| Repository/CMake bootstrap | DONE | Clean host configure/build succeeds with GCC and Clang |
| Portable 120 Hz frame schedule | DONE | Deterministic unit tests pass |
| Frame statistics | DONE | Unit tests pass |
| Runtime option parser | DONE | Unit tests pass |
| Structured logging | DONE | Unit tests pass |
| Win32 executable target | READY_FOR_WINDOWS_VALIDATION | Source/CMake present; requires MSVC + Windows SDK build |
| Win32 window | READY_FOR_WINDOWS_VALIDATION | Source present; must be executed on Windows |
| QPC high-resolution clock | READY_FOR_WINDOWS_VALIDATION | Windows integration test present |
| 120 FPS Windows frame pacer | READY_FOR_WINDOWS_VALIDATION | Waitable timer + spin implemented; Windows pacing test present |
| Crash handler/minidump | READY_FOR_WINDOWS_VALIDATION | Source present; requires controlled Windows crash test |
| PS2 ELF loader | TODO | Next major milestone after bootstrap Windows validation |
| PS2 memory mapping | TODO | Requires executable analysis |
| MIPS decoder | TODO | Requires executable analysis |
| Static/binary recompiler | TODO | Strategy intentionally not selected yet |
| Graphics | TODO | No D3D initialization yet |
| Audio | TODO | No XAudio2 initialization yet |
| Input | TODO | No keyboard/XInput layer yet |
| Game initialization | TODO | No game code translated yet |
| Menu/frontend | TODO | Blocked by prior milestones |
| Test race | TODO | Blocked by prior milestones |

## Test Build 0.1 gate

The bootstrap is **not yet a completed Test Build 0.1** because the actual Windows executable has not been compiled and executed in this environment. The next gate is a Windows 10/11 x64 Visual Studio 2022 build followed by CTest and a runtime pacing capture.
