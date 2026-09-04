# Progress

Status date: 2026-09-04

Completion rule: `implemented -> compiled -> executed/tested -> validated`.

| Component | Status | Evidence / next gate |
|---|---|---|
| Repository/CMake bootstrap | DONE | Clean host configure/build succeeds with GCC and Clang; Windows CI configures with VS2022 |
| Portable 120 Hz frame schedule | DONE | Deterministic unit tests pass on host and Windows CI |
| Frame statistics | DONE | Unit tests pass on host and Windows CI |
| Frame pacing telemetry | CI_VALIDATED | Pure mean/min/max/stddev/P50/P95/P99/threshold metrics are deterministic; Windows CI captures 240 real pacer samples and surfaces the report |
| `Burnout3PacingProbe` | CI_VALIDATED | Windows x64 console probe reuses the production 120 Hz pacer/QPC path; parser, 1-second/120-frame smoke, stdout report and file-output contract pass Windows/MSVC CI; physical 60-second-or-longer desktop capture remains pending |
| `Burnout3PacingProbe` Windows artifact | CI_VALIDATED | Post-merge `main` run `33834646204` published isolated artifact `Burnout3PacingProbe-windows-x64` ID `9922874193` from commit `212997302b2026c9447d5386c728339442a9c9c7`; downloaded ZIP inspection confirmed exactly the probe executable plus usage guide and matching SHA-256 `6ed6a3e8f0906056ec57a35b5e60b6ba0f0199035301b0b4587b37c2ab84a4ec` |
| Runtime option parser | DONE | Unit tests pass on host and Windows CI |
| Structured logging | DONE | Unit tests pass on host and Windows CI |
| Win32 executable target | COMPILED_IN_CI | `Burnout3Recompiled_Test.exe` builds with MSVC 19.44 / Visual Studio 2022; interactive launch still required |
| Win32 window | READY_FOR_INTERACTIVE_VALIDATION | Windows CI smoke creates a real `HWND`, verifies it is live, posts `WM_CLOSE`, observes `WM_QUIT` and confirms destruction; visual/interactive Windows 10/11 validation still required |
| QPC high-resolution clock | DONE | Windows integration test executes successfully in GitHub Actions |
| 120 FPS Windows frame pacer | WORKING | Short CI telemetry validates the production pacing path; `Burnout3PacingProbe` now provides a reproducible 60-second-or-longer desktop capture path, but a normal physical Windows desktop capture remains the validation gate |
| Crash handler/minidump | CI_VALIDATED | Controlled child-process access violation on Windows CI verifies `last_crash.txt`, PS2/native execution markers and a non-empty `.dmp`; GUI/window validation remains separate |
| PS2 ELF loader | CI_VALIDATED | Synthetic ELF32 little-endian MIPS tests pass with GCC, Clang and MSVC; next gate is a legally supplied real game ELF |
| PS2 memory mapping | CI_VALIDATED | ELF PT_LOAD-backed mapper passes GCC/Clang tests and 8/8 Windows/MSVC CI suite; next gate is real executable metadata |
| R5900 decoder | CI_VALIDATED | Initial static integer/control-flow/load-store decoder passes GCC/Clang tests and 9/9 Windows/MSVC CI suite; MMI/COP families remain explicit Unknown |
| R5900 basic-block analysis | CI_VALIDATED | Conservative block/edge analysis passes GCC/Clang tests and 10/10 Windows/MSVC CI suite; delay slots and branch-likely are explicit |
| Reachable control-flow graph | CI_VALIDATED | Default bounded worklist keeps calls as evidence; opt-in `follow_direct_calls` traverses explicit direct callees with shared block budget, deduplication and failure evidence while indirect exits remain unresolved; Windows/MSVC suite passes 20/20 |
| R5900 analysis report | CI_VALIDATED | Stable deterministic text report for blocks/instructions/calls/issues plus evidence metrics passes Windows/MSVC CI |
| R5900 opcode coverage metrics | CI_VALIDATED | Report includes deterministic instruction histogram, unknown-primary histogram and per-PC `UNKNOWN_SITES` raw bitfield diagnostics including delay slots; Windows/MSVC suite passes 20/20 |
| R5900 direct-call target metrics | CI_VALIDATED | `DIRECT_CALL_TARGETS` groups resolved direct references by guest target address and counts static call sites deterministically; indirect/unresolved calls remain ordinary evidence and no function boundary is inferred |
| External PS2 ELF analysis pipeline | CI_VALIDATED | Synthetic ELF is parsed, mapped, traversed and rendered end-to-end; current byte-exact report contract passes Windows/MSVC CI |
| `Burnout3Analyze` CLI | CI_VALIDATED | Console executable builds with VS2022/MSVC; `--follow-direct-calls` is opt-in, defaults off, propagates end-to-end and preserves deterministic reporting; current Windows suite passes 20/20 |
| `Burnout3Analyze` Windows artifact | CI_VALIDATED | `main`/manual Windows CI publishes `Burnout3Analyze-windows-x64`; latest verified current-main artifact ID `9922873755` was published by run `33834646204` from `main` commit `212997302b2026c9447d5386c728339442a9c9c7` |
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

