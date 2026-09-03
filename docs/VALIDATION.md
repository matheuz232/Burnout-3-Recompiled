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
