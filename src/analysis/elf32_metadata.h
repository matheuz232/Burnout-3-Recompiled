#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace b3r::analysis {

enum class Elf32MetadataStatus : std::uint8_t {
    Available,
    Absent,
    Malformed,
};

struct Elf32Symbol {
    std::string name{};
    std::uint32_t value{};
    std::uint32_t size{};
    std::uint8_t binding{};
    std::uint8_t type{};
    std::uint16_t section_index{};
};

struct Elf32MetadataResult {
    Elf32MetadataStatus status{Elf32MetadataStatus::Absent};
    std::vector<Elf32Symbol> symbols{};
    std::string diagnostic{};
};

inline constexpr std::uint32_t kShtSymtab = 2u;
inline constexpr std::uint32_t kShtStrtab = 3u;
inline constexpr std::uint32_t kShtDynsym = 11u;
inline constexpr std::uint8_t kSttNotype = 0u;
inline constexpr std::uint8_t kSttFunc = 2u;
inline constexpr std::size_t kElf32SectionHeaderSize = 40u;
inline constexpr std::size_t kElf32SymbolSize = 16u;

namespace elf32_metadata_detail {

[[nodiscard]] inline bool checked_add(std::size_t a,
                                      std::size_t b,
                                      std::size_t& out) noexcept {
    if (b > std::numeric_limits<std::size_t>::max() - a) {
        return false;
    }
    out = a + b;
    return true;
}

[[nodiscard]] inline bool checked_mul(std::size_t a,
                                      std::size_t b,
                                      std::size_t& out) noexcept {
    if (a != 0u && b > std::numeric_limits<std::size_t>::max() / a) {
        return false;
    }
    out = a * b;
    return true;
}

[[nodiscard]] inline std::uint16_t read_u16_le(std::span<const std::uint8_t> bytes,
                                                std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1u]) << 8u);
}

