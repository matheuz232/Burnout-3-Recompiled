#include "core/runtime_options.h"

namespace b3r::core {

RuntimeOptions::ParseResult RuntimeOptions::parse(const std::vector<std::wstring_view>& args) {
    ParseResult result{};

    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto arg = args[i];
        if (arg == L"--debug") {
            result.options.debug = true;
        } else if (arg == L"--verbose") {
            result.options.verbose = true;
        } else if (arg == L"--windowed") {
            result.options.windowed = true;
            result.options.fullscreen = false;
        } else if (arg == L"--fullscreen") {
            result.options.fullscreen = true;
            result.options.windowed = false;
        } else if (arg == L"--disable-audio") {
            result.options.disable_audio = true;
        } else if (arg == L"--frame-stats") {
            result.options.frame_stats = true;
        } else if (arg == L"--game-data" || arg == L"--log-level") {
            if (i + 1 >= args.size()) {
                result.ok = false;
                result.error = std::wstring(arg) + L" requires a value";
                return result;
            }
            const auto value = args[++i];
            if (arg == L"--game-data") {
                result.options.game_data = value;
            } else {
                result.options.log_level = value;
            }
        } else {
            result.ok = false;
            result.error = L"Unknown argument: " + std::wstring(arg);
            return result;
        }
    }

    return result;
}

} // namespace b3r::core
