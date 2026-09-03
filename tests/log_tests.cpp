#include "debug/log.h"

#include <cstdlib>
#include <iostream>

int main() {
    const auto line = b3r::debug::format_log_line("BOOT", "runtime initialized");
    if (line != "[BOOT] runtime initialized") {
        std::cerr << "unexpected formatted log line: " << line << '\n';
        return EXIT_FAILURE;
    }

    const auto stub = b3r::debug::format_unimplemented(0x00123450u);
    if (stub != "Unimplemented function called: 0x00123450") {
        std::cerr << "unexpected stub log line: " << stub << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "log_tests: PASS\n";
    return EXIT_SUCCESS;
}
