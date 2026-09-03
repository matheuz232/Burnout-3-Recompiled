# Validation Record

## 2026-09-03 bootstrap host validation

Environment available to the implementation session:

- CMake 3.31.6;
- Ninja;
- GCC 14.2;
- Clang 17 toolchain frontend;
- Linux host;
- no local MSVC, MinGW, or Windows SDK.

Host-portable tests:

- `frame_schedule_tests`
- `frame_stats_tests`
- `runtime_options_tests`
- `log_tests`

All four bootstrap host-portable tests passed under GCC and Clang before the ELF milestone.

## 2026-09-03 Windows CI validation

GitHub Actions run `33713829165` used Windows Server 2022, Visual Studio 2022 Enterprise, MSVC 19.44.35228 and Windows SDK 10.0.26100.0. CMake configure, Release build, `Burnout3Recompiled_Test.exe` link and all 6/6 bootstrap tests passed.

CI does not constitute interactive validation of the Win32 window, and the current pacing integration test is only a coarse average-rate sanity check. Those remain separate gates.

## 2026-09-03 PS2 ELF loader validation

The ELF loader is tested only with synthetic, non-proprietary ELF data. It validates ELF32 little-endian MIPS executable headers, program-header table bounds, PT_LOAD ranges, and `p_memsz >= p_filesz`.

Host result: 5/5 tests pass in clean Release builds with GCC 14.2 and Clang 17. GitHub Actions run `33714243602` compiled the ELF loader with MSVC 19.44 and passed 7/7 tests on Windows.

## 2026-09-03 PS2 ELF-backed memory-map validation

Synthetic tests cover PT_LOAD payload copy, BSS zero-fill, guest-address translation/bounds, overlap rejection, 32-bit address-space overflow rejection, and mutable little-endian 8/16/32-bit helpers.

Fresh clean Release host results:

- GCC 14.2: 6/6 tests passed;
- Clang 17: 6/6 tests passed.

GitHub Actions run `33715582203` built `b3r_runtime` with MSVC 19.44 and passed 8/8 tests. The previously observed integer-promotion C4244 warning was eliminated.

## 2026-09-03 R5900 static decoder validation

The initial R5900 decoder is a static-analysis primitive only. It does not execute instructions or emulate CPU state. Synthetic tests cover field extraction, signed immediates, direct branch/jump targets, delay-slot metadata, link/branch-likely flags, indirect `JR`/`JALR`, stable instruction names, and R5900-specific 128-bit `LQ`/`SQ` widths. Unsupported MMI/COP families remain explicitly `Unknown`.

Fresh clean host results:

- GCC 14.2: 7/7 tests passed with no compiler warnings;
- Clang 17: 7/7 tests passed with no compiler warnings.

GitHub Actions run `33716010679` compiled the decoder with MSVC 19.44 and passed 9/9 tests with no project-code compiler warnings.

## 2026-09-03 R5900 basic-block/control-flow validation

The first control-flow analyzer is read-only and does not execute guest instructions or infer register values. It introduces a separate `b3r_analysis` layer depending on the guest-memory map and decoder, avoiding a recompiler/runtime dependency cycle.

Synthetic tests cover:

- linear instruction collection through the terminating control instruction;
- architectural delay slots stored separately from the linear body;
- normal and branch-likely delay-slot path metadata;
- conditional taken/not-taken edges;
- direct `J` and `JAL` edges;
- indirect `JR`/`JALR` exits without invented targets;
- call continuations after the delay slot;
- `BREAK`/system trap termination;
- conservative termination on unsupported/Unknown instructions;
- bounded instruction-limit fallthrough;
- unaligned, unmapped, non-executable and missing-delay-slot rejection.

Fresh clean Release host results:

- GCC: 8/8 tests passed, no compiler warnings;
- Clang 17: 8/8 tests passed, no compiler warnings.

GitHub Actions run `33716632916` on Windows Server 2022 / Visual Studio 2022 / MSVC 19.44 built `b3r_analysis` and passed 10/10 tests, including `r5900_control_flow_tests`. No project-code compiler warning was emitted. The only workflow warning is the external `actions/checkout@v4` Node-runtime deprecation notice.

## 2026-09-03 R5900 reachable control-flow validation

The reachability walker remains a read-only static-analysis layer. It uses a bounded deterministic worklist over validated basic blocks, follows only explicit non-call direct successors, records direct and indirect calls separately, and never invents register-indirect targets.

Synthetic coverage includes:

- deterministic branch/fallthrough discovery;
- call-continuation traversal while keeping direct call targets as evidence only;
- direct self-loop deduplication;
- invalid/unmapped successor recording without discarding the valid source block;
- explicit block-limit truncation evidence;
- unresolved indirect exits;
- leader-inside-block conflict detection;
- fatal rejection of an invalid entry point.

Fresh clean Release host results:

- GCC: 9/9 tests passed, no compiler warnings;
- Clang 17: 9/9 tests passed, no compiler warnings.

GitHub Actions run `33717425111` on Windows Server 2022 / Visual Studio 2022 / MSVC 19.44 compiled `r5900_reachability.cpp` and passed 11/11 tests, including `r5900_reachability_tests`. No project-code compiler warning was emitted. The only workflow warning is the external `actions/checkout@v4` Node-runtime deprecation notice.

## 2026-09-03 deterministic R5900 analysis-report validation

The report renderer is a pure presentation layer over `R5900ReachabilityGraph`. It does not access the guest memory map, execute instructions, discover additional control flow, infer indirect targets, or assign function boundaries. Its purpose is to make analysis evidence reproducible and diffable once a legal real game ELF is supplied.

The text contract includes:

- entry point;
- block/instruction/decoded/unknown counts;
- call, indirect-exit and CFG-issue counts;
- blocks sorted by guest start PC;
- instruction and delay-slot raw words;
- explicit control-flow edges;
- direct/indirect call evidence;
- unresolved/failed/conflicting reachability issues.

The deterministic-order test constructs equivalent graphs with reversed container ordering and requires byte-identical report text.

TDD evidence:

- RED run `33718368434`: MSVC build failed exactly because `analysis/r5900_analysis_report.h` did not exist yet;
- GREEN run `33718514985`: Windows Server 2022 / Visual Studio 2022 / MSVC 19.44 built `r5900_analysis_report.cpp` and passed 12/12 tests, including `r5900_analysis_report_tests`;
- no project-code compiler warning was emitted; the only workflow warning is the external `actions/checkout@v4` Node-runtime deprecation notice.

No fresh GCC/Clang rerun was performed for this report-only milestone in the connector-backed continuation; the prior portable analysis layers remain covered by their recorded clean host validations above.

## 2026-09-03 external PS2 ELF analysis pipeline validation

`Ps2ElfAnalysisResult` composes the existing primitives without executing guest instructions:

`ELF bytes -> parse_ps2_elf -> Ps2MemoryMap::from_elf -> analyze_r5900_reachability -> render_r5900_analysis_report`.

The synthetic end-to-end fixture contains one executable PT_LOAD at guest entry `0x00100000` and one `BREAK` instruction. The test requires an exact deterministic report and separately verifies that invalid ELF magic is classified as `ElfParseFailed` before memory/CFG analysis.

TDD evidence:

- RED run `33772468390`: MSVC failed exactly because `analysis/ps2_elf_analysis.h` did not exist;
- GREEN run `33772705605`: the pipeline compiled and the Windows suite passed 13/13.

## 2026-09-03 `Burnout3Analyze` option parser validation

The console-tool parser supports `--elf <path>`, optional `--output <path>`, optional positive `--max-blocks <count>` (default 4096), and `--help`. Tests cover defaults, missing values, missing ELF path, invalid/zero block limits and unknown options.

TDD evidence:

- RED run `33772935813`: MSVC failed exactly because `tools/burnout3_analyze_options.h` did not exist;
- GREEN run `33773184514`: `b3r_tools` built and the Windows suite passed 14/14 with no project-code warning.

## 2026-09-03 `Burnout3Analyze` file-I/O validation

The application layer opens an externally supplied ELF as a binary file, bounds the in-memory read size, forwards the requested block limit into the analysis pipeline, and writes the deterministic report either to the provided output stream or to an explicit output file. The test uses only a temporary synthetic ELF and verifies missing-input-file classification.

TDD evidence:

- RED run `33773406647`: MSVC failed exactly because `tools/burnout3_analyze_app.h` did not exist;
- GREEN run `33773705488`: Windows/MSVC passed 15/15 tests, including actual temporary-file read/write behavior.

## 2026-09-03 `Burnout3Analyze.exe` validation

The console `main` is intentionally thin: it adapts `argv` to the tested parser, prints help or parse errors, and delegates real work to the tested application layer. It contains no guest-analysis semantics.

TDD evidence:

- RED run `33773875369`: the project built, 15 tests passed, and only `burnout3_analyze_help` failed because the `Burnout3Analyze` executable did not exist yet;
- GREEN run `33774102751`: Visual Studio 2022 / MSVC 19.44 linked `Burnout3Analyze.exe` and `Burnout3Recompiled_Test.exe`, and all 16/16 tests passed including `Burnout3Analyze --help`.

No project-code compiler warning was emitted in the final GREEN run. The only workflow warning remains the external `actions/checkout@v4` Node-runtime deprecation notice. No proprietary Burnout 3 executable or asset was used or committed. A legally supplied real game ELF remains the next evidence gate.

PR #7 merge-ref run `33774831203` also passed 16/16 tests. After merge commit `6168a581a3aff455636790ea15ca8967b53ca6ac`, `main` push run `33777197703` completed successfully, preserving the same validated toolchain baseline.

## 2026-09-03 R5900 opcode-coverage report validation

The deterministic report now adds two evidence-only coverage sections without changing decoder or CFG semantics:

- `INSTRUCTION_HISTOGRAM`: counts reachable decoded instruction names, including `UNKNOWN`, and includes architectural delay slots;
- `UNKNOWN_PRIMARY_OPCODES`: counts only reachable `UNKNOWN` instruction sites by the six-bit MIPS primary opcode extracted from each raw instruction word.

Both histograms use stable ordering. The unknown-primary grouping is intentionally coarse: primary opcodes identify top-level unresolved families but do not by themselves resolve SPECIAL/MMI/COP sub-operations.

TDD evidence:

- RED run `33777384366`: the entire project built successfully; 14/16 tests passed and exactly `r5900_analysis_report_tests` plus `ps2_elf_analysis_tests` failed because the new histogram sections were absent;
- GREEN run `33777558935`: Windows Server 2022 / Visual Studio 2022 / MSVC 19.44 built the updated renderer, `Burnout3Analyze.exe`, and `Burnout3Recompiled_Test.exe`; all 16/16 tests passed.

No project-code compiler warning was emitted in the GREEN run. No decoder instruction support, reachability behavior, or guest execution semantics were added by this milestone. The next meaningful evidence gate remains a legally supplied Burnout 3 ELF.

PR #8 merge-ref run `33778143705` passed 16/16 tests. After merge commit `3f43e1bcce3aad31b826b11226af18302eb6efef`, post-merge `main` run `33778300157` also passed 16/16, preserving the same baseline before crash-handler work.

## 2026-09-03 controlled Windows crash-handler/minidump validation

The crash handler is now exercised in Windows CI through an isolated child process rather than by intentionally crashing the GUI executable. The dedicated `crash_handler_probe` installs the real `CrashHandler`, records known execution markers (`PS2=0x00B3C0DE`, native=`0x1234ABCD`), suppresses Windows fault UI for automation, and raises `EXCEPTION_ACCESS_VIOLATION`.

The parent `crash_handler_windows_tests` harness:

- creates a unique temporary working directory and removes any stale test data before launch;
- starts the probe as a separate process so the CTest runner survives the intentional crash;
- requires abnormal child termination;
- verifies `crash_dumps/last_crash.txt` exists and contains `EXCEPTION_ACCESS_VIOLATION` plus the expected PS2/native execution markers;
- requires at least one non-empty `.dmp` file;
- removes the isolated temporary directory after successful validation.

TDD evidence:

- RED run `33799222874`: CMake configuration failed exactly because the test-declared `tests/crash_handler_probe.cpp` production probe did not exist; build/test stages were therefore skipped;
- GREEN run `33799323131`: Windows Server 2022 / Visual Studio 2022 / MSVC 19.44 built `crash_handler_probe.exe`, `crash_handler_windows_tests.exe`, `Burnout3Recompiled_Test.exe`, and `Burnout3Analyze.exe`; all 17/17 tests passed, including the controlled crash/minidump test in 0.11 seconds.

No project-code compiler warning was emitted in the GREEN run. The remaining workflow warning is external (`actions/checkout@v4` Node runtime deprecation). This validates controlled crash-state/minidump generation by the shared Windows crash-handler library; it does not replace the separate interactive Win32 window/message-loop validation gate.

## 2026-09-03 Win32 window lifecycle smoke validation

The Win32 window path is now exercised in Windows CI with a real top-level `HWND` using the existing production `Win32Window` implementation. This is an automated lifecycle smoke test, not visual desktop validation.

The smoke test:

- acquires the current process `HINSTANCE`;
- creates a 640x360 windowed `Win32Window`;
- requires a non-null live `HWND`;
- posts `WM_CLOSE` to the real window;
- pumps the production message loop until `WM_QUIT` is observed;
- confirms the `HWND` is destroyed;
- uses a bounded loop plus CTest timeout so CI cannot hang indefinitely.

TDD evidence:

- RED run `33802472364`: CMake generation failed exactly because the declared `tests/win32_window_smoke_tests.cpp` source did not exist yet;
- GREEN run `33802569842`: Windows Server 2022 / Visual Studio 2022 / MSVC 19.44 built the smoke target and passed 18/18 tests; `win32_window_smoke_tests` completed successfully in 0.05 seconds.

No project-code compiler warning was emitted in the GREEN run. The only workflow warning remains the external `actions/checkout@v4` Node-runtime deprecation notice. This proves automated create/close/message-loop behavior in the hosted Windows environment, but it does not replace visual inspection of the GUI on an interactive Windows 10/11 desktop.

## 2026-09-03 frame-pacing telemetry validation

The frame-pacing telemetry layer is a portable statistical/reporting component in `b3r_core`. It does not change `WindowsFramePacer`, frame scheduling, simulation cadence, or game semantics. It accepts measured frame durations in seconds and reports milliseconds using a deterministic contract.

The summary contains:

- sample count;
- arithmetic mean;
- minimum and maximum;
- population standard deviation;
- nearest-rank P50, P95 and P99;
- counts strictly above the 120 Hz period (`1000/120` ms), 9 ms, 10 ms and 12 ms.

The renderer emits stable `B3R_FRAME_PACING_TELEMETRY 1` text with fixed three-decimal formatting. Synthetic tests validate the exact statistics, threshold counts, empty-input behavior and byte-exact report text.

TDD / CI evidence:

- RED run `33805068940`: CMake configured successfully and MSVC failed exactly because `core/frame_pacing_telemetry.h` did not exist yet;
- pure telemetry GREEN run `33805247593`: Windows Server 2022 / Visual Studio 2022 / MSVC 19.44 built the new core component and passed 19/19 tests including `frame_pacing_telemetry_tests`;
- Windows pacing integration run `33805412037`: 19/19 tests passed with `frame_pacer_windows_tests` collecting 240 real pacer intervals over approximately two seconds;
- visible-report run `33805548230`: 19/19 tests passed and a dedicated workflow step executed the pacing smoke again so its telemetry was preserved in the CI log.

Observed hosted-runner report from run `33805548230`:

```text
B3R_FRAME_PACING_TELEMETRY 1
SAMPLES 240
MEAN_MS 8.333
MIN_MS 8.204
MAX_MS 8.462
STDDEV_MS 0.012
P50_MS 8.333
P95_MS 8.333
P99_MS 8.333
OVER_8_333MS 80
OVER_9MS 0
OVER_10MS 0
OVER_12MS 0
HIGH_RESOLUTION_TIMER YES
```

The `OVER_8_333MS` counter uses the exact `1000/120 = 8.333333...` ms threshold while the displayed values are rounded to three decimals, so samples can display as `8.333` and still be slightly above the exact target period. No measured sample in this run exceeded 9 ms.

This is a short hosted-CI smoke, not a physical-desktop performance certification. Scheduler behavior, power management, display composition, GPU work and longer-duration jitter still require capture on a normal interactive Windows 10/11 desktop. The presentation target remains 120 Hz; this milestone does not select or alter the game's simulation cadence.

No project-code compiler warning was emitted in the final telemetry run. The only workflow warning remains the external `actions/checkout@v4` Node-runtime deprecation notice.

## 2026-09-03 `Burnout3Analyze` Windows artifact validation

Windows CI now stages a narrowly scoped analyzer package after the normal configure, build, test and frame-pacing telemetry steps. Package staging and validation run on every workflow event; publishing runs only for a push to `main` or an explicit `workflow_dispatch`, so pull requests and feature-branch pushes do not publish redundant analyzer artifacts.

TDD / CI evidence:

- RED run `33808095539`: configure/build, 19/19 tests and pacing telemetry passed; `Stage analyzer package` then failed exactly because `docs/ANALYZE-USAGE.txt` did not exist, and validation/upload were skipped;
- GREEN run `33808250674`: 19/19 tests, telemetry, package staging and package validation passed; upload was correctly skipped on the feature-branch push;
- PR #12 merge-ref run `33808465659` checked out temporary merge commit `ab34417e4702453a4ecf155f9596bd9e5ce66049`, passed 19/19 tests plus package staging/validation, and correctly skipped upload for the `pull_request` event;
- PR #12 merged as `46f04e4f798cd1850ae94647952c85bd16911e13`;
- post-merge `main` run `33808599204` passed 19/19 tests and completed `Upload analyzer artifact` successfully.