Direct-call traversal used three explicit RED/GREEN evidence gates. Core RED run `33811079587` failed exactly because `R5900ReachabilityOptions::follow_direct_calls` did not exist; core GREEN run `33811230685` passed 20/20. CLI RED run `33811349684` failed exactly because `Burnout3AnalyzeOptions::follow_direct_calls` did not exist; parser GREEN run `33811484381` passed 20/20. App propagation RED run `33811708045` built successfully and failed only `burnout3_analyze_app_tests` because the option was not yet forwarded into reachability. After propagation and correction of a test-only report-string mismatch, GREEN run `33812029332` passed 20/20. Regression-coverage run `33812164557` also passed 20/20 while proving direct-callee deduplication, shared `max_blocks` accounting and `TargetAnalysisFailed` behavior for an unmapped direct callee. PR #14 merge-ref run `33812746572` passed 20/20; merge commit `99aa138688f7d36d26ad94409a99b9288aa7138e` then passed 20/20 in post-merge main run `33812901166` and published analyzer artifact ID `9915484087` (58,885-byte ZIP; SHA-256 `fe4470f544794cbcd66a3508880ee2f3e550bfb69ed12f8f6578467691601da0`).

`UNKNOWN_SITES` diagnostics used an explicit report-contract RED/GREEN sequence. RED run `33817785688` built successfully and passed 19/20 tests; only `r5900_analysis_report_tests` failed because the new site-level diagnostics were absent. Renderer run `33817933590` proved the new report section itself: `r5900_analysis_report_tests` passed, while the only remaining failure was the older byte-exact `ps2_elf_analysis_tests` expected text missing `UNKNOWN_SITES 0`. After updating that test-only end-to-end contract, GREEN run `33818075117` passed 20/20 with telemetry plus analyzer package staging/validation. PR #15 merge-ref run `33818605215` passed 20/20 on temporary merge SHA `a0b4694779bf4dadd5bfe3f958428e66fc6347ee`; merge commit `d493761463d33249fa17ab26916d9d44b51a7164` then passed 20/20 in main run `33818702279` and published analyzer artifact ID `9917475746` (61,394-byte ZIP; SHA-256 `ba5a626be85e5904f624cb1731c433c9e22a1ad5e44ee263d198b8830e368c9e`). Synthetic coverage verifies two unknown sites, including an architectural delay slot, raw-field extraction for `PRIMARY/RS/RT/RD/SA/FUNCT`, PC ordering and byte-identical output under reordered graph containers.

