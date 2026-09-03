#include "core/runtime_options.h"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, const char* label) {
    if (!condition) {
        std::cerr << label << ": expected true\n";
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using b3r::core::RuntimeOptions;

    const std::vector<std::wstring_view> args{
        L"Burnout3Recompiled_Test.exe",
        L"--debug",
        L"--verbose",
        L"--windowed",
        L"--game-data", L"D:\\Games\\Burnout3\\data",
        L"--log-level", L"trace",
        L"--disable-audio",
        L"--frame-stats",
    };

    const auto parsed = RuntimeOptions::parse(args);
    expect(parsed.ok, "valid arguments parse");
    expect(parsed.options.debug, "debug");
    expect(parsed.options.verbose, "verbose");
    expect(parsed.options.windowed, "windowed");
    expect(!parsed.options.fullscreen, "not fullscreen");
    expect(parsed.options.disable_audio, "disable audio");
    expect(parsed.options.frame_stats, "frame stats");
    expect(parsed.options.game_data == L"D:\\Games\\Burnout3\\data", "game data");
    expect(parsed.options.log_level == L"trace", "log level");

    const std::vector<std::wstring_view> bad_args{L"app.exe", L"--game-data"};
    const auto bad = RuntimeOptions::parse(bad_args);
    expect(!bad.ok, "missing value rejected");

    std::cout << "runtime_options_tests: PASS\n";
    return EXIT_SUCCESS;
}
