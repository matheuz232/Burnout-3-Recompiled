#include "tools/burnout3_analyze_app.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

Bytes make_elf(const std::vector<std::uint32_t>& words) {
    constexpr std::uint32_t kProgramHeaderOffset = 52;
    constexpr std::uint32_t kSegmentOffset = 0x100;
    constexpr std::uint32_t kEntry = 0x00100000;
    const auto payload_size = static_cast<std::uint32_t>(words.size() * 4u);

    Bytes bytes(kSegmentOffset + payload_size, 0);
    bytes[0] = 0x7F;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 1;
    bytes[5] = 1;
    bytes[6] = 1;

    put_u16(bytes, 16, 2);
    put_u16(bytes, 18, 8);
    put_u32(bytes, 20, 1);
    put_u32(bytes, 24, kEntry);
    put_u32(bytes, 28, kProgramHeaderOffset);
    put_u16(bytes, 40, 52);
    put_u16(bytes, 42, 32);
    put_u16(bytes, 44, 1);

    put_u32(bytes, kProgramHeaderOffset + 0, 1);
    put_u32(bytes, kProgramHeaderOffset + 4, kSegmentOffset);
    put_u32(bytes, kProgramHeaderOffset + 8, kEntry);
    put_u32(bytes, kProgramHeaderOffset + 12, kEntry);
    put_u32(bytes, kProgramHeaderOffset + 16, payload_size);
    put_u32(bytes, kProgramHeaderOffset + 20, payload_size);
    put_u32(bytes, kProgramHeaderOffset + 24, 5);
    put_u32(bytes, kProgramHeaderOffset + 28, 0x1000);

    for (std::size_t i = 0; i < words.size(); ++i) {
        put_u32(bytes, kSegmentOffset + i * 4u, words[i]);
    }
    return bytes;
}

Bytes make_break_elf() {
    return make_elf({0x0000000Du});
}

Bytes make_direct_call_elf() {
    constexpr std::uint32_t kTarget = 0x00100100u;
    std::vector<std::uint32_t> words(65u, 0u);
    words[0] = (0x03u << 26u) | ((kTarget >> 2u) & 0x03FFFFFFu); // JAL target
    words[1] = 0u;                                               // delay slot
    words[2] = 0x0000000Du;                                      // continuation BREAK
    words[64] = 0x0000000Du;                                     // callee BREAK
    return make_elf(words);
}

[[noreturn]] void fail(const char* message) {
    std::cerr << "burnout3_analyze_app_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void write_bytes(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        fail("failed to create synthetic ELF fixture");
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        fail("failed to write synthetic ELF fixture");
    }
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    using namespace b3r::tools;

    const auto temp = std::filesystem::temp_directory_path();
    const auto elf_path = temp / "b3r_burnout3_analyze_test.elf";
    const auto direct_call_elf_path = temp / "b3r_burnout3_direct_call_test.elf";
    const auto report_path = temp / "b3r_burnout3_analyze_test.txt";
    std::filesystem::remove(elf_path);
    std::filesystem::remove(direct_call_elf_path);
    std::filesystem::remove(report_path);
    write_bytes(elf_path, make_break_elf());
    write_bytes(direct_call_elf_path, make_direct_call_elf());

    {
        Burnout3AnalyzeOptions options{};
        options.elf_path = elf_path.string();
        options.max_blocks = 32;
        std::ostringstream stdout_stream;
        const auto result = run_burnout3_analyze(options, stdout_stream);
        expect(result.ok(), "valid external ELF must analyze successfully");
        expect(stdout_stream.str().find("ENTRY 0x00100000\n") == 0u,
               "stdout report must begin with the ELF entry point");
        expect(stdout_stream.str().find("BLOCK 0x00100000 END Trap") != std::string::npos,
               "stdout report must contain the reachable BREAK block");
    }

    {
        Burnout3AnalyzeOptions options{};
        options.elf_path = elf_path.string();
        options.output_path = report_path.string();
        std::ostringstream stdout_stream;
        const auto result = run_burnout3_analyze(options, stdout_stream);
        expect(result.ok(), "output-file analysis must succeed");
        expect(stdout_stream.str().empty(), "--output must keep the report off stdout");
        const auto report = read_text(report_path);
        expect(report.find("ENTRY 0x00100000\n") == 0u,
               "output file must contain the deterministic report");
    }

    {
        Burnout3AnalyzeOptions options{};
        options.elf_path = direct_call_elf_path.string();
        options.max_blocks = 32;
        std::ostringstream stdout_stream;
        const auto result = run_burnout3_analyze(options, stdout_stream);
        expect(result.ok(), "default direct-call fixture analysis must succeed");
        expect(stdout_stream.str().find("BLOCK 0x00100100") == std::string::npos,
               "default app analysis must keep direct callees out of reachable blocks");
    }

    {
        Burnout3AnalyzeOptions options{};
        options.elf_path = direct_call_elf_path.string();
        options.max_blocks = 32;
        options.follow_direct_calls = true;
        std::ostringstream stdout_stream;
        const auto result = run_burnout3_analyze(options, stdout_stream);
        expect(result.ok(), "follow-direct-calls app analysis must succeed");
        expect(stdout_stream.str().find("BLOCK 0x00100100 END Trap") != std::string::npos,
               "app must propagate follow-direct-calls into reachability");
        expect(stdout_stream.str().find("CALL 0x00100000 PC 0x00100000 DIRECT 0x00100100") != std::string::npos,
               "followed callee must remain represented as direct-call evidence");
    }

    {
        Burnout3AnalyzeOptions options{};
        options.elf_path = (temp / "b3r_missing_burnout3.elf").string();
        std::ostringstream stdout_stream;
        const auto result = run_burnout3_analyze(options, stdout_stream);
        expect(!result.ok(), "missing ELF file must fail");
        expect(result.error == Burnout3AnalyzeRunError::InputOpenFailed,
               "missing ELF must report an input-open error");
    }

    std::filesystem::remove(elf_path);
    std::filesystem::remove(direct_call_elf_path);
    std::filesystem::remove(report_path);

    std::cout << "burnout3_analyze_app_tests: PASS\n";
    return EXIT_SUCCESS;
}