Published artifact evidence from run `33808599204`:

- name: `Burnout3Analyze-windows-x64`;
- artifact ID: `9913944778`;
- ZIP size: 58,379 bytes;
- GitHub artifact SHA-256: `07bde0a403eb7981a2d43ab0335ba0318f691aa0840aae3aeaed875c5cc724d1`;
- expiration: `2026-12-02T21:33:39Z`;
- workflow log states exactly two files were uploaded.

The published ZIP was downloaded and inspected independently after the workflow completed. It contains exactly:

- `ANALYZE-USAGE.txt`: 1,009 bytes, SHA-256 `f099454e11f891dbeb120aa2720a640cf44572c25ddfa97d24a7ca52148f7a3a`;
- `Burnout3Analyze.exe`: 119,296 bytes, SHA-256 `0d7e0e8a9fa087cc1715faa6036b85592e0bf882ea17e96275504107d93d2065`, with a valid `MZ` header and PE header offset at byte 256.

No ELF, game asset, dump or other proprietary Burnout 3 data is present in the package. This validates the analyzer delivery channel only; it does **not** validate real Burnout 3 opcode/CFG coverage. The next evidence gate remains running this analyzer against a legally obtained external game executable and using only the generated text report to drive decoder/recompiler work.

No project-code compiler warning was emitted in the final artifact validation run. Remaining workflow warnings are external action/runtime deprecations from `actions/checkout@v4` / `actions/upload-artifact@v4` and their Node runtime dependencies.

## 2026-09-03 opt-in R5900 direct-call traversal validation

The reachability walker now supports an explicit `follow_direct_calls` option, exposed by `Burnout3Analyze --follow-direct-calls`. The option defaults to `false`, so the original entry-root analysis remains unchanged unless the caller opts in. When enabled, only explicit `DirectCall` targets are placed into the existing deterministic FIFO worklist; ordinary CFG successors and callees share the same `max_blocks` budget and the existing scheduled-target set provides deduplication.

The conservative boundary remains unchanged: direct calls are still recorded as call evidence, `JR`/`JALR` and other register-indirect exits are never resolved by guesswork, and traversing a direct target does not assert a function boundary. If an explicit callee cannot be analyzed, the existing non-fatal `TargetAnalysisFailed` issue path records that evidence while valid queued successors continue to be analyzed.

TDD / CI evidence:

- core RED run `33811079587`: MSVC failed exactly because `R5900ReachabilityOptions::follow_direct_calls` did not exist;
- core GREEN run `33811230685`: the new option and direct-callee enqueue behavior compiled and all 20/20 tests passed;
- CLI RED run `33811349684`: MSVC failed exactly because `Burnout3AnalyzeOptions::follow_direct_calls` did not exist;
- parser GREEN run `33811484381`: the CLI flag parsed successfully, default-off behavior remained explicit, and 20/20 tests passed;
- app propagation RED run `33811708045`: the project built and 19/20 tests passed; only `burnout3_analyze_app_tests` failed because the parsed flag was not yet forwarded into `R5900ReachabilityOptions`;
- propagation run `33811825857`: the synthetic direct callee was successfully present in the report, proving propagation worked, but one test-only assertion used the wrong textual spelling for existing call evidence; production behavior was not the cause of that remaining failure;
- corrected report-contract GREEN run `33812029332`: all 20/20 tests passed, including end-to-end direct-callee analysis through the application layer;
- regression-coverage run `33812164557`: all 20/20 tests passed while additionally proving shared `max_blocks` accounting, deduplication of a repeated direct callee, preservation of both call-site records, `TargetAnalysisFailed` for an unmapped direct callee, and continued traversal of a valid call continuation.

The application test additionally runs the same synthetic direct-call ELF twice with the option enabled and requires byte-identical reports, preserving deterministic output. Final branch and merge validation must continue to pass that contract before integration.

All fixtures are synthetic and non-proprietary. No Burnout 3 executable or asset was used. Real opcode/CFG coverage remains blocked on a legally obtained external game executable; the recommended evidence run is `Burnout3Analyze --follow-direct-calls` with a sufficiently large `--max-blocks`, followed by inspection of block-limit, failed-target, indirect-exit and unknown-opcode evidence before choosing decoder/recompiler scope.
