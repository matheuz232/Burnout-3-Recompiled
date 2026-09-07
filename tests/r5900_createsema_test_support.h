#pragma once

#include "recompiler/ps2_elf.h"
#include "runtime/ps2_memory_map.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

namespace b3r::test_support::createsema {

inline void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "CreateSema: FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

constexpr std::uint32_t i_type(std::uint32_t op, std::uint32_t rs,
                               std::uint32_t rt, std::uint16_t imm) {
    return (op << 26u) | (rs << 21u) | (rt << 16u) | imm;
}

constexpr std::uint32_t r_type(std::uint32_t rs, std::uint32_t rt,
                               std::uint32_t rd, std::uint32_t fn) {
    return (rs << 21u) | (rt << 16u) | (rd << 11u) | fn;
}

constexpr std::uint32_t j_type(std::uint32_t op, std::uint32_t target) {
    return (op << 26u) | ((target >> 2u) & 0x03ffffffu);
}

struct Segment {
    std::uint32_t pc;
    std::vector<std::uint32_t> words;
};

inline runtime::Ps2MemoryMap make_memory(
    const std::vector<Segment>& segments = {{0x00100000u, {0u}}}) {
    std::vector<std::uint8_t> bytes(52u + 32u * segments.size(), 0u);
    const auto put = [&](std::size_t offset, std::uint32_t value, std::size_t width = 4u) {
        for (std::size_t i = 0; i < width; ++i) {
            bytes[offset + i] = static_cast<std::uint8_t>(value >> (8u * i));
        }
    };
    put(0u, 0x464c457fu);
    put(4u, 0x00010101u);
    put(16u, 2u, 2u);
    put(18u, 8u, 2u);
    put(20u, 1u);
    put(24u, segments.front().pc);
    put(28u, 52u);
    put(40u, 52u, 2u);
    put(42u, 32u, 2u);
    put(44u, static_cast<std::uint32_t>(segments.size()), 2u);
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& segment = segments[i];
        const auto ph = 52u + 32u * i;
        const auto offset = bytes.size();
        const auto size = segment.words.size() * 4u;
        bytes.resize(offset + size);
        put(ph, 1u);
        put(ph + 4u, static_cast<std::uint32_t>(offset));
        put(ph + 8u, segment.pc);
        put(ph + 12u, segment.pc);
        put(ph + 16u, static_cast<std::uint32_t>(size));
        put(ph + 20u, static_cast<std::uint32_t>(size));
        put(ph + 24u, 5u);
        put(ph + 28u, 4u);
        for (std::size_t j = 0; j < segment.words.size(); ++j) {
            put(offset + j * 4u, segment.words[j]);
        }
    }
    const auto elf = recompiler::parse_ps2_elf(bytes);
    expect(elf.ok(), "synthetic ELF must parse");
    auto built = runtime::Ps2MemoryMap::from_elf(*elf.image);
    expect(built.ok(), "synthetic RAM must build");
    return std::move(*built.memory);
}

inline void descriptor(runtime::Ps2MemoryMap& memory, std::uint32_t address,
                       std::uint32_t initial = 1u, std::uint32_t maximum = 1u,
                       std::uint32_t attr = 0u, std::uint32_t option = 0x12345678u) {
    // Status fields deliberately disagree with the create parameters.
    expect(memory.write_u32(address, 0xdeadbeefu) &&
               memory.write_u32(address + 4u, maximum) &&
               memory.write_u32(address + 8u, initial) &&
               memory.write_u32(address + 12u, 0xffffffffu) &&
               memory.write_u32(address + 16u, attr) &&
               memory.write_u32(address + 20u, option),
           "descriptor must fit RAM");
}

} // namespace b3r::test_support::createsema
