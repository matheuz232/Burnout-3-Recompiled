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

void expect_client_size(HWND hwnd, LONG width, LONG height, const std::string& context) {
    RECT client{};
    expect(GetClientRect(hwnd, &client) != FALSE, context + ": GetClientRect failed");
    expect(client.right - client.left == width, context + ": client width mismatch");
    expect(client.bottom - client.top == height, context + ": client height mismatch");
}

void expect_shutdown(b3r::platform::windows::Win32Window& window,
                     HWND original_hwnd,
                     const std::string& context) {
    bool running = true;
    for (int attempt = 0; attempt < 100 && running; ++attempt) {
        running = window.pump_messages();
        if (running) {
            Sleep(1);
        }
    }

    expect(!running, context + ": message pump did not observe WM_QUIT");
    expect(IsWindow(original_hwnd) == FALSE, context + ": HWND still exists after shutdown");
    expect(window.handle() == nullptr, context + ": handle() did not clear after shutdown");
}

void test_windowed_lifecycle(HINSTANCE instance) {
    b3r::platform::windows::Win32Window window;
    expect(window.create(instance, 640, 360, false), "windowed create failed");

    const HWND hwnd = window.handle();
    expect(hwnd != nullptr, "windowed handle returned null after create");
    expect(IsWindow(hwnd) != FALSE, "windowed HWND is not live");
    expect_client_size(hwnd, 640, 360, "windowed");

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    expect((style & WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW,
           "windowed style is missing WS_OVERLAPPEDWINDOW");

    expect(PostMessageW(hwnd, WM_CLOSE, 0, 0) != FALSE, "failed to post windowed WM_CLOSE");
    expect_shutdown(window, hwnd, "windowed WM_CLOSE");

    expect(window.create(instance, 320, 180, false), "windowed recreate failed");
    const HWND recreated = window.handle();
    expect(recreated != nullptr, "recreated handle returned null");
    expect(IsWindow(recreated) != FALSE, "recreated HWND is not live");
    expect_client_size(recreated, 320, 180, "recreated windowed");

    expect(PostMessageW(recreated, WM_CLOSE, 0, 0) != FALSE, "failed to post recreated WM_CLOSE");
    expect_shutdown(window, recreated, "recreated WM_CLOSE");
}

void test_escape_shutdown(HINSTANCE instance) {
    b3r::platform::windows::Win32Window window;
    expect(window.create(instance, 400, 225, false), "escape-path create failed");

    const HWND hwnd = window.handle();
    expect(hwnd != nullptr && IsWindow(hwnd) != FALSE, "escape-path HWND is not live");
    expect(PostMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0) != FALSE, "failed to post VK_ESCAPE");
    expect_shutdown(window, hwnd, "VK_ESCAPE");
}

void test_fullscreen_shape(HINSTANCE instance) {
    b3r::platform::windows::Win32Window window;
    expect(window.create(instance, 320, 180, true), "fullscreen create failed");

    const HWND hwnd = window.handle();
    expect(hwnd != nullptr && IsWindow(hwnd) != FALSE, "fullscreen HWND is not live");

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    expect((style & WS_POPUP) == WS_POPUP, "fullscreen style is missing WS_POPUP");
    expect((style & WS_OVERLAPPEDWINDOW) == 0, "fullscreen style unexpectedly has WS_OVERLAPPEDWINDOW");

    const LONG screen_width = GetSystemMetrics(SM_CXSCREEN);
    const LONG screen_height = GetSystemMetrics(SM_CYSCREEN);
    expect(screen_width > 0 && screen_height > 0, "screen metrics are invalid");
    expect_client_size(hwnd, screen_width, screen_height, "fullscreen");

    expect(PostMessageW(hwnd, WM_CLOSE, 0, 0) != FALSE, "failed to post fullscreen WM_CLOSE");
    expect_shutdown(window, hwnd, "fullscreen WM_CLOSE");
}

} // namespace

int wmain() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    expect(instance != nullptr, "GetModuleHandleW returned null");

    test_windowed_lifecycle(instance);
    test_escape_shutdown(instance);
    test_fullscreen_shape(instance);

    std::cout << "win32_window_smoke_tests: PASS\n";
    return EXIT_SUCCESS;
}
