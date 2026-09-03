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
| Crash handler/minidump | CI_VALIDATED | Controlled child-process access violation on Windows CI verifies `last_crash.txt`, PS2/native execution markers and a non-empty `.dmp`; GUI/window validation remains separate |
| PS2 ELF loader | CI_VALIDATED | Synthetic ELF32 little-endian MIPS tests pass with GCC, Clang and MSVC; next gate is a legally supplied real game ELF |
| PS2 memory mapping | CI_VALIDATED | ELF PT_LOAD-backed mapper passes GCC/Clang tests and 8/8 Windows/MSVC CI suite; next gate is real executable metadata |
| R5900 decoder | CI_VALIDATED | Initial static integer/control-flow/load-store decoder passes GCC/Clang tests and 9/9 Windows/MSVC CI suite; MMI/COP families remain explicit Unknown |
| R5900 basic-block analysis | CI_VALIDATED | Conservative block/edge analysis passes GCC/Clang tests and 10/10 Windows/MSVC CI suite; delay slots and branch-likely are explicit |
| Reachable control-flow graph | CI_VALIDATED | Bounded direct-edge worklist passes GCC/Clang tests and 11/11 Windows/MSVC CI suite; calls and indirect exits remain evidence rather than guessed traversal |
| R5900 analysis report | CI_VALIDATED | Stable deterministic text report for blocks/instructions/calls/issues passes Windows/MSVC CI |
| R5900 opcode coverage metrics | CI_VALIDATED | Report includes deterministic instruction histogram plus primary-opcode histogram for reachable `UNKNOWN` sites, including delay slots; Windows/MSVC suite passes 16/16 |
| External PS2 ELF analysis pipeline | CI_VALIDATED | Synthetic ELF is parsed, mapped, traversed and rendered end-to-end; 13/13 Windows/MSVC tests pass |
| `Burnout3Analyze` CLI | CI_VALIDATED | Console executable builds with VS2022/MSVC, external-file/stdout/output-path behavior is tested, and the full suite passes 16/16; next gate is a legally supplied Burnout 3 ELF |
| Static/binary recompiler | TODO | Strategy intentionally not selected yet; real ELF coverage should drive the instruction/IR/backend plan |
| Graphics | TODO | No D3D initialization yet |
| Audio | TODO | No XAudio2 initialization yet |
| Input | TODO | No keyboard/XInput layer yet |
| Game initialization | TODO | No game code translated yet |
| Menu/frontend | TODO | Blocked by prior milestones |
| Test race | TODO | Blocked by prior milestones |

## Windows CI evidence

GitHub Actions run `33713829165` on `windows-2022` completed successfully using Visual Studio 2022 / MSVC 19.44. It built `Burnout3Recompiled_Test.exe` and passed all six bootstrap tests. Feature run `33714243602` passed 7/7 after adding the ELF loader.

Memory-map run `33715582203` passed 8/8. R5900 decoder run `33716010679` passed 9/9. R5900 basic-block run `33716632916` built `b3r_analysis` with MSVC 19.44 and passed 10/10 tests including `r5900_control_flow_tests`.

Reachability run `33717425111` passed 11/11. Analysis-report GREEN run `33718514985` passed 12/12.

`Burnout3Analyze` development used three additional TDD gates plus the final executable gate:

- ELF-pipeline RED `33772468390` -> GREEN `33772705605` (13/13);
- CLI-options RED `33772935813` -> GREEN `33773184514` (14/14);
- file-I/O app RED `33773406647` -> GREEN `33773705488` (15/15);
- executable help-smoke RED `33773875369` -> GREEN `33774102751` (16/16).

PR #7 merge-ref run `33774831203` passed 16/16, and post-merge `main` run `33777197703` also completed successfully.

Opcode-coverage RED run `33777384366` built successfully and failed only the two report-contract tests that required the new histograms. GREEN run `33777558935` built the updated renderer and passed 16/16 tests. PR #8 merge-ref run `33778143705` and post-merge `main` run `33778300157` both passed 16/16.

Crash-handler RED run `33799222874` failed during CMake generation exactly because the test-declared probe source did not exist. GREEN run `33799323131` built the isolated probe and harness with MSVC 19.44 and passed 17/17 tests, including controlled access-violation state/minidump validation.

No project-code compiler warnings were emitted in these final validation runs; the remaining workflow warning is external to the project (`actions/checkout@v4` Node runtime deprecation).

The first CI attempt failed before compilation because `windows-latest` had moved to a Windows Server 2025 / Visual Studio 2026 image while the project explicitly requested the Visual Studio 2022 CMake generator. The workflow remains pinned to `windows-2022`.

## Test Build 0.1 gate

The bootstrap is materially further along but **Test Build 0.1 is not yet complete**. Remaining bootstrap validation gates are:

1. launch the GUI executable on Windows 10/11 and verify window/message-loop shutdown behavior;
2. capture frame pacing/jitter over a longer interval on a normal desktop session;
3. run `Burnout3Analyze` against an externally supplied legal Burnout 3 executable without committing proprietary data, use the deterministic report to measure real reachable-code/opcode coverage, and drive the actual recompilation strategy from that evidence.
