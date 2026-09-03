#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace b3r::platform::windows {

class Win32Window {
public:
    Win32Window() = default;
    ~Win32Window();

    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;

    [[nodiscard]] bool create(HINSTANCE instance, int width, int height, bool fullscreen);
    [[nodiscard]] bool pump_messages() const noexcept;
    [[nodiscard]] HWND handle() const noexcept;

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    HINSTANCE instance_{};
    HWND hwnd_{};
};

} // namespace b3r::platform::windows
