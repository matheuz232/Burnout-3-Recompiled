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

CI does not constitute interactive validation of the Win32 window or crash/minidump path, and the current pacing integration test is only a coarse average-rate sanity check. Those remain separate gates.

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