[[nodiscard]] inline std::uint32_t read_u32_le(std::span<const std::uint8_t> bytes,
                                                std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
           (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
           (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
}

struct Section {
    std::uint32_t name{};
    std::uint32_t type{};
    std::uint32_t offset{};
    std::uint32_t size{};
    std::uint32_t link{};
    std::uint32_t entsize{};
};

[[nodiscard]] inline bool span_fits(std::span<const std::uint8_t> bytes,
                                    std::size_t offset,
                                    std::size_t size) noexcept {
    std::size_t end{};
    return checked_add(offset, size, end) && end <= bytes.size();
}

[[nodiscard]] inline bool read_string(std::span<const std::uint8_t> bytes,
                                      std::size_t table_offset,
                                      std::size_t table_size,
                                      std::uint32_t string_offset,
                                      std::string& out) {
    if (static_cast<std::size_t>(string_offset) >= table_size) {
        return false;
    }

    const std::size_t begin = table_offset + static_cast<std::size_t>(string_offset);
    const std::size_t end = table_offset + table_size;
    std::size_t cursor = begin;
    while (cursor < end && bytes[cursor] != 0u) {
        ++cursor;
    }
    if (cursor == end) {
        return false;
    }

    out.assign(reinterpret_cast<const char*>(bytes.data() + begin), cursor - begin);
    return true;
}

[[nodiscard]] inline Elf32MetadataResult malformed(std::string message) {
    Elf32MetadataResult result{};
    result.status = Elf32MetadataStatus::Malformed;
    result.diagnostic = std::move(message);
    return result;
}

} // namespace elf32_metadata_detail

[[nodiscard]] inline Elf32MetadataResult
parse_elf32_metadata(std::span<const std::uint8_t> bytes) {
    using namespace elf32_metadata_detail;

    if (bytes.size() < 52u) {
        return malformed("ELF32 header is truncated");
    }

    const std::uint32_t section_offset = read_u32_le(bytes, 32u);
    const std::uint16_t section_entry_size = read_u16_le(bytes, 46u);
    const std::uint16_t section_count = read_u16_le(bytes, 48u);
    const std::uint16_t section_name_index = read_u16_le(bytes, 50u);

    if (section_offset == 0u || section_count == 0u) {
        return {};
    }
    if (section_entry_size != kElf32SectionHeaderSize) {
        return malformed("ELF32 section header entry size is not 40 bytes");
    }

    std::size_t section_bytes{};
    if (!checked_mul(static_cast<std::size_t>(section_count),
                     kElf32SectionHeaderSize,
                     section_bytes) ||
        !span_fits(bytes, static_cast<std::size_t>(section_offset), section_bytes)) {
        return malformed("ELF32 section header table is out of bounds");
    }
    if (section_name_index >= section_count) {
        return malformed("ELF32 section-name string table index is out of bounds");
    }

    std::vector<Section> sections{};
    sections.reserve(section_count);
    for (std::size_t index = 0; index < section_count; ++index) {
        const std::size_t base = static_cast<std::size_t>(section_offset) +
                                 index * kElf32SectionHeaderSize;
        Section section{};
        section.name = read_u32_le(bytes, base + 0u);
        section.type = read_u32_le(bytes, base + 4u);
        section.offset = read_u32_le(bytes, base + 16u);
        section.size = read_u32_le(bytes, base + 20u);
        section.link = read_u32_le(bytes, base + 24u);
        section.entsize = read_u32_le(bytes, base + 36u);
        sections.push_back(section);
    }

    const Section& section_names = sections[section_name_index];
    if (section_names.type != kShtStrtab ||
        !span_fits(bytes,
                   static_cast<std::size_t>(section_names.offset),
                   static_cast<std::size_t>(section_names.size))) {
        return malformed("ELF32 section-name string table is invalid");
    }

    for (const Section& section : sections) {
        std::string ignored{};
        if (!read_string(bytes,
                         static_cast<std::size_t>(section_names.offset),
                         static_cast<std::size_t>(section_names.size),
                         section.name,
                         ignored)) {
            return malformed("ELF32 section name is outside its string table");
        }
    }

    Elf32MetadataResult result{};
    result.status = Elf32MetadataStatus::Available;

    for (const Section& section : sections) {
        if (section.type != kShtSymtab && section.type != kShtDynsym) {
            continue;
        }
        if (section.entsize != kElf32SymbolSize ||
            (section.size % kElf32SymbolSize) != 0u) {
            return malformed("ELF32 symbol table has an invalid entry size");
        }
        if (section.link >= sections.size()) {
            return malformed("ELF32 symbol table string-table link is out of bounds");
        }

        const Section& strings = sections[section.link];
        if (strings.type != kShtStrtab) {
            return malformed("ELF32 symbol table link does not reference a string table");
        }
        if (!span_fits(bytes,
                       static_cast<std::size_t>(section.offset),
                       static_cast<std::size_t>(section.size)) ||
            !span_fits(bytes,
                       static_cast<std::size_t>(strings.offset),
                       static_cast<std::size_t>(strings.size))) {
            return malformed("ELF32 symbol or string table is out of bounds");
        }

        const std::size_t symbol_count = static_cast<std::size_t>(section.size) /
                                         kElf32SymbolSize;
        for (std::size_t index = 0; index < symbol_count; ++index) {
            const std::size_t base = static_cast<std::size_t>(section.offset) +
                                     index * kElf32SymbolSize;
            const std::uint32_t name_offset = read_u32_le(bytes, base + 0u);

            Elf32Symbol symbol{};
            if (!read_string(bytes,
                             static_cast<std::size_t>(strings.offset),
                             static_cast<std::size_t>(strings.size),
                             name_offset,
                             symbol.name)) {
                return malformed("ELF32 symbol name is outside its string table");
            }
            symbol.value = read_u32_le(bytes, base + 4u);
            symbol.size = read_u32_le(bytes, base + 8u);
            const std::uint8_t info = bytes[base + 12u];
            symbol.binding = static_cast<std::uint8_t>(info >> 4u);
            symbol.type = static_cast<std::uint8_t>(info & 0x0Fu);
            symbol.section_index = read_u16_le(bytes, base + 14u);
            result.symbols.push_back(std::move(symbol));
        }
    }

    return result;
}

} // namespace b3r::analysis
