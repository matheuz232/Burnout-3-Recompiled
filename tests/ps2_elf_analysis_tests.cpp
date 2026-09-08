#include "analysis/elf32_metadata.h"
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

void put_bytes(Bytes& bytes, std::size_t offset, const std::string& value) {
    for (std::size_t i = 0; i < value.size(); ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>(value[i]);
    }
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

Bytes make_metadata_elf(std::uint32_t symbol_section_type = 2u) {
    constexpr std::size_t kSectionTable = 0x100;
    constexpr std::size_t kShStr = 0x1A0;
    constexpr std::size_t kStr = 0x1C0;
    constexpr std::size_t kSym = 0x1E0;

    Bytes bytes(0x240, 0);
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
    put_u32(bytes, 32, static_cast<std::uint32_t>(kSectionTable));
    put_u16(bytes, 40, 52);
    put_u16(bytes, 46, 40);
    put_u16(bytes, 48, 4);
    put_u16(bytes, 50, 1);

    const std::string section_names = std::string("\0.shstrtab\0.strtab\0.symtab\0", 27);
    put_bytes(bytes, kShStr, section_names);
    const std::string symbol_names = std::string("\0padRead\0_padEnd\0", 17);
    put_bytes(bytes, kStr, symbol_names);

    const auto write_section = [&](std::size_t index,
                                   std::uint32_t name,
                                   std::uint32_t type,
                                   std::uint32_t offset,
                                   std::uint32_t size,
                                   std::uint32_t link,
                                   std::uint32_t entsize) {
        const std::size_t base = kSectionTable + index * 40u;
        put_u32(bytes, base + 0u, name);
        put_u32(bytes, base + 4u, type);
        put_u32(bytes, base + 16u, offset);
        put_u32(bytes, base + 20u, size);
        put_u32(bytes, base + 24u, link);
        put_u32(bytes, base + 36u, entsize);
    };

    write_section(0, 0, 0, 0, 0, 0, 0);
    write_section(1, 1, 3, static_cast<std::uint32_t>(kShStr),
                  static_cast<std::uint32_t>(section_names.size()), 0, 0);
    write_section(2, 11, 3, static_cast<std::uint32_t>(kStr),
                  static_cast<std::uint32_t>(symbol_names.size()), 0, 0);
    write_section(3, 19, symbol_section_type, static_cast<std::uint32_t>(kSym), 48, 2, 16);

    // Symbol 0 is the required null symbol.
    const std::size_t pad_read = kSym + 16u;
    put_u32(bytes, pad_read + 0u, 1);
    put_u32(bytes, pad_read + 4u, 0x00102000u);
    put_u32(bytes, pad_read + 8u, 0x40u);
    bytes[pad_read + 12u] = 0x12u; // GLOBAL | STT_FUNC
    put_u16(bytes, pad_read + 14u, 1u);

    const std::size_t pad_end = kSym + 32u;
    put_u32(bytes, pad_end + 0u, 9);
    put_u32(bytes, pad_end + 4u, 0x00102100u);
    put_u32(bytes, pad_end + 8u, 0x20u);
    bytes[pad_end + 12u] = 0x10u; // GLOBAL | STT_NOTYPE
    put_u16(bytes, pad_end + 14u, 1u);

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

void run_metadata_tests() {
    using namespace b3r::analysis;

    {
        const auto metadata = parse_elf32_metadata(make_break_elf());
        expect(metadata.status == Elf32MetadataStatus::Absent,
               "ELF without section table must report metadata absent");
        expect(metadata.symbols.empty(), "absent metadata must not fabricate symbols");
    }

    for (const std::uint32_t table_type : {kShtSymtab, kShtDynsym}) {
        const auto metadata = parse_elf32_metadata(make_metadata_elf(table_type));
        expect(metadata.status == Elf32MetadataStatus::Available,
               "valid symbol metadata must be available");
        expect(metadata.symbols.size() == 3u, "symbol table must preserve all entries");
        expect(metadata.symbols[1].name == "padRead", "STT_FUNC symbol name must parse");
        expect(metadata.symbols[1].value == 0x00102000u, "STT_FUNC value must parse");
        expect(metadata.symbols[1].size == 0x40u, "STT_FUNC size must parse");
        expect(metadata.symbols[1].type == kSttFunc, "STT_FUNC type must parse");
        expect(metadata.symbols[2].name == "_padEnd", "STT_NOTYPE name must parse");
        expect(metadata.symbols[2].type == kSttNotype, "STT_NOTYPE type must parse");
    }

    {
        auto bytes = make_metadata_elf();
        bytes.resize(0x130);
        const auto metadata = parse_elf32_metadata(bytes);
        expect(metadata.status == Elf32MetadataStatus::Malformed,
               "truncated section table must be malformed");
    }

    {
        auto bytes = make_metadata_elf();
        put_u32(bytes, 0x100u + 40u + 0u, 0xFFFFu);
        const auto metadata = parse_elf32_metadata(bytes);
        expect(metadata.status == Elf32MetadataStatus::Malformed,
               "section-name offset outside shstrtab must be malformed");
    }

    {
        auto bytes = make_metadata_elf();
        put_u32(bytes, 0x100u + 3u * 40u + 24u, 99u);
        const auto metadata = parse_elf32_metadata(bytes);
        expect(metadata.status == Elf32MetadataStatus::Malformed,
               "symbol link outside section table must be malformed");
    }

    {
        auto bytes = make_metadata_elf();
        put_u32(bytes, 0x100u + 3u * 40u + 36u, 8u);
        const auto metadata = parse_elf32_metadata(bytes);
        expect(metadata.status == Elf32MetadataStatus::Malformed,
               "wrong symbol entry size must be malformed");
    }

    {
        auto bytes = make_metadata_elf();
        put_u32(bytes, 32u, 0xFFFFFFF0u);
        const auto metadata = parse_elf32_metadata(bytes);
        expect(metadata.status == Elf32MetadataStatus::Malformed,
               "huge section-table offset must fail safely");
    }

    {
        auto bytes = make_metadata_elf();
        constexpr std::size_t kSym = 0x1E0;
        const std::size_t duplicate = kSym + 32u;
        put_u32(bytes, duplicate + 0u, 1u);
        put_u32(bytes, duplicate + 4u, 0x00102000u);
        put_u32(bytes, duplicate + 8u, 0x40u);
        bytes[duplicate + 12u] = 0x12u;
        const auto metadata = parse_elf32_metadata(bytes);
        expect(metadata.status == Elf32MetadataStatus::Available,
               "duplicate symbols remain valid metadata");
        expect(metadata.symbols[1].name == metadata.symbols[2].name,
               "duplicate symbols must be preserved rather than collapsed");
    }
}

} // namespace

int main() {
    using namespace b3r::analysis;

    run_metadata_tests();

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
        "UNKNOWN_SITES 0\n"
        "DIRECT_CALL_TARGETS 0\n"
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
