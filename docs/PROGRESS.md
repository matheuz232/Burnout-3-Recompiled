# Progress

Status date: 2026-09-03

Completion rule: `implemented -> compiled -> executed/tested -> validated`.

| Component | Status | Evidence / next gate |
|---|---|---|
| Repository/CMake bootstrap | DONE | Clean host configure/build succeeds with GCC and Clang; Windows CI configures with VS2022 |
| Portable 120 Hz frame schedule | DONE | Deterministic unit tests pass on host and Windows CI |
| Frame statistics | DONE | Unit tests pass on host and Windows CI |
| Runtime option parser | DONE | Unit tests pass on host and Windows CI |
| Structured logging | DONE | Unit tests pass on host and Windows CI |
| Win32 executable target | COMPILED_IN_CI | `Burnout3Recompiled_Test.exe` builds with MSVC 19.44 / Visual Studio 2022; interactive launch still required |
| Win32 window | READY_FOR_INTERACTIVE_VALIDATION | Compiles in Windows CI; must be opened/closed on Windows 10/11 |
| QPC high-resolution clock | DONE | Windows integration test executes successfully in GitHub Actions |
| 120 FPS Windows frame pacer | WORKING | Windows integration test executes successfully; next gate is tighter pacing/jitter capture on desktop Windows |
| Crash handler/minidump | READY_FOR_INTERACTIVE_VALIDATION | Compiles in Windows CI; requires controlled Windows crash test |
| PS2 ELF loader | CI_VALIDATED | Synthetic ELF32 little-endian MIPS tests pass with GCC, Clang and MSVC; next gate is a legally supplied real game ELF |
| PS2 memory mapping | TODO | Next subsystem after ELF loader is merged and tested against legally supplied executable metadata |
| MIPS decoder | TODO | Requires executable analysis |
| Static/binary recompiler | TODO | Strategy intentionally not selected yet |
| Graphics | TODO | No D3D initialization yet |
| Audio | TODO | No XAudio2 initialization yet |
| Input | TODO | No keyboard/XInput layer yet |
| Game initialization | TODO | No game code translated yet |
| Menu/frontend | TODO | Blocked by prior milestones |
| Test race | TODO | Blocked by prior milestones |

## Windows CI evidence

GitHub Actions run `33713829165` on `windows-2022` completed successfully using Visual Studio 2022 / MSVC 19.44. It built `Burnout3Recompiled_Test.exe` and passed all six bootstrap tests, including `qpc_clock_windows_tests` and `frame_pacer_windows_tests`. Feature run `33714243602` then passed 7/7 tests after adding `ps2_elf_tests`; the earlier MSVC macro/nodiscard warnings were also eliminated.

The first CI attempt failed before compilation because `windows-latest` had moved to a Windows Server 2025 / Visual Studio 2026 image while the project explicitly requested the Visual Studio 2022 CMake generator. The workflow is now pinned to `windows-2022` to match the project requirement.

## Test Build 0.1 gate

The bootstrap is materially further along but **Test Build 0.1 is not yet complete**. Remaining bootstrap validation gates are:

1. launch the GUI executable on Windows 10/11 and verify window/message-loop shutdown behavior;
2. perform a controlled crash and verify minidump + state output;
3. capture frame pacing/jitter over a longer interval on a normal desktop session;
4. then integrate the ELF loader with an externally supplied legal game executable without committing proprietary data.
