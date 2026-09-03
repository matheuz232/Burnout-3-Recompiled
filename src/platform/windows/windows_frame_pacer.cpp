#include "platform/windows/windows_frame_pacer.h"

#include "platform/windows/qpc_clock.h"

#include <intrin.h>
#include <mmsystem.h>
#include <stdexcept>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace b3r::platform::windows {

WindowsFramePacer::WindowsFramePacer(double target_hz)
    : schedule_(target_hz, QpcClock::now_seconds()) {
    timer_ = CreateWaitableTimerExW(
        nullptr,
        nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_ALL_ACCESS);

    if (timer_ != nullptr) {
        high_resolution_timer_ = true;
        return;
    }

    timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (timer_ == nullptr) {
        throw std::runtime_error("Failed to create Windows waitable timer");
    }

    if (timeBeginPeriod(1) == TIMERR_NOERROR) {
        fallback_timer_resolution_ = true;
    }
}

WindowsFramePacer::~WindowsFramePacer() {
    if (fallback_timer_resolution_) {
        timeEndPeriod(1);
    }
    if (timer_ != nullptr) {
        CloseHandle(timer_);
    }
}

double WindowsFramePacer::wait_for_next_frame() {
    const double deadline = schedule_.deadline_seconds();
    double now = QpcClock::now_seconds();

    if (now < deadline) {
        const double remaining = deadline - now;
        const double coarse_wait = remaining - spin_threshold_seconds_;

        if (coarse_wait > 0.0) {
            LARGE_INTEGER due_time{};
            const auto hundred_ns = static_cast<LONGLONG>(coarse_wait * 10'000'000.0);
            due_time.QuadPart = -((hundred_ns > 0) ? hundred_ns : 1);

            if (SetWaitableTimer(timer_, &due_time, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer_, INFINITE);
            }
        }

        do {
            YieldProcessor();
            now = QpcClock::now_seconds();
        } while (now < deadline);
    }

    schedule_.advance_after_frame(now);
    return now;
}

double WindowsFramePacer::target_period_seconds() const noexcept {
    return schedule_.period_seconds();
}

bool WindowsFramePacer::using_high_resolution_timer() const noexcept {
    return high_resolution_timer_;
}

} // namespace b3r::platform::windows
