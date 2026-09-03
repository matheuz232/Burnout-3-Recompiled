#include "analysis/r5900_reachability.h"
#include "recompiler/ps2_elf.h"
#include "runtime/ps2_memory_map.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_follow_direct_calls_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset + 0u] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0u] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

constexpr std::uint32_t j_type(std::uint8_t op, std::uint32_t target) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           ((target >> 2u) & 0x03FFFFFFu);
}

b3r::runtime::Ps2MemoryMap make_memory() {
    constexpr std::uint32_t kBase = 0x1000u;
    constexpr std::uint32_t kProgramHeaderOffset = 52u;
    constexpr std::uint32_t kSegmentOffset = 0x100u;
    constexpr std::size_t kWordCount = 66u;

    std::vector<std::uint32_t> words(kWordCount, 0u);
    words[0] = j_type(0x03u, 0x1100u); // JAL 0x1100
    words[1] = 0u;                    // delay slot
    words[2] = 0x0000000Du;           // BREAK continuation
    words[64] = 0x0000000Du;          // BREAK callee

    const auto payload_size = static_cast<std::uint32_t>(words.size() * 4u);
    Bytes bytes(kSegmentOffset + payload_size, 0u);
    bytes[0] = 0x7Fu;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 1u;
    bytes[5] = 1u;
    bytes[6] = 1u;

    put_u16(bytes, 16u, 2u);
    put_u16(bytes, 18u, 8u);
    put_u32(bytes, 20u, 1u);
    put_u32(bytes, 24u, kBase);
    put_u32(bytes, 28u, kProgramHeaderOffset);
    put_u16(bytes, 40u, 52u);
    put_u16(bytes, 42u, 32u);
    put_u16(bytes, 44u, 1u);

    put_u32(bytes, kProgramHeaderOffset + 0u, 1u);
    put_u32(bytes, kProgramHeaderOffset + 4u, kSegmentOffset);
    put_u32(bytes, kProgramHeaderOffset + 8u, kBase);
    put_u32(bytes, kProgramHeaderOffset + 12u, kBase);
    put_u32(bytes, kProgramHeaderOffset + 16u, payload_size);
    put_u32(bytes, kProgramHeaderOffset + 20u, payload_size);
    put_u32(bytes, kProgramHeaderOffset + 24u, 5u);
    put_u32(bytes, kProgramHeaderOffset + 28u, 0x1000u);

    for (std::size_t i = 0u; i < words.size(); ++i) {
        put_u32(bytes, kSegmentOffset + i * 4u, words[i]);
    }

    const auto elf = b3r::recompiler::parse_ps2_elf(bytes);
    expect(elf.ok(), "synthetic ELF must parse");
    auto map = b3r::runtime::Ps2MemoryMap::from_elf(*elf.image);
    expect(map.ok(), "synthetic ELF must map");
    return std::move(*map.memory);
}

bool has_block(const b3r::analysis::R5900ReachabilityGraph& graph, std::uint32_t pc) {
    return std::any_of(graph.blocks.begin(), graph.blocks.end(),
                       [pc](const auto& block) { return block.start_pc == pc; });
}

} // namespace

int main() {
    using namespace b3r::analysis;

    const auto memory = make_memory();

    {
        const auto result = analyze_r5900_reachability(memory, 0x1000u);
        expect(result.ok(), "default reachability must succeed");
        expect(!has_block(*result.graph, 0x1100u),
               "default reachability must keep direct-call targets as evidence only");
    }

    {
        R5900ReachabilityOptions options{};
        options.follow_direct_calls = true;
        const auto result = analyze_r5900_reachability(memory, 0x1000u, options);
        expect(result.ok(), "follow-direct-calls reachability must succeed");
        expect(has_block(*result.graph, 0x1100u),
               "enabled follow-direct-calls must traverse the explicit callee target");
        expect(result.graph->calls.size() == 1u && result.graph->calls[0].target == 0x1100u,
               "traversed direct call must remain recorded as call evidence");
    }

    std::cout << "r5900_follow_direct_calls_tests: PASS\n";
    return EXIT_SUCCESS;
}
