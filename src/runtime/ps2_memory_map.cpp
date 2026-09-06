#include "runtime/ps2_memory_map.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace b3r::runtime {
namespace {

constexpr std::uint64_t kGuestAddressSpaceSize = (std::uint64_t{1} << 32u);
constexpr std::uint32_t kEeMainRamSize = 0x02000000u;

Ps2MemoryMapBuildResult fail(Ps2MemoryMapBuildError error, const char* message) {
    Ps2MemoryMapBuildResult result{};
    result.error = error;
    result.message = message;
    return result;
}

std::uint64_t region_end(const Ps2MemoryRegion& region) noexcept {
    return static_cast<std::uint64_t>(region.guest_base) + static_cast<std::uint64_t>(region.size);
}


} // namespace

Ps2MemoryMapBuildResult Ps2MemoryMap::from_elf(const recompiler::Ps2ElfImage& elf) {
    struct PendingRegion {
        Ps2MemoryRegion metadata{};
        std::span<const std::uint8_t> file_bytes{};
    };

    std::vector<PendingRegion> pending;
    pending.reserve(elf.load_segments().size());

    for (std::size_t i = 0; i < elf.load_segments().size(); ++i) {
        const auto& segment = elf.load_segments()[i];
        if (segment.memory_size == 0u) {
            continue;
        }

        const std::uint64_t end = static_cast<std::uint64_t>(segment.virtual_address) +
                                  static_cast<std::uint64_t>(segment.memory_size);
        if (end > kGuestAddressSpaceSize) {
            return fail(Ps2MemoryMapBuildError::AddressOverflow,
                        "ELF PT_LOAD guest range exceeds the 32-bit PS2 address space");
        }

        if (end > static_cast<std::uint64_t>(kEeMainRamSize)) {
            return fail(Ps2MemoryMapBuildError::OutsideSupportedMainRam,
                        "ELF PT_LOAD guest range exceeds supported 32 MiB EE main RAM");
        }

        const auto file_bytes = elf.segment_file_bytes(i);
        if (file_bytes.size() != static_cast<std::size_t>(segment.file_size)) {
            return fail(Ps2MemoryMapBuildError::SegmentPayloadMismatch,
                        "ELF PT_LOAD payload does not match p_filesz");
        }

        pending.push_back(PendingRegion{
            Ps2MemoryRegion{segment.virtual_address, segment.memory_size, segment.flags},
            file_bytes,
        });
    }

    std::sort(pending.begin(), pending.end(), [](const PendingRegion& lhs, const PendingRegion& rhs) {
        return lhs.metadata.guest_base < rhs.metadata.guest_base;
    });

    for (std::size_t i = 1; i < pending.size(); ++i) {
        if (static_cast<std::uint64_t>(pending[i].metadata.guest_base) < region_end(pending[i - 1].metadata)) {
            return fail(Ps2MemoryMapBuildError::OverlappingRegions,
                        "ELF PT_LOAD guest ranges overlap");
        }
    }

    Ps2MemoryMap memory;
    try {
        memory.main_ram_.assign(static_cast<std::size_t>(kEeMainRamSize), 0u);
        memory.regions_.reserve(pending.size());
        for (const auto& item : pending) {
            memory.regions_.push_back(item.metadata);
            std::copy(item.file_bytes.begin(), item.file_bytes.end(),
                      memory.main_ram_.begin() +
                          static_cast<std::ptrdiff_t>(item.metadata.guest_base));
        }
    } catch (const std::bad_alloc&) {
        return fail(Ps2MemoryMapBuildError::AllocationFailed,
                    "Unable to allocate 32 MiB EE main RAM backing storage");
    } catch (const std::length_error&) {
        return fail(Ps2MemoryMapBuildError::AllocationFailed,
                    "EE main RAM backing size exceeds native container limits");
    }

    Ps2MemoryMapBuildResult result{};
    result.memory = std::move(memory);
    return result;
}

const std::vector<Ps2MemoryRegion>& Ps2MemoryMap::regions() const noexcept {
    return regions_;
}

std::optional<std::span<std::uint8_t>>
Ps2MemoryMap::translate(std::uint32_t address, std::size_t length) noexcept {
    // Check the owned backing too: a default-constructed map has no RAM.
    if (length > main_ram_.size() ||
        static_cast<std::uint64_t>(address) + length > main_ram_.size()) {
        return std::nullopt;
    }
    return std::span<std::uint8_t>(main_ram_).subspan(address, length);
}

