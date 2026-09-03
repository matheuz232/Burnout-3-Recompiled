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

GitHub Actions run `33713829165` used:

- runner: `windows-2022`;
- Windows Server 2022;
- Visual Studio 2022 Enterprise toolchain;
- MSVC 19.44.35228;
- Windows SDK 10.0.26100.0.

Results:

- CMake configure: PASS;
- MSVC Release build: PASS;
- `Burnout3Recompiled_Test.exe` link: PASS;
- `frame_schedule_tests`: PASS;
- `frame_stats_tests`: PASS;
- `runtime_options_tests`: PASS;
- `log_tests`: PASS;
- `qpc_clock_windows_tests`: PASS;
- `frame_pacer_windows_tests`: PASS;
- total: 6/6 tests passed.

The CI exposed two warning classes that were cleaned up in the ELF feature branch:

- duplicate `WIN32_LEAN_AND_MEAN` definitions (already provided by CMake usage requirements);
- intentional discard of the frame-pacer return value without an explicit cast.

CI does not constitute interactive validation of the Win32 window or crash/minidump path, and the current pacing integration test is only a coarse average-rate sanity check. Those remain separate gates.

## 2026-09-03 PS2 ELF loader validation

The ELF loader is tested only with synthetic, non-proprietary ELF data. It validates ELF32 little-endian MIPS executable headers, program-header table bounds, PT_LOAD ranges, and `p_memsz >= p_filesz`.

Host result: 5/5 tests pass in clean Release builds with GCC 14.2 and Clang 17. GitHub Actions run `33714243602` then compiled the ELF loader with MSVC 19.44 and passed 7/7 tests on Windows, including `ps2_elf_tests`. The code-specific MSVC warnings observed in the earlier bootstrap run are absent; the remaining workflow warning is an external `actions/checkout@v4` Node-runtime deprecation notice.

## 2026-09-03 PS2 ELF-backed memory-map validation

The first-stage PS2 virtual-address mapper is tested entirely with synthetic ELF images and contains no proprietary Burnout 3 data. The tests cover:

- PT_LOAD payload copy into native backing;
- zero-filled `[p_filesz, p_memsz)` BSS;
- guest-address translation and range bounds;
- overlap rejection;
- 32-bit guest address-space overflow rejection;
- mutable little-endian 8/16/32-bit read/write helpers.

Fresh clean Release host results:

- GCC 14.2: 6/6 tests passed;
- Clang 17: 6/6 tests passed.

GitHub Actions run `33715582203` on Windows Server 2022 / Visual Studio 2022 / MSVC 19.44 then built `b3r_runtime` and passed 8/8 tests, including `ps2_memory_map_tests`. An earlier run exposed MSVC warning C4244 caused by integer promotion in the `read_u16` return expression; commit `5e7d3a2a` adds an explicit final narrowing cast, and the clean rerun contains no project-code compiler warnings. The remaining warning is the external `actions/checkout@v4` Node-runtime deprecation notice.
