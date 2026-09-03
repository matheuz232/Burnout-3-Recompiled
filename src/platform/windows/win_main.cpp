#include "core/frame_stats.h"
#include "core/runtime_options.h"
#include "debug/log.h"
#include "platform/windows/crash_handler.h"
#include "platform/windows/qpc_clock.h"
#include "platform/windows/win32_window.h"
#include "platform/windows/windows_frame_pacer.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <exception>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void create_test_console() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        AllocConsole();
    }

    FILE* stream{};
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
}

std::string utf8_from_wide(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (size <= 0) {
        return "<wide-string conversion failed>";
    }

    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

b3r::core::RuntimeOptions::ParseResult parse_command_line() {
    int argc{};
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        b3r::core::RuntimeOptions::ParseResult result{};
        result.ok = false;
        result.error = L"CommandLineToArgvW failed";
        return result;
    }

    std::vector<std::wstring_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        arguments.emplace_back(argv[i]);
    }

    auto result = b3r::core::RuntimeOptions::parse(arguments);
    LocalFree(argv);
    return result;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    create_test_console();
    b3r::debug::Log::initialize();
    b3r::platform::windows::CrashHandler::install();

    b3r::debug::Log::write("BOOT", "Burnout 3 Recompiled - Test Build 0.1");
    b3r::debug::Log::write("BOOT", "Target runtime: Windows x86-64, 120.000 FPS");

    const auto parsed = parse_command_line();
    if (!parsed.ok) {
        b3r::debug::Log::write("ERROR", utf8_from_wide(parsed.error));
        MessageBoxW(nullptr, parsed.error.c_str(), L"Burnout3Recompiled command-line error", MB_OK | MB_ICONERROR);
        b3r::debug::Log::shutdown();
        return 2;
    }

    if (!parsed.options.game_data.empty()) {
        b3r::debug::Log::write("GAME", "External game-data path: " + utf8_from_wide(parsed.options.game_data));
    } else {
        b3r::debug::Log::write("GAME", "No --game-data path supplied; bootstrap will run without proprietary assets.");
    }

    b3r::platform::windows::Win32Window window;
    if (!window.create(instance, 1280, 720, parsed.options.fullscreen)) {
        b3r::debug::Log::write("ERROR", "Failed to create Win32 window");
        b3r::debug::Log::shutdown();
        return 3;
    }
    b3r::debug::Log::write("BOOT", "Win32 window created");

    try {
        b3r::platform::windows::WindowsFramePacer pacer{120.0};
        b3r::core::FrameStats frame_stats;

        std::ostringstream timer_info;
        timer_info << "QPC frequency=" << b3r::platform::windows::QpcClock::frequency_hz()
                   << " Hz, high-resolution waitable timer="
                   << (pacer.using_high_resolution_timer() ? "yes" : "fallback");
        b3r::debug::Log::write("BOOT", timer_info.str());

        double last_report = b3r::platform::windows::QpcClock::now_seconds();
        while (window.pump_messages()) {
            const double frame_begin = b3r::platform::windows::QpcClock::now_seconds();

            const double simulation_begin = b3r::platform::windows::QpcClock::now_seconds();
            // Game simulation is intentionally not implemented in the bootstrap milestone.
            const double simulation_end = b3r::platform::windows::QpcClock::now_seconds();

            const double render_begin = b3r::platform::windows::QpcClock::now_seconds();
            // Graphics initialization/rendering starts in the next milestone.
            const double render_end = b3r::platform::windows::QpcClock::now_seconds();

            const double frame_end = pacer.wait_for_next_frame();
            frame_stats.add_frame(
                frame_end - frame_begin,
                simulation_end - simulation_begin,
                render_end - render_begin,
                0);

            if (parsed.options.frame_stats && (frame_end - last_report) >= 1.0) {
                const auto stats = frame_stats.snapshot();
                std::ostringstream line;
                line.setf(std::ios::fixed);
                line.precision(3);
                line << "FPS=" << stats.fps
                     << " frame=" << stats.frame_time_ms << "ms"
                     << " sim=" << stats.simulation_time_ms << "ms"
                     << " render=" << stats.render_time_ms << "ms"
                     << " steps=" << stats.simulation_steps;
                b3r::debug::Log::write("GAME", line.str());
                last_report = frame_end;
            }
        }
    } catch (const std::exception& exception) {
        b3r::debug::Log::write("ERROR", exception.what());
        b3r::debug::Log::shutdown();
        return 4;
    }

    b3r::debug::Log::write("BOOT", "Clean shutdown");
    b3r::debug::Log::shutdown();
    return 0;
}
