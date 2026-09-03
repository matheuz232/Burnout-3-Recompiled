#include "platform/windows/crash_handler.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>

int wmain() {
    constexpr std::uint32_t kPs2Address = 0x00B3C0DEu;
    constexpr std::uintptr_t kNativeAddress = static_cast<std::uintptr_t>(0x1234ABCDu);

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    b3r::platform::windows::CrashHandler::install();
    b3r::platform::windows::CrashHandler::set_execution_markers(kPs2Address, kNativeAddress);

    RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    return EXIT_FAILURE;
}
