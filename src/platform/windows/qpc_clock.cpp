#include "platform/windows/qpc_clock.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace b3r::platform::windows {
namespace {

LARGE_INTEGER query_frequency() noexcept {
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency)) {
        frequency.QuadPart = 1;
    }
    return frequency;
}

const LARGE_INTEGER kFrequency = query_frequency();

} // namespace

double QpcClock::now_seconds() noexcept {
    LARGE_INTEGER counter{};
    if (!QueryPerformanceCounter(&counter)) {
        return 0.0;
    }
    return static_cast<double>(counter.QuadPart) / static_cast<double>(kFrequency.QuadPart);
}

double QpcClock::frequency_hz() noexcept {
    return static_cast<double>(kFrequency.QuadPart);
}

} // namespace b3r::platform::windows
