cmake_minimum_required(VERSION 3.25)
project(Burnout3Recompiled LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(B3R_BUILD_TESTS "Build Burnout3Recompiled tests" ON)

add_library(b3r_core
  src/core/frame_schedule.cpp
  src/core/frame_stats.cpp
  src/core/frame_pacing_telemetry.cpp
  src/core/runtime_options.cpp
  src/debug/log.cpp
)
target_include_directories(b3r_core PUBLIC src)

add_library(b3r_recompiler
  src/recompiler/ps2_elf.cpp
  src/recompiler/r5900_decoder.cpp
)
target_include_directories(b3r_recompiler PUBLIC src)
if(MSVC)
  target_compile_options(b3r_recompiler PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(b3r_recompiler PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(b3r_runtime
  src/runtime/ps2_memory_map.cpp
)
target_include_directories(b3r_runtime PUBLIC src)
target_link_libraries(b3r_runtime PUBLIC b3r_recompiler)
if(MSVC)
  target_compile_options(b3r_runtime PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(b3r_runtime PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(b3r_analysis
  src/analysis/r5900_control_flow.cpp
  src/analysis/r5900_reachability.cpp
  src/analysis/r5900_analysis_report.cpp
  src/analysis/ps2_elf_analysis.cpp
)
target_include_directories(b3r_analysis PUBLIC src)
target_link_libraries(b3r_analysis PUBLIC b3r_runtime b3r_recompiler)
if(MSVC)
  target_compile_options(b3r_analysis PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(b3r_analysis PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(b3r_tools
  src/tools/burnout3_analyze_options.cpp
  src/tools/burnout3_analyze_app.cpp
)
target_include_directories(b3r_tools PUBLIC src)
target_link_libraries(b3r_tools PUBLIC b3r_analysis)
if(MSVC)
  target_compile_options(b3r_tools PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(b3r_tools PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(Burnout3Analyze
  src/tools/burnout3_analyze_main.cpp
)
target_link_libraries(Burnout3Analyze PRIVATE b3r_tools)
if(MSVC)
  target_compile_options(Burnout3Analyze PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(Burnout3Analyze PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MSVC)
  target_compile_options(b3r_core PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(b3r_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
  add_library(b3r_windows_runtime STATIC
    src/platform/windows/crash_handler.cpp
    src/platform/windows/qpc_clock.cpp
    src/platform/windows/win32_window.cpp
    src/platform/windows/windows_frame_pacer.cpp
  )
  target_include_directories(b3r_windows_runtime PUBLIC src)
  target_compile_definitions(b3r_windows_runtime PUBLIC
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
  )
  target_link_libraries(b3r_windows_runtime PUBLIC b3r_core dbghelp winmm)
  if(MSVC)
    target_compile_options(b3r_windows_runtime PRIVATE /W4 /permissive- /Zc:__cplusplus)
  endif()

  add_library(b3r_pacing_probe STATIC
    src/tools/burnout3_pacing_probe_options.cpp
    src/tools/burnout3_pacing_probe_app.cpp
  )
  target_include_directories(b3r_pacing_probe PUBLIC src)
  target_link_libraries(b3r_pacing_probe PUBLIC b3r_windows_runtime)
  if(MSVC)
    target_compile_options(b3r_pacing_probe PRIVATE /W4 /permissive- /Zc:__cplusplus)
  endif()

  add_executable(Burnout3Recompiled_Test WIN32
    src/platform/windows/win_main.cpp
  )
  target_link_libraries(Burnout3Recompiled_Test PRIVATE b3r_windows_runtime shell32)
  if(MSVC)
    target_compile_options(Burnout3Recompiled_Test PRIVATE /W4 /permissive- /Zc:__cplusplus)
  endif()
else()
  message(STATUS "Non-Windows host: Win32 runtime target is skipped; portable tests remain available.")
endif()

if(B3R_BUILD_TESTS)
  enable_testing()

  add_executable(frame_schedule_tests tests/frame_schedule_tests.cpp)
  target_link_libraries(frame_schedule_tests PRIVATE b3r_core)
  add_test(NAME frame_schedule_tests COMMAND frame_schedule_tests)

  add_executable(frame_stats_tests tests/frame_stats_tests.cpp)
  target_link_libraries(frame_stats_tests PRIVATE b3r_core)
  add_test(NAME frame_stats_tests COMMAND frame_stats_tests)

  add_executable(frame_pacing_telemetry_tests tests/frame_pacing_telemetry_tests.cpp)
  target_link_libraries(frame_pacing_telemetry_tests PRIVATE b3r_core)
  add_test(NAME frame_pacing_telemetry_tests COMMAND frame_pacing_telemetry_tests)

  add_executable(runtime_options_tests tests/runtime_options_tests.cpp)
  target_link_libraries(runtime_options_tests PRIVATE b3r_core)
  add_test(NAME runtime_options_tests COMMAND runtime_options_tests)

  add_executable(log_tests tests/log_tests.cpp)
  target_link_libraries(log_tests PRIVATE b3r_core)
  add_test(NAME log_tests COMMAND log_tests)

  add_executable(ps2_elf_tests tests/ps2_elf_tests.cpp)
  target_link_libraries(ps2_elf_tests PRIVATE b3r_recompiler)
  add_test(NAME ps2_elf_tests COMMAND ps2_elf_tests)

  add_executable(r5900_decoder_tests tests/r5900_decoder_tests.cpp)
  target_link_libraries(r5900_decoder_tests PRIVATE b3r_recompiler)
  add_test(NAME r5900_decoder_tests COMMAND r5900_decoder_tests)

  add_executable(ps2_memory_map_tests tests/ps2_memory_map_tests.cpp)
  target_link_libraries(ps2_memory_map_tests PRIVATE b3r_runtime)
  add_test(NAME ps2_memory_map_tests COMMAND ps2_memory_map_tests)

  add_executable(r5900_control_flow_tests tests/r5900_control_flow_tests.cpp)
  target_link_libraries(r5900_control_flow_tests PRIVATE b3r_analysis)
  add_test(NAME r5900_control_flow_tests COMMAND r5900_control_flow_tests)

  add_executable(r5900_reachability_tests tests/r5900_reachability_tests.cpp)
  target_link_libraries(r5900_reachability_tests PRIVATE b3r_analysis)
  add_test(NAME r5900_reachability_tests COMMAND r5900_reachability_tests)

  add_executable(r5900_follow_direct_calls_tests tests/r5900_follow_direct_calls_tests.cpp)
  target_link_libraries(r5900_follow_direct_calls_tests PRIVATE b3r_analysis)
  add_test(NAME r5900_follow_direct_calls_tests COMMAND r5900_follow_direct_calls_tests)

  add_executable(r5900_analysis_report_tests tests/r5900_analysis_report_tests.cpp)
  target_link_libraries(r5900_analysis_report_tests PRIVATE b3r_analysis)
  add_test(NAME r5900_analysis_report_tests COMMAND r5900_analysis_report_tests)

  add_executable(ps2_elf_analysis_tests tests/ps2_elf_analysis_tests.cpp)
  target_link_libraries(ps2_elf_analysis_tests PRIVATE b3r_analysis)
  add_test(NAME ps2_elf_analysis_tests COMMAND ps2_elf_analysis_tests)

  add_executable(burnout3_analyze_options_tests tests/burnout3_analyze_options_tests.cpp)
  target_link_libraries(burnout3_analyze_options_tests PRIVATE b3r_tools)
  add_test(NAME burnout3_analyze_options_tests COMMAND burnout3_analyze_options_tests)

  add_executable(burnout3_analyze_app_tests tests/burnout3_analyze_app_tests.cpp)
  target_link_libraries(burnout3_analyze_app_tests PRIVATE b3r_tools)
  add_test(NAME burnout3_analyze_app_tests COMMAND burnout3_analyze_app_tests)

  add_test(NAME burnout3_analyze_help COMMAND Burnout3Analyze --help)

  if(WIN32)
    add_executable(qpc_clock_windows_tests tests/qpc_clock_windows_tests.cpp)
    target_link_libraries(qpc_clock_windows_tests PRIVATE b3r_windows_runtime)
    add_test(NAME qpc_clock_windows_tests COMMAND qpc_clock_windows_tests)

    add_executable(frame_pacer_windows_tests tests/frame_pacer_windows_tests.cpp)
    target_link_libraries(frame_pacer_windows_tests PRIVATE b3r_windows_runtime)
    add_test(NAME frame_pacer_windows_tests COMMAND frame_pacer_windows_tests)

    add_executable(burnout3_pacing_probe_options_tests tests/burnout3_pacing_probe_options_tests.cpp)
    target_link_libraries(burnout3_pacing_probe_options_tests PRIVATE b3r_pacing_probe)
    if(MSVC)
      target_compile_options(burnout3_pacing_probe_options_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
    endif()
    add_test(NAME burnout3_pacing_probe_options_tests COMMAND burnout3_pacing_probe_options_tests)

    add_executable(burnout3_pacing_probe_windows_tests tests/burnout3_pacing_probe_windows_tests.cpp)
    target_link_libraries(burnout3_pacing_probe_windows_tests PRIVATE b3r_pacing_probe)
    if(MSVC)
      target_compile_options(burnout3_pacing_probe_windows_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
    endif()
    add_test(NAME burnout3_pacing_probe_windows_tests COMMAND burnout3_pacing_probe_windows_tests)
    set_tests_properties(burnout3_pacing_probe_windows_tests PROPERTIES TIMEOUT 10)

    add_executable(win32_window_smoke_tests tests/win32_window_smoke_tests.cpp)
    target_link_libraries(win32_window_smoke_tests PRIVATE b3r_windows_runtime)
    if(MSVC)
      target_compile_options(win32_window_smoke_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
    endif()
    add_test(NAME win32_window_smoke_tests COMMAND win32_window_smoke_tests)
    set_tests_properties(win32_window_smoke_tests PROPERTIES TIMEOUT 15)

    add_executable(crash_handler_probe tests/crash_handler_probe.cpp)
    target_link_libraries(crash_handler_probe PRIVATE b3r_windows_runtime)
    if(MSVC)
      target_compile_options(crash_handler_probe PRIVATE /W4 /permissive- /Zc:__cplusplus)
    endif()

    add_executable(crash_handler_windows_tests tests/crash_handler_windows_tests.cpp)
    target_link_libraries(crash_handler_windows_tests PRIVATE b3r_windows_runtime)
    if(MSVC)
      target_compile_options(crash_handler_windows_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
    endif()
    add_test(
      NAME crash_handler_windows_tests
      COMMAND crash_handler_windows_tests $<TARGET_FILE:crash_handler_probe>
    )
    set_tests_properties(crash_handler_windows_tests PROPERTIES TIMEOUT 30)
  endif()
endif()
