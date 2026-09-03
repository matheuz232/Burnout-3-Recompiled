#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace b3r::recompiler {

enum class Ps2ElfError {
    None = 0,
    TooSmall,
    BadMagic,
    UnsupportedClass,
    UnsupportedEndian,
    UnsupportedIdentVersion,
    UnsupportedType,
    UnsupportedMachine,
    UnsupportedVersion,
    InvalidHeaderSize,
    InvalidProgramHeaderSize,
    ProgramHeaderTableOutOfBounds,
    SegmentOutOfBounds,
    SegmentMemoryTooSmall,
};

struct Ps2ElfParseResult;

struct Ps2ElfLoadSegment {
    std::uint32_t file_offset{};
    std::uint32_t virtual_address{};
    std::uint32_t physical_address{};
    std::uint32_t file_size{};
    std::uint32_t memory_size{};
    std::uint32_t flags{};
    std::uint32_t alignment{};
};

class Ps2ElfImage {
public:
    [[nodiscard]] std::uint32_t entry_point() const noexcept;
    [[nodiscard]] std::uint16_t program_header_count() const noexcept;
    [[nodiscard]] const std::vector<Ps2ElfLoadSegment>& load_segments() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> segment_file_bytes(std::size_t index) const noexcept;

private:
    friend struct Ps2ElfParseResult;
    friend Ps2ElfParseResult parse_ps2_elf(std::span<const std::uint8_t> bytes);

    std::uint32_t entry_point_{};
    std::uint16_t program_header_count_{};
    std::vector<Ps2ElfLoadSegment> load_segments_{};
    std::vector<std::uint8_t> bytes_{};
};

struct Ps2ElfParseResult {
    Ps2ElfError error{Ps2ElfError::None};
    std::string message{};
    std::optional<Ps2ElfImage> image{};

    [[nodiscard]] bool ok() const noexcept {
        return error == Ps2ElfError::None && image.has_value();
    }
};

[[nodiscard]] Ps2ElfParseResult parse_ps2_elf(std::span<const std::uint8_t> bytes);

} // namespace b3r::recompiler
