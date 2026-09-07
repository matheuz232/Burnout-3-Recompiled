#include "recompiler/ps2_elf.h"
#include "runtime/ps2_memory_map.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <cstdlib>
#include <iostream>
#include <utility>
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

struct SegmentFixture {
    std::uint32_t file_offset{};
    std::uint32_t virtual_address{};
    std::uint32_t file_size{};
    std::uint32_t memory_size{};
    std::uint32_t flags{5};
    std::vector<std::uint8_t> payload{};
};

Bytes make_elf(const std::vector<SegmentFixture>& segments) {
    constexpr std::uint32_t kProgramHeaderOffset = 52;
    Bytes bytes(0x300, 0);
    bytes[0] = 0x7F;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 1;
    bytes[5] = 1;
    bytes[6] = 1;
    put_u16(bytes, 16, 2); // ET_EXEC
    put_u16(bytes, 18, 8); // EM_MIPS
    put_u32(bytes, 20, 1);
    put_u32(bytes, 24, segments.empty() ? 0u : segments.front().virtual_address);
    put_u32(bytes, 28, kProgramHeaderOffset);
    put_u16(bytes, 40, 52);
    put_u16(bytes, 42, 32);
    put_u16(bytes, 44, static_cast<std::uint16_t>(segments.size()));

    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& segment = segments[i];
        const std::size_t ph = kProgramHeaderOffset + (i * 32);
        put_u32(bytes, ph + 0, 1); // PT_LOAD
        put_u32(bytes, ph + 4, segment.file_offset);
        put_u32(bytes, ph + 8, segment.virtual_address);
        put_u32(bytes, ph + 12, segment.virtual_address);
        put_u32(bytes, ph + 16, segment.file_size);
        put_u32(bytes, ph + 20, segment.memory_size);
        put_u32(bytes, ph + 24, segment.flags);
        put_u32(bytes, ph + 28, 0x1000);

        for (std::size_t j = 0; j < segment.payload.size(); ++j) {
            bytes[static_cast<std::size_t>(segment.file_offset) + j] = segment.payload[j];
        }
    }
    return bytes;
}

[[noreturn]] void fail(const char* message) {
    std::cerr << "ps2_memory_map_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

b3r::recompiler::Ps2ElfImage parse_or_fail(Bytes bytes) {
    auto parsed = b3r::recompiler::parse_ps2_elf(bytes);
    expect(parsed.ok(), "synthetic ELF fixture must parse");
    return std::move(*parsed.image);
}

} // namespace

