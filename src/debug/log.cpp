#include "debug/log.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace b3r::debug {
namespace {
std::mutex g_mutex;
std::ofstream g_file;
}

std::string format_log_line(std::string_view tag, std::string_view message) {
    std::string line;
    line.reserve(tag.size() + message.size() + 4);
    line.push_back('[');
    line.append(tag);
    line.append("] ");
    line.append(message);
    return line;
}

std::string format_unimplemented(std::uint32_t ps2_address) {
    std::ostringstream stream;
    stream << "Unimplemented function called: 0x"
           << std::hex << std::uppercase << std::setfill('0') << std::setw(8)
           << ps2_address;
    return stream.str();
}

bool Log::initialize(const std::filesystem::path& file_path) {
    std::scoped_lock lock(g_mutex);
    g_file.open(file_path, std::ios::out | std::ios::trunc);
    return g_file.is_open();
}

void Log::shutdown() {
    std::scoped_lock lock(g_mutex);
    if (g_file.is_open()) {
        g_file.flush();
        g_file.close();
    }
}

void Log::write(std::string_view tag, std::string_view message) {
    const auto line = format_log_line(tag, message);
    std::scoped_lock lock(g_mutex);
    std::clog << line << '\n';
    if (g_file.is_open()) {
        g_file << line << '\n';
        g_file.flush();
    }
}

void Log::unimplemented(std::uint32_t ps2_address) {
    write("STUB", format_unimplemented(ps2_address));
}

} // namespace b3r::debug
