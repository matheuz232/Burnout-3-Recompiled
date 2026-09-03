#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace b3r::debug {

[[nodiscard]] std::string format_log_line(std::string_view tag, std::string_view message);
[[nodiscard]] std::string format_unimplemented(std::uint32_t ps2_address);

class Log {
public:
    static bool initialize(const std::filesystem::path& file_path = "Burnout3Recompiled.log");
    static void shutdown();
    static void write(std::string_view tag, std::string_view message);
    static void unimplemented(std::uint32_t ps2_address);
};

} // namespace b3r::debug
