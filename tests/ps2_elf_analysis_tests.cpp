#include "analysis/ps2_elf_analysis.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
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

Bytes make_break_elf() {
    constexpr std::uint32_t kProgramHeaderOffset = 52;
    constexpr std::uint32_t kSegmentOffset = 0x100;
    constexpr std::uint32_t kEntry = 0x00100000;

    Bytes bytes(0x104, 0);
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
    put_u32(bytes, kProgramHeaderOffset + 16, 4);
    put_u32(bytes, kProgramHeaderOffset + 20, 4);
    put_u32(bytes, kProgramHeaderOffset + 24, 5);
    put_u32(bytes, kProgramHeaderOffset + 28, 0x1000);

    put_u32(bytes, kSegmentOffset, 0x0000000Du); // BREAK
    return bytes;
}

[[noreturn]] void fail(const char* message) {
    std::cerr << "ps2_elf_analysis_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

} // namespace

int main() {
    using namespace b3r::analysis;

    const std::string expected =
        "ENTRY 0x00100000\n"
        "BLOCKS 1\n"
        "INSTRUCTIONS 1\n"
        "DECODED 1\n"
        "UNKNOWN 0\n"
        "CALLS 0\n"
        "INDIRECT_EXITS 0\n"
        "CFG_ISSUES 0\n"
        "INSTRUCTION_HISTOGRAM 1\n"
        "  BREAK 1\n"
        "UNKNOWN_PRIMARY_OPCODES 0\n"
        "\n"
        "BLOCK 0x00100000 END Trap\n"
        "  0x00100000 BREAK RAW 0x0000000D\n"
        "\n";

    {
        const auto result = analyze_ps2_elf(make_break_elf());
        expect(result.ok(), "valid executable ELF must produce an analysis report");
        expect(result.report.has_value(), "successful analysis must contain a report");
        expect(*result.report == expected, "pipeline report must expose coverage metrics end-to-end");
        expect(result.error == Ps2ElfAnalysisError::None, "successful analysis must not report an error");
    }

    {
        auto bytes = make_break_elf();
        bytes[0] = 0;
        const auto result = analyze_ps2_elf(bytes);
        expect(!result.ok(), "invalid ELF must fail before memory/CFG analysis");
        expect(result.error == Ps2ElfAnalysisError::ElfParseFailed,
               "invalid ELF must be classified as an ELF parse failure");
        expect(!result.report.has_value(), "failed analysis must not return a report");
    }

    std::cout << "ps2_elf_analysis_tests: PASS\n";
    return EXIT_SUCCESS;
}
