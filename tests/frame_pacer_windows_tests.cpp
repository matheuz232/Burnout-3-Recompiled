#include "platform/windows/qpc_clock.h"
#include "platform/windows/windows_frame_pacer.h"

#include <cstdlib>
#include <iostream>

int main() {
    using namespace b3r::platform::windows;

    WindowsFramePacer pacer{120.0};
    constexpr int kFrames = 60;
    const double begin = QpcClock::now_seconds();
    for (int i = 0; i < kFrames; ++i) {
        (void)pacer.wait_for_next_frame();
    }
    const double end = QpcClock::now_seconds();
    const double fps = static_cast<double>(kFrames) / (end - begin);

    if (fps < 105.0 || fps > 135.0) {
        std::cerr << "unexpected pacing average: " << fps << " FPS\n";
        return EXIT_FAILURE;
    }

    std::cout << "frame_pacer_windows_tests: PASS (" << fps << " FPS)\n";
    return EXIT_SUCCESS;
}
