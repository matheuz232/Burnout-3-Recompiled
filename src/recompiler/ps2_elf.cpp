#include "recompiler/ps2_elf.h"

#include <limits>

namespace b3r::recompiler {
namespace {

constexpr std::size_t kElf32HeaderSize = 52;
constexpr std::size_t kElf32ProgramHeaderSize = 32;
constexpr std::uint32_t kPtLoad = 1;

std::uint16_t read_u16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8u);
}

std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

Ps2ElfParseResult fail(Ps2ElfError error, const char* message) {
    Ps2ElfParseResult result{};
    result.error = error;
    result.message = message;
    return result;
}

bool range_fits(std::size_t offset, std::size_t length, std::size_t total) noexcept {
    return offset <= total && length <= (total - offset);
}

} // namespace

std::uint32_t Ps2ElfImage::entry_point() const noexcept {
    return entry_point_;
}

std::uint16_t Ps2ElfImage::program_header_count() const noexcept {
    return program_header_count_;
}

const std::vector<Ps2ElfLoadSegment>& Ps2ElfImage::load_segments() const noexcept {
    return load_segments_;
}

std::span<const std::uint8_t> Ps2ElfImage::segment_file_bytes(std::size_t index) const noexcept {
    if (index >= load_segments_.size()) {
        return {};
    }

    const auto& segment = load_segments_[index];
    return std::span<const std::uint8_t>(bytes_)
        .subspan(static_cast<std::size_t>(segment.file_offset), static_cast<std::size_t>(segment.file_size));
}

Ps2ElfParseResult parse_ps2_elf(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kElf32HeaderSize) {
        return fail(Ps2ElfError::TooSmall, "ELF file is smaller than an ELF32 header");
    }

    if (bytes[0] != 0x7Fu || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F') {
        return fail(Ps2ElfError::BadMagic, "ELF magic is invalid");
    }
    if (bytes[4] != 1u) {
        return fail(Ps2ElfError::UnsupportedClass, "Only ELFCLASS32 is supported");
    }
    if (bytes[5] != 1u) {
        return fail(Ps2ElfError::UnsupportedEndian, "Only little-endian ELF is supported");
    }
    if (bytes[6] != 1u) {
        return fail(Ps2ElfError::UnsupportedIdentVersion, "ELF ident version is unsupported");
    }

    const auto type = read_u16(bytes, 16);
    const auto machine = read_u16(bytes, 18);
    const auto version = read_u32(bytes, 20);
    const auto entry_point = read_u32(bytes, 24);
    const auto program_header_offset = read_u32(bytes, 28);
    const auto header_size = read_u16(bytes, 40);
    const auto program_header_size = read_u16(bytes, 42);
    const auto program_header_count = read_u16(bytes, 44);

    if (type != 2u) {
        return fail(Ps2ElfError::UnsupportedType, "Only ET_EXEC ELF files are supported");
    }
    if (machine != 8u) {
        return fail(Ps2ElfError::UnsupportedMachine, "Only EM_MIPS ELF files are supported");
    }
    if (version != 1u) {
        return fail(Ps2ElfError::UnsupportedVersion, "ELF version is unsupported");
    }
    if (header_size != kElf32HeaderSize) {
        return fail(Ps2ElfError::InvalidHeaderSize, "Unexpected ELF32 header size");
    }
    if (program_header_size != kElf32ProgramHeaderSize) {
        return fail(Ps2ElfError::InvalidProgramHeaderSize, "Unexpected ELF32 program-header size");
    }

    const auto phoff = static_cast<std::size_t>(program_header_offset);
    const auto phnum = static_cast<std::size_t>(program_header_count);
    if (phnum > std::numeric_limits<std::size_t>::max() / kElf32ProgramHeaderSize) {
        return fail(Ps2ElfError::ProgramHeaderTableOutOfBounds, "Program-header table size overflows");
    }
    const auto table_size = phnum * kElf32ProgramHeaderSize;
    if (!range_fits(phoff, table_size, bytes.size())) {
        return fail(Ps2ElfError::ProgramHeaderTableOutOfBounds, "Program-header table is outside the ELF file");
    }

    Ps2ElfImage image{};
    image.entry_point_ = entry_point;
    image.program_header_count_ = program_header_count;
    image.load_segments_.reserve(program_header_count);

    for (std::size_t i = 0; i < phnum; ++i) {
        const std::size_t base = phoff + (i * kElf32ProgramHeaderSize);
        const auto program_type = read_u32(bytes, base + 0);
        if (program_type != kPtLoad) {
            continue;
        }

        Ps2ElfLoadSegment segment{};
        segment.file_offset = read_u32(bytes, base + 4);
        segment.virtual_address = read_u32(bytes, base + 8);
        segment.physical_address = read_u32(bytes, base + 12);
        segment.file_size = read_u32(bytes, base + 16);
        segment.memory_size = read_u32(bytes, base + 20);
        segment.flags = read_u32(bytes, base + 24);
        segment.alignment = read_u32(bytes, base + 28);

        if (segment.memory_size < segment.file_size) {
            return fail(Ps2ElfError::SegmentMemoryTooSmall, "PT_LOAD p_memsz is smaller than p_filesz");
        }

        if (!range_fits(static_cast<std::size_t>(segment.file_offset),
                        static_cast<std::size_t>(segment.file_size),
                        bytes.size())) {
            return fail(Ps2ElfError::SegmentOutOfBounds, "PT_LOAD file range is outside the ELF file");
        }

        image.load_segments_.push_back(segment);
    }

    image.bytes_.assign(bytes.begin(), bytes.end());

    Ps2ElfParseResult result{};
    result.image = std::move(image);
    return result;
}

} // namespace b3r::recompiler
