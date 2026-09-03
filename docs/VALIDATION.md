# Validation Record

## 2026-09-03 bootstrap host validation

Environment available to the implementation session:

- CMake 3.31.6;
- Ninja;
- GCC 14.2;
- Clang 17 toolchain frontend;
- Linux host;
- no MSVC, MinGW, or Windows SDK.

Host-portable tests:

- `frame_schedule_tests`
- `frame_stats_tests`
- `runtime_options_tests`
- `log_tests`

Windows-only tests prepared but not executable on this host:

- `qpc_clock_windows_tests`
- `frame_pacer_windows_tests`

A Windows CI workflow is included to compile the Win32 runtime and execute all tests when the repository is run on GitHub Actions/Windows.
