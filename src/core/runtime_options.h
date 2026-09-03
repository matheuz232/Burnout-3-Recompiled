#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace b3r::core {

struct RuntimeOptions {
    struct ParseResult;

    bool debug{};
    bool verbose{};
    bool windowed{true};
    bool fullscreen{};
    bool disable_audio{};
    bool frame_stats{};
    std::wstring game_data{};
    std::wstring log_level{L"info"};

    static ParseResult parse(const std::vector<std::wstring_view>& args);
};

struct RuntimeOptions::ParseResult {
    RuntimeOptions options{};
    bool ok{true};
    std::wstring error{};
};

} // namespace b3r::core
