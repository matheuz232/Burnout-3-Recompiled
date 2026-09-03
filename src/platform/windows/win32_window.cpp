#include "platform/windows/win32_window.h"

namespace b3r::platform::windows {
namespace {
constexpr wchar_t kWindowClass[] = L"Burnout3RecompiledWindowClass";
constexpr wchar_t kWindowTitle[] = L"Burnout 3 Recompiled - Test Build 0.1";
}

Win32Window::~Win32Window() {
    if (hwnd_ != nullptr && IsWindow(hwnd_)) {
        DestroyWindow(hwnd_);
    }
    if (instance_ != nullptr) {
        UnregisterClassW(kWindowClass, instance_);
    }
}

bool Win32Window::create(HINSTANCE instance, int width, int height, bool fullscreen) {
    instance_ = instance;

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = &Win32Window::window_proc;
    window_class.hInstance = instance_;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kWindowClass;

    if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    DWORD style = fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    RECT rect{0, 0, width, height};

    if (fullscreen) {
        rect.right = GetSystemMetrics(SM_CXSCREEN);
        rect.bottom = GetSystemMetrics(SM_CYSCREEN);
    } else if (!AdjustWindowRectEx(&rect, style, FALSE, 0)) {
        return false;
    }

    hwnd_ = CreateWindowExW(
        0,
        kWindowClass,
        kWindowTitle,
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        instance_,
        nullptr);

    if (hwnd_ == nullptr) {
        return false;
    }

    ShowWindow(hwnd_, SW_SHOWDEFAULT);
    UpdateWindow(hwnd_);
    return true;
}

bool Win32Window::pump_messages() const noexcept {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return true;
}

HWND Win32Window::handle() const noexcept {
    return hwnd_;
}

LRESULT CALLBACK Win32Window::window_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_KEYDOWN:
        if (w_param == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, message, w_param, l_param);
}

} // namespace b3r::platform::windows
