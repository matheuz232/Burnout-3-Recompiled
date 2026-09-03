# Progress

Status date: 2026-09-03

Completion rule: `implemented -> compiled -> executed/tested -> validated`.

| Component | Status | Evidence / next gate |
|---|---|---|
| Repository/CMake bootstrap | DONE | Clean host configure/build succeeds with GCC and Clang; Windows CI configures with VS2022 |
| Portable 120 Hz frame schedule | DONE | Deterministic unit tests pass on host and Windows CI |
| Frame statistics | DONE | Unit tests pass on host and Windows CI |
| Frame pacing telemetry | CI_VALIDATED | Pure mean/min/max/stddev/P50/P95/P99/threshold metrics are deterministic; Windows CI captures 240 real pacer samples and surfaces the report |
| Runtime option parser | DONE | Unit tests pass on host and Windows CI |
| Structured logging | DONE | Unit tests pass on host and Windows CI |
| Win32 executable target | COMPILED_IN_CI | `Burnout3Recompiled_Test.exe` builds with MSVC 19.44 / Visual Studio 2022; interactive launch still required |
| Win32 window | READY_FOR_INTERACTIVE_VALIDATION | Windows CI smoke creates a real `HWND`, verifies it is live, posts `WM_CLOSE`, observes `WM_QUIT` and confirms destruction; visual/interactive Windows 10/11 validation still required |
| QPC high-resolution clock | DONE | Windows integration test executes successfully in GitHub Actions |
| 120 FPS Windows frame pacer | WORKING | 240-frame CI telemetry measured 8.333 ms mean, 8.333 ms P95/P99 and 8.462 ms max with high-resolution timer; longer normal-desktop capture remains the validation gate |
| Crash handler/minidump | CI_VALIDATED | Controlled child-process access violation on Windows CI verifies `last_crash.txt`, PS2/native execution markers and a non-empty `.dmp`; GUI/window validation remains separate |
| PS2 ELF loader | CI_VALIDATED | Synthetic ELF32 little-endian MIPS tests pass with GCC, Clang and MSVC; next gate is a legally supplied real game ELF |
| PS2 memory mapping | CI_VALIDATED | ELF PT_LOAD-backed mapper passes GCC/Clang tests and 8/8 Windows/MSVC CI suite; next gate is real executable metadata |
| R5900 decoder | CI_VALIDATED | Initial static integer/control-flow/load-store decoder passes GCC/Clang tests and 9/9 Windows/MSVC CI suite; MMI/COP families remain explicit Unknown |
| R5900 basic-block analysis | CI_VALIDATED | Conservative block/edge analysis passes GCC/Clang tests and 10/10 Windows/MSVC CI suite; delay slots and branch-likely are explicit |
| Reachable control-flow graph | CI_VALIDATED | Default bounded worklist keeps calls as evidence; opt-in `follow_direct_calls` traverses explicit direct callees with shared block budget, deduplication and failure evidence while indirect exits remain unresolved; Windows/MSVC suite passes 20/20 |
| R5900 analysis report | CI_VALIDATED | Stable deterministic text report for blocks/instructions/calls/issues passes Windows/MSVC CI |
| R5900 opcode coverage metrics | CI_VALIDATED | Report includes deterministic instruction histogram plus primary-opcode histogram for reachable `UNKNOWN` sites, including delay slots; Windows/MSVC suite passes 16/16 |
| External PS2 ELF analysis pipeline | CI_VALIDATED | Synthetic ELF is parsed, mapped, traversed and rendered end-to-end; 13/13 Windows/MSVC tests pass |
| `Burnout3Analyze` CLI | CI_VALIDATED | Console executable builds with VS2022/MSVC; `--follow-direct-calls` is opt-in, defaults off, propagates end-to-end and preserves deterministic reporting; current Windows suite passes 20/20 |
| `Burnout3Analyze` Windows artifact | CI_VALIDATED | `main`/manual Windows CI publishes `Burnout3Analyze-windows-x64`; artifact ID `9913944778` was downloaded and verified to contain only the analyzer executable plus usage guide |
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

