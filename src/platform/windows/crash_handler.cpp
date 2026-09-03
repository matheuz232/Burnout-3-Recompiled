#include "platform/windows/crash_handler.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <cstdio>
#include <cwchar>

namespace b3r::platform::windows {
namespace {

std::atomic<std::uint32_t> g_last_ps2_address{};
std::atomic<std::uintptr_t> g_last_native_address{};

void write_crash_state(EXCEPTION_POINTERS* exception_info) noexcept {
    const HANDLE file = CreateFileW(
        L"crash_dumps\\last_crash.txt",
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    const auto exception_code = exception_info && exception_info->ExceptionRecord
        ? exception_info->ExceptionRecord->ExceptionCode
        : 0u;
    const auto exception_address = exception_info && exception_info->ExceptionRecord
        ? reinterpret_cast<std::uintptr_t>(exception_info->ExceptionRecord->ExceptionAddress)
        : 0u;

    char buffer[640]{};
    const int length = std::snprintf(
        buffer,
        sizeof(buffer),
        "exception_code=0x%08lX\r\nexception_address=0x%p\r\nlast_ps2_address=0x%08X\r\nlast_native_address=0x%p\r\n",
        static_cast<unsigned long>(exception_code),
        reinterpret_cast<void*>(exception_address),
        g_last_ps2_address.load(std::memory_order_relaxed),
        reinterpret_cast<void*>(g_last_native_address.load(std::memory_order_relaxed)));

    if (length > 0) {
        DWORD written{};
        WriteFile(file, buffer, static_cast<DWORD>(length), &written, nullptr);
    }
    CloseHandle(file);
}

LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS* exception_info) noexcept {
    CreateDirectoryW(L"crash_dumps", nullptr);

    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t dump_path[MAX_PATH]{};
    swprintf_s(
        dump_path,
        L"crash_dumps\\Burnout3Recompiled_%04u%02u%02u_%02u%02u%02u.dmp",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond);

    const HANDLE dump_file = CreateFileW(
        dump_path,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (dump_file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION dump_exception{};
        dump_exception.ThreadId = GetCurrentThreadId();
        dump_exception.ExceptionPointers = exception_info;
        dump_exception.ClientPointers = FALSE;

        const auto dump_type = static_cast<MINIDUMP_TYPE>(
            MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory);

        MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            dump_file,
            dump_type,
            exception_info ? &dump_exception : nullptr,
            nullptr,
            nullptr);
        CloseHandle(dump_file);
    }

    write_crash_state(exception_info);
    OutputDebugStringW(L"[ERROR] Burnout3Recompiled crashed; minidump attempted in crash_dumps.\n");
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void CrashHandler::install() noexcept {
    SetUnhandledExceptionFilter(&unhandled_exception_filter);
}

void CrashHandler::set_execution_markers(std::uint32_t ps2_address, std::uintptr_t native_address) noexcept {
    g_last_ps2_address.store(ps2_address, std::memory_order_relaxed);
    g_last_native_address.store(native_address, std::memory_order_relaxed);
}

} // namespace b3r::platform::windows
