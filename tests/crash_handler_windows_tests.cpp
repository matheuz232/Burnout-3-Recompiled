#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr std::uint32_t kExpectedPs2Address = 0x00B3C0DEu;
constexpr std::uintptr_t kExpectedNativeAddress = static_cast<std::uintptr_t>(0x1234ABCDu);

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "crash_handler_windows_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        fail("unable to open crash state file: " + path.string());
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

std::optional<std::uint64_t> parse_hex_field(const std::string& text, std::string_view key) {
    const auto key_position = text.find(key);
    if (key_position == std::string::npos) {
        return std::nullopt;
    }

    const auto value_begin = key_position + key.size();
    const auto value_end = text.find_first_of("\r\n", value_begin);
    auto value = text.substr(value_begin, value_end - value_begin);
    if (value.starts_with("0x") || value.starts_with("0X")) {
        value.erase(0, 2);
    }
    if (value.empty()) {
        return std::nullopt;
    }

    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(value, &consumed, 16);
        if (consumed != value.size()) {
            return std::nullopt;
        }
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    namespace fs = std::filesystem;

    expect(argc == 2, "expected crash-handler probe path as the only argument");

    const fs::path probe_path = fs::absolute(argv[1]);
    expect(fs::exists(probe_path), "crash-handler probe executable does not exist");

    const fs::path test_root = fs::temp_directory_path() /
        (L"burnout3-crash-handler-test-" + std::to_wstring(GetCurrentProcessId()));

    std::error_code error;
    fs::remove_all(test_root, error);
    error.clear();
    fs::create_directories(test_root, error);
    expect(!error, "failed to create isolated crash-test working directory");

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    const BOOL created = CreateProcessW(
        probe_path.c_str(),
        nullptr,
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        test_root.c_str(),
        &startup,
        &process);
    if (!created) {
        fail("CreateProcessW failed with error " + std::to_string(GetLastError()));
    }

    const DWORD wait_result = WaitForSingleObject(process.hProcess, 15000);
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 0xDEADu);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        fail("crash-handler probe timed out");
    }
    if (wait_result != WAIT_OBJECT_0) {
        const DWORD wait_error = GetLastError();
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        fail("WaitForSingleObject failed with error " + std::to_string(wait_error));
    }

    DWORD exit_code = 0;
    const BOOL have_exit_code = GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    expect(have_exit_code != FALSE, "GetExitCodeProcess failed");
    expect(exit_code != 0 && exit_code != STILL_ACTIVE,
           "crash-handler probe must terminate abnormally");

    const fs::path crash_directory = test_root / L"crash_dumps";
    const fs::path state_path = crash_directory / L"last_crash.txt";
    expect(fs::is_regular_file(state_path), "last_crash.txt was not created");

    const std::string state = read_text_file(state_path);
    const auto exception_code = parse_hex_field(state, "exception_code=");
    const auto ps2_address = parse_hex_field(state, "last_ps2_address=");
    const auto native_address = parse_hex_field(state, "last_native_address=");

    expect(exception_code.has_value(), "crash state is missing a parseable exception code");
    expect(*exception_code == EXCEPTION_ACCESS_VIOLATION,
           "crash state exception code must be EXCEPTION_ACCESS_VIOLATION");
    expect(ps2_address.has_value(), "crash state is missing the PS2 execution marker");
    expect(*ps2_address == kExpectedPs2Address, "PS2 execution marker was not preserved");
    expect(native_address.has_value(), "crash state is missing the native execution marker");
    expect(*native_address == kExpectedNativeAddress, "native execution marker was not preserved");

    bool non_empty_dump_found = false;
    error.clear();
    for (const auto& entry : fs::directory_iterator(crash_directory, error)) {
        if (error) {
            break;
        }
        if (!entry.is_regular_file() || entry.path().extension() != L".dmp") {
            continue;
        }
        std::error_code size_error;
        if (entry.file_size(size_error) > 0 && !size_error) {
            non_empty_dump_found = true;
            break;
        }
    }
    expect(!error, "failed while enumerating crash dump directory");
    expect(non_empty_dump_found, "no non-empty minidump was produced");

    error.clear();
    fs::remove_all(test_root, error);
    expect(!error, "failed to clean isolated crash-test working directory");

    std::cout << "crash_handler_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