int main() {
    using namespace b3r::runtime;

    {
        const auto elf = parse_or_fail(make_elf({
            {0x100, 0x00100000, 4, 8, 5, {0x11, 0x22, 0x33, 0x44}},
        }));
        auto built = Ps2MemoryMap::from_elf(elf);
        expect(built.ok(), "valid ELF load segment must map");
        expect(built.memory.has_value(), "successful build must return a memory map");
        expect(built.memory->regions().size() == 1u, "one PT_LOAD must produce one region");

        const auto& region = built.memory->regions().front();
        expect(region.guest_base == 0x00100000u, "region base must use p_vaddr");
        expect(region.size == 8u, "region size must use p_memsz");
        expect(region.flags == 5u, "region flags must be retained");

        const auto view = built.memory->translate(0x00100000u, 8u);
        expect(view.has_value(), "full mapped region must translate");
        expect((*view)[0] == 0x11u && (*view)[3] == 0x44u, "ELF file bytes must be copied");
        expect((*view)[4] == 0u && (*view)[7] == 0u, "BSS tail must be zero-filled");

        const auto before_segment = built.memory->translate(0x000fffffu, 1u);
        expect(before_segment.has_value() && (*before_segment)[0] == 0u,
               "EE RAM outside PT_LOAD must translate and begin zero-filled");
        const auto across_segment = built.memory->translate(0x00100007u, 2u);
        expect(across_segment.has_value() && (*across_segment)[0] == 0u &&
                   (*across_segment)[1] == 0u,
               "physical RAM must cross PT_LOAD metadata edges");
        const auto& memory = *built.memory;
        const auto ram = memory.translate(0u, 0x02000000u);
        expect(ram.has_value(), "all 32 MiB of EE main RAM must be backed");
        const auto zero = [](std::uint8_t byte) { return byte == 0u; };
        expect(std::all_of(ram->begin(), ram->begin() + 0x00100000u, zero) &&
                   std::all_of(ram->begin() + 0x00100004u, ram->end(), zero),
               "all RAM apart from ELF file bytes must begin zero-filled");
        expect(memory.read_u64(0x01fffff0u).value_or(1u) == 0u,
               "uninitialized stack RAM must be zero");
        expect(built.memory->write_u64(0x01fffff0u, 0x00000000001001f0ull),
               "stack write_u64 outside PT_LOAD must succeed");
        expect(memory.read_u64(0x01fffff0u).value_or(0u) == 0x00000000001001f0ull,
               "stack read_u64 must round-trip the return address");
        expect(!memory.translate(0x01ffffffu, 2u).has_value() &&
                   !built.memory->translate(0x02000000u, 1u).has_value(),
               "access crossing EE main RAM end must reject");
        expect(!memory.translate(1u, std::numeric_limits<std::size_t>::max()).has_value(),
               "oversized translation must reject without arithmetic wrap");
        expect(!built.memory->translate(0xFFFFFFFFu, 2u).has_value(), "translation arithmetic overflow must reject");
    }

    {
        const auto elf = parse_or_fail(make_elf({
            {0x100, 0x00200000, 4, 0x20, 5, {1, 2, 3, 4}},
            {0x120, 0x00200010, 4, 0x20, 6, {5, 6, 7, 8}},
        }));
        const auto built = Ps2MemoryMap::from_elf(elf);
        expect(!built.ok(), "overlapping guest regions must reject");
        expect(built.error == Ps2MemoryMapBuildError::OverlappingRegions,
               "overlap must return explicit error");
    }

    for (const auto address : {0x03000000u, 0x01fffff0u}) {
        const auto elf = parse_or_fail(make_elf({
            {0x100, address, 4u, 0x20u, 5u, {1, 2, 3, 4}},
        }));
        const auto built = Ps2MemoryMap::from_elf(elf);
        expect(!built.ok() && !built.memory.has_value(),
               "PT_LOAD outside or crossing EE main RAM must reject atomically");
        expect(built.error == Ps2MemoryMapBuildError::OutsideSupportedMainRam,
               "out-of-RAM PT_LOAD must use explicit build error");
    }

    {
        Ps2MemoryMap empty;
        expect(!empty.translate(0u, 1u).has_value() && !empty.read_u64(0u).has_value(),
               "unbuilt memory map must not expose unavailable backing");
    }

    {
        const auto elf = parse_or_fail(make_elf({
            {0x100, 0xFFFFFFF0u, 4, 0x20, 5, {1, 2, 3, 4}},
        }));
        const auto built = Ps2MemoryMap::from_elf(elf);
        expect(!built.ok(), "guest range beyond 32-bit address space must reject");
        expect(built.error == Ps2MemoryMapBuildError::AddressOverflow,
               "overflow must return explicit error");
    }

    {
        const auto elf = parse_or_fail(make_elf({
            {0x180, 0x00300000, 8, 32, 6, {0, 0, 0, 0, 0, 0, 0, 0}},
        }));
        auto built = Ps2MemoryMap::from_elf(elf);
        expect(built.ok(), "read/write fixture must map");
        auto& memory = *built.memory;

        expect(memory.write_u8(0x00300000, 0xABu), "write_u8 must succeed in range");
        expect(memory.write_u16(0x00300001, 0xCDEFu), "write_u16 must support byte-addressed unaligned access");
        expect(memory.write_u32(0x00300004, 0x12345678u), "write_u32 must succeed in range");

        expect(memory.read_u8(0x00300000).value_or(0) == 0xABu, "read_u8 must return written value");
        expect(memory.read_u16(0x00300001).value_or(0) == 0xCDEFu, "read_u16 must decode little-endian value");
        expect(memory.read_u32(0x00300004).value_or(0) == 0x12345678u, "read_u32 must decode little-endian value");

        const auto bytes = memory.translate(0x00300000, 8);
        expect(bytes.has_value(), "written bytes must remain translatable");
        expect((*bytes)[1] == 0xEFu && (*bytes)[2] == 0xCDu, "u16 must be stored little-endian");
        expect((*bytes)[4] == 0x78u && (*bytes)[7] == 0x12u, "u32 must be stored little-endian");

        const Ps2MemoryValue128 original{
            0x0123456789abcdefull,
            0xfedcba9876543210ull,
        };
        expect(memory.write_u64(0x00300008u, 0x8877665544332211ull),
               "write_u64 must succeed");
        expect(memory.read_u64(0x00300008u).value_or(0) == 0x8877665544332211ull,
               "read_u64 must round-trip little-endian data");
        expect(memory.write_u128(0x00300010u, original),
               "write_u128 must succeed for a complete mapped span");
        const auto loaded = memory.read_u128(0x00300010u);
        expect(loaded.has_value(), "read_u128 must succeed");
        expect((*loaded)[0] == original[0] && (*loaded)[1] == original[1],
               "read_u128 must preserve low/high ordering");

        const auto wide_bytes = memory.translate(0x00300008u, 24u);
        expect(wide_bytes.has_value(), "wide typed writes must remain translatable");
        expect((*wide_bytes)[0] == 0x11u && (*wide_bytes)[7] == 0x88u,
               "u64 must be stored little-endian");
        expect((*wide_bytes)[8] == 0xefu && (*wide_bytes)[15] == 0x01u,
               "u128 low64 must be stored first in little-endian order");
        expect((*wide_bytes)[16] == 0x10u && (*wide_bytes)[23] == 0xfeu,
               "u128 high64 must follow low64 in little-endian order");

        expect(!memory.read_u32(0x01fffffeu).has_value(), "cross-boundary scalar read must reject");
        expect(!memory.write_u32(0x01fffffeu, 1u), "cross-boundary scalar write must reject");
        expect(!memory.read_u64(0x01fffffcu).has_value(), "cross-boundary u64 read must reject");
        expect(!memory.write_u64(0x01fffffcu, 1u), "cross-boundary u64 write must reject");
    }

    {
        const auto elf = parse_or_fail(make_elf({
            {0x1C0, 0x00400000u, 8u, 24u, 6u, {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80}},
        }));
        auto built = Ps2MemoryMap::from_elf(elf);
        expect(built.ok(), "atomicity fixture must map");
        auto& memory = *built.memory;

        auto tail = memory.translate(0x01fffff8u, 8u);
        expect(tail.has_value(), "atomicity fixture tail must translate");
        for (std::size_t index = 0; index < tail->size(); ++index) {
            (*tail)[index] = static_cast<std::uint8_t>(0xa0u + index);
        }
        const std::vector<std::uint8_t> before(tail->begin(), tail->end());

        const Ps2MemoryValue128 replacement{
            0x1112131415161718ull,
            0x2122232425262728ull,
        };
        expect(!memory.write_u128(0x01fffff8u, replacement),
               "write_u128 must reject a span crossing physical RAM end");

        const auto after = memory.translate(0x01fffff8u, 8u);
        expect(after.has_value(), "atomicity fixture tail must remain mapped");
        expect(std::vector<std::uint8_t>(after->begin(), after->end()) == before,
               "failed write_u128 must leave every mapped byte unchanged");
    }

    std::cout << "ps2_memory_map_tests: PASS\n";
    return EXIT_SUCCESS;
}