Crash-handler RED run `33799222874` failed during CMake generation exactly because the test-declared probe source did not exist. GREEN run `33799323131` built the isolated probe and harness with MSVC 19.44 and passed 17/17 tests, including controlled access-violation state/minidump validation. PR #9 merge-ref run `33799706870` and post-merge `main` run `33799864371` also passed 17/17.

Win32-window smoke RED run `33802472364` failed during CMake generation exactly because the declared smoke source did not exist. GREEN run `33802569842` built the real-window smoke test and passed 18/18 tests; `win32_window_smoke_tests` created/closed the window and completed in 0.05 seconds.

Frame-pacing telemetry RED run `33805068940` failed exactly because `core/frame_pacing_telemetry.h` did not yet exist. Pure telemetry GREEN run `33805247593` passed 19/19. Windows telemetry run `33805412037` passed 19/19 with a 240-frame pacing smoke. Run `33805548230` additionally surfaced the report in the CI log: 240 samples, 8.333 ms mean, 8.204 ms min, 8.462 ms max, 0.012 ms population stddev, 8.333 ms P50/P95/P99, 80 samples above the exact 120 Hz period, and zero above 9/10/12 ms; the high-resolution timer path was active.

Analyzer-artifact RED run `33808095539` passed configure/build, 19/19 tests and pacing telemetry, then failed only at package staging because `docs/ANALYZE-USAGE.txt` was absent. GREEN run `33808250674` passed 19/19 plus package staging/validation, with upload correctly skipped on a feature-branch push. PR #12 merge-ref run `33808465659` passed 19/19 plus staging/validation and also skipped publishing. After merge commit `46f04e4f798cd1850ae94647952c85bd16911e13`, post-merge `main` run `33808599204` passed 19/19 and published artifact `Burnout3Analyze-windows-x64` ID `9913944778` (58,379-byte ZIP; SHA-256 `07bde0a403eb7981a2d43ab0335ba0318f691aa0840aae3aeaed875c5cc724d1`). Download inspection confirmed exactly `Burnout3Analyze.exe` and `ANALYZE-USAGE.txt`, with no ELF, assets, dumps or other proprietary data.

Direct-call traversal used three explicit RED/GREEN evidence gates. Core RED run `33811079587` failed exactly because `R5900ReachabilityOptions::follow_direct_calls` did not exist; core GREEN run `33811230685` passed 20/20. CLI RED run `33811349684` failed exactly because `Burnout3AnalyzeOptions::follow_direct_calls` did not exist; parser GREEN run `33811484381` passed 20/20. App propagation RED run `33811708045` built successfully and failed only `burnout3_analyze_app_tests` because the option was not yet forwarded into reachability. After propagation and correction of a test-only report-string mismatch, GREEN run `33812029332` passed 20/20. Regression-coverage run `33812164557` also passed 20/20 while proving direct-callee deduplication, shared `max_blocks` accounting and `TargetAnalysisFailed` behavior for an unmapped direct callee.

No project-code compiler warnings were emitted in these final validation runs; the remaining workflow warnings are external action/runtime deprecations (`actions/checkout@v4` and `actions/upload-artifact@v4`).

The first CI attempt failed before compilation because `windows-latest` had moved to a Windows Server 2025 / Visual Studio 2026 image while the project explicitly requested the Visual Studio 2022 CMake generator. The workflow remains pinned to `windows-2022`.

## Test Build 0.1 gate

The bootstrap is materially further along but **Test Build 0.1 is not yet complete**. Remaining bootstrap validation gates are:

1. visually inspect the GUI executable on an interactive Windows 10/11 desktop; automated create/`WM_CLOSE`/`WM_QUIT` lifecycle is already covered by Windows CI smoke;
2. capture frame pacing/jitter over a longer interval on a normal desktop session; the 240-frame hosted-CI telemetry is evidence only, not a physical-desktop benchmark;
3. download/use the CI-published `Burnout3Analyze-windows-x64` (or a local build) against an externally supplied legal Burnout 3 executable, preferably with `--follow-direct-calls` and a sufficiently large `--max-blocks`, without committing proprietary data; use the deterministic report to measure real reachable-code/opcode coverage and drive the actual recompilation strategy from that evidence.
