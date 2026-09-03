#include "platform/windows/qpc_clock.h"

#include <windows.h>

#include <cstdlib>
#include <iostream>

int main() {
    using b3r::platform::windows::QpcClock;

    if (QpcClock::frequency_hz() <= 0.0) {
        std::cerr << "QPC frequency must be positive\n";
        return EXIT_FAILURE;
    }

    const double before = QpcClock::now_seconds();
    Sleep(2);
    const double after = QpcClock::now_seconds();
    if (!(after > before)) {
        std::cerr << "QPC clock must be monotonic\n";
        return EXIT_FAILURE;
    }

    std::cout << "qpc_clock_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
