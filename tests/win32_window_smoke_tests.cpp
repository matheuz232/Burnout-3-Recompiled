#include "platform/windows/win32_window.h"

#include <windows.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "win32_window_smoke_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

} // namespace

int wmain() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    expect(instance != nullptr, "GetModuleHandleW returned null");

    b3r::platform::windows::Win32Window window;
    expect(window.create(instance, 640, 360, false), "Win32Window::create failed");

    const HWND hwnd = window.handle();
    expect(hwnd != nullptr, "Win32Window::handle returned null after create");
    expect(IsWindow(hwnd) != FALSE, "created HWND is not a live window");

    expect(PostMessageW(hwnd, WM_CLOSE, 0, 0) != FALSE, "failed to post WM_CLOSE");

    bool running = true;
    for (int attempt = 0; attempt < 100 && running; ++attempt) {
        running = window.pump_messages();
        if (running) {
            Sleep(1);
        }
    }

    expect(!running, "message pump did not observe WM_QUIT after WM_CLOSE");
    expect(IsWindow(hwnd) == FALSE, "HWND still exists after WM_CLOSE shutdown");

    std::cout << "win32_window_smoke_tests: PASS\n";
    return EXIT_SUCCESS;
}