std::optional<std::span<const std::uint8_t>>
Ps2MemoryMap::translate(std::uint32_t address, std::size_t length) const noexcept {
    if (length > main_ram_.size() ||
        static_cast<std::uint64_t>(address) + length > main_ram_.size()) {
        return std::nullopt;
    }
    return std::span<const std::uint8_t>(main_ram_).subspan(address, length);
}

std::optional<std::uint8_t> Ps2MemoryMap::read_u8(std::uint32_t address) const noexcept {
    const auto bytes = translate(address, 1);
    if (!bytes) {
        return std::nullopt;
    }
    return (*bytes)[0];
}

std::optional<std::uint16_t> Ps2MemoryMap::read_u16(std::uint32_t address) const noexcept {
    const auto bytes = translate(address, 2);
    if (!bytes) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>((*bytes)[0]) |
        (static_cast<std::uint16_t>((*bytes)[1]) << 8u));
}

std::optional<std::uint32_t> Ps2MemoryMap::read_u32(std::uint32_t address) const noexcept {
    const auto bytes = translate(address, 4);
    if (!bytes) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>((*bytes)[0]) |
           (static_cast<std::uint32_t>((*bytes)[1]) << 8u) |
           (static_cast<std::uint32_t>((*bytes)[2]) << 16u) |
           (static_cast<std::uint32_t>((*bytes)[3]) << 24u);
}

std::optional<std::uint64_t> Ps2MemoryMap::read_u64(std::uint32_t address) const noexcept {
    const auto bytes = translate(address, 8u);
    if (!bytes) {
        return std::nullopt;
    }

    std::uint64_t value{};
    for (unsigned i = 0u; i < 8u; ++i) {
        value |= static_cast<std::uint64_t>((*bytes)[i]) << (i * 8u);
    }
    return value;
}

std::optional<Ps2MemoryValue128> Ps2MemoryMap::read_u128(std::uint32_t address) const noexcept {
    const auto bytes = translate(address, 16u);
    if (!bytes) {
        return std::nullopt;
    }

    Ps2MemoryValue128 value{};
    for (unsigned i = 0u; i < 8u; ++i) {
        value[0] |= static_cast<std::uint64_t>((*bytes)[i]) << (i * 8u);
        value[1] |= static_cast<std::uint64_t>((*bytes)[8u + i]) << (i * 8u);
    }
    return value;
}

bool Ps2MemoryMap::write_u8(std::uint32_t address, std::uint8_t value) noexcept {
    const auto bytes = translate(address, 1);
    if (!bytes) {
        return false;
    }
    (*bytes)[0] = value;
    return true;
}

bool Ps2MemoryMap::write_u16(std::uint32_t address, std::uint16_t value) noexcept {
    const auto bytes = translate(address, 2);
    if (!bytes) {
        return false;
    }
    (*bytes)[0] = static_cast<std::uint8_t>(value & 0xFFu);
    (*bytes)[1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    return true;
}

bool Ps2MemoryMap::write_u32(std::uint32_t address, std::uint32_t value) noexcept {
    const auto bytes = translate(address, 4);
    if (!bytes) {
        return false;
    }
    (*bytes)[0] = static_cast<std::uint8_t>(value & 0xFFu);
    (*bytes)[1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    (*bytes)[2] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    (*bytes)[3] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
    return true;
}

bool Ps2MemoryMap::write_u64(std::uint32_t address, std::uint64_t value) noexcept {
    const auto bytes = translate(address, 8u);
    if (!bytes) {
        return false;
    }

    for (unsigned i = 0u; i < 8u; ++i) {
        (*bytes)[i] = static_cast<std::uint8_t>((value >> (i * 8u)) & 0xffu);
    }
    return true;
}

bool Ps2MemoryMap::write_u128(std::uint32_t address,
                              const Ps2MemoryValue128& value) noexcept {
    const auto bytes = translate(address, 16u);
    if (!bytes) {
        return false;
    }

    for (unsigned i = 0u; i < 8u; ++i) {
        (*bytes)[i] = static_cast<std::uint8_t>((value[0] >> (i * 8u)) & 0xffu);
        (*bytes)[8u + i] = static_cast<std::uint8_t>((value[1] >> (i * 8u)) & 0xffu);
    }
    return true;
}

} // namespace b3r::runtime