`DIRECT_CALL_TARGETS` diagnostics used the same report-contract TDD pattern. RED run `33828724619` built successfully and passed 19/20 tests; only `r5900_analysis_report_tests` failed because target aggregation was absent. Renderer run `33828875766` proved grouping/filtering/order: `r5900_analysis_report_tests` passed, while the only remaining failure was the older byte-exact `ps2_elf_analysis_tests` expectation missing `DIRECT_CALL_TARGETS 0`. After updating only that expected text, GREEN run `33828970350` passed 20/20 plus frame-pacing telemetry and analyzer package staging/validation. PR #16 merged as `79b83039df6d58d3d997a1536021c32cadac6a33`; post-merge run `33829442535` passed 20/20 and published current analyzer artifact ID `9921137059`.

`Burnout3PacingProbe` uses four explicit TDD/packaging gates. Parser RED run `33833425537` failed compilation exactly because `tools/burnout3_pacing_probe_options.h` was absent; parser GREEN run `33833561650` passed 21/21. Executable-smoke RED run `33833683371` passed 21/22 with only `burnout3_pacing_probe_smoke` Not Run because `Burnout3PacingProbe.exe` did not yet exist; executable GREEN run `33833827722` passed 22/22 and captured a real 1-second/120-frame 120 Hz smoke. File-output RED run `33833988815` passed 22/23 and failed only `burnout3_pacing_probe_output` because file output was deliberately unavailable; output GREEN run `33834065915` passed 23/23 and verified the requested report file plus no duplicate stdout. Package RED run `33834158018` passed 23/23, existing pacing telemetry, the visible probe smoke, and analyzer package staging/validation, then failed only because `docs/PACING-PROBE-USAGE.txt` was absent. Package GREEN run `33834235339` passed 23/23 plus visible probe smoke and both analyzer/probe package staging/validation, with uploads correctly skipped on the feature branch. PR #17 merge-ref run `33834545021` passed 23/23 with both packages staged/validated and uploads skipped for the pull-request event. Merge commit `212997302b2026c9447d5386c728339442a9c9c7` then passed 23/23 in post-merge `main` run `33834646204`; the one-second probe reported `TARGET_HZ 120`, `REQUESTED_SECONDS 1`, `FRAMES 120` and `HIGH_RESOLUTION_TIMER YES`. That run published pacing-probe artifact ID `9922874193` (27,369-byte ZIP; SHA-256 `6ed6a3e8f0906056ec57a35b5e60b6ba0f0199035301b0b4587b37c2ab84a4ec`) and republished analyzer artifact ID `9922873755`. Download inspection of the pacing-probe ZIP confirmed exactly `Burnout3PacingProbe.exe` (62,976 bytes, PE/MZ) and `PACING-PROBE-USAGE.txt` (1,588 bytes).

No game execution, simulation timing, decoder/recompiler semantics, graphics, audio, or input behavior is changed by `Burnout3PacingProbe`. The tool measures only the existing 120 Hz Windows pacing path. Hosted CI smoke evidence is not treated as physical-desktop performance certification.

The first CI attempt failed before compilation because `windows-latest` had moved to a Windows Server 2025 / Visual Studio 2026 image while the project explicitly requested the Visual Studio 2022 CMake generator. The workflow remains pinned to `windows-2022`.

## Test Build 0.1 gate

The bootstrap is materially further along but **Test Build 0.1 is not yet complete**. Remaining bootstrap validation gates are:

1. visually inspect the GUI executable on an interactive Windows 10/11 desktop; automated create/`WM_CLOSE`/`WM_QUIT` lifecycle is already covered by Windows CI smoke;
2. run `Burnout3PacingProbe --seconds 60 --output <report>` (or longer) during a normal physical Windows 10/11 desktop session and review the generated pacing report; hosted-CI 1-second/120-frame and 240-frame telemetry are evidence only, not a physical-desktop benchmark;
3. download/use the CI-published `Burnout3Analyze-windows-x64` (or a local build) against an externally supplied legal Burnout 3 executable, preferably with `--follow-direct-calls` and a sufficiently large `--max-blocks`, without committing proprietary data; use `UNKNOWN_PRIMARY_OPCODES`, `UNKNOWN_SITES`, and `DIRECT_CALL_TARGETS` to measure real unresolved opcode/subfield coverage and static direct-reference structure before choosing decoder/recompiler scope.