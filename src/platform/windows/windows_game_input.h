#pragma once

#include "input/game_input.h"

#include <windows.h>
#include <Xinput.h>

namespace b3r::platform::windows {

using XInputGetStateFn = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
using GetAsyncKeyStateFn = SHORT (WINAPI*)(int);

struct WindowsGameInputApi {
    XInputGetStateFn xinput_get_state{};
    GetAsyncKeyStateFn get_async_key_state{};
};

class WindowsGameInput {
public:
    WindowsGameInput() noexcept;
    explicit WindowsGameInput(WindowsGameInputApi api) noexcept;

    [[nodiscard]] b3r::input::GameInputState poll() noexcept;

private:
    [[nodiscard]] b3r::input::GameInputState poll_keyboard() const noexcept;
    [[nodiscard]] b3r::input::GameInputState map_gamepad(const XINPUT_GAMEPAD& gamepad) const noexcept;
    [[nodiscard]] bool try_controller(DWORD index,
                                      b3r::input::GameInputState& state) noexcept;

    WindowsGameInputApi api_{};
    DWORD active_controller_{XUSER_MAX_COUNT};
};

} // namespace b3r::platform::windows
