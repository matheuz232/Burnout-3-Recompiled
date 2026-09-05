#pragma once

#include "recompiler/ps2_elf.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace b3r::runtime {

using Ps2MemoryValue128 = std::array<std::uint64_t, 2>;

enum class Ps2MemoryMapBuildError {
    None = 0,
    AddressOverflow,
    OverlappingRegions,
    SegmentPayloadMismatch,
    AllocationFailed,
};

struct Ps2MemoryRegion {
    std::uint32_t guest_base{};
    std::uint32_t size{};
    std::uint32_t flags{};
};

struct Ps2MemoryMapBuildResult;

class Ps2MemoryMap {
public:
    [[nodiscard]] static Ps2MemoryMapBuildResult from_elf(const recompiler::Ps2ElfImage& elf);

    [[nodiscard]] const std::vector<Ps2MemoryRegion>& regions() const noexcept;

    [[nodiscard]] std::optional<std::span<std::uint8_t>>
    translate(std::uint32_t address, std::size_t length) noexcept;

    [[nodiscard]] std::optional<std::span<const std::uint8_t>>
    translate(std::uint32_t address, std::size_t length) const noexcept;

    [[nodiscard]] std::optional<std::uint8_t> read_u8(std::uint32_t address) const noexcept;
    [[nodiscard]] std::optional<std::uint16_t> read_u16(std::uint32_t address) const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> read_u32(std::uint32_t address) const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> read_u64(std::uint32_t address) const noexcept;
    [[nodiscard]] std::optional<Ps2MemoryValue128> read_u128(std::uint32_t address) const noexcept;

    [[nodiscard]] bool write_u8(std::uint32_t address, std::uint8_t value) noexcept;
    [[nodiscard]] bool write_u16(std::uint32_t address, std::uint16_t value) noexcept;
    [[nodiscard]] bool write_u32(std::uint32_t address, std::uint32_t value) noexcept;
    [[nodiscard]] bool write_u64(std::uint32_t address, std::uint64_t value) noexcept;
    [[nodiscard]] bool write_u128(std::uint32_t address,
                                  const Ps2MemoryValue128& value) noexcept;

private:
    struct BackingRegion {
        Ps2MemoryRegion metadata{};
        std::vector<std::uint8_t> bytes{};
    };

    std::vector<Ps2MemoryRegion> regions_{};
    std::vector<BackingRegion> backing_regions_{};
};

struct Ps2MemoryMapBuildResult {
    Ps2MemoryMapBuildError error{Ps2MemoryMapBuildError::None};
    std::string message{};
    std::optional<Ps2MemoryMap> memory{};

    [[nodiscard]] bool ok() const noexcept {
        return error == Ps2MemoryMapBuildError::None && memory.has_value();
    }
};

} // namespace b3r::runtime
