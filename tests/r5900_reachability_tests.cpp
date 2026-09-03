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
    std::cerr << "r5900_reachability_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}
void expect(bool condition, const char* message) { if (!condition) fail(message); }

void put_u16(Bytes& b, std::size_t o, std::uint16_t v) {
    b[o] = static_cast<std::uint8_t>(v & 0xFFu);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8u) & 0xFFu);
}
void put_u32(Bytes& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<std::uint8_t>(v & 0xFFu);
    b[o + 1] = static_cast<std::uint8_t>((v >> 8u) & 0xFFu);
    b[o + 2] = static_cast<std::uint8_t>((v >> 16u) & 0xFFu);
    b[o + 3] = static_cast<std::uint8_t>((v >> 24u) & 0xFFu);
}
constexpr std::uint32_t r_type(std::uint8_t rs, std::uint8_t rt, std::uint8_t rd, std::uint8_t sa, std::uint8_t funct) {
    return (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) | funct;
}
constexpr std::uint32_t i_type(std::uint8_t op, std::uint8_t rs, std::uint8_t rt, std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) | imm;
}
constexpr std::uint32_t j_type(std::uint8_t op, std::uint32_t target) {
    return (static_cast<std::uint32_t>(op) << 26u) | ((target >> 2u) & 0x03FFFFFFu);
}

b3r::runtime::Ps2MemoryMap make_memory(const std::vector<std::uint32_t>& words,
                                        std::uint32_t flags = 5u,
                                        std::uint32_t base = 0x1000u) {
    constexpr std::uint32_t phoff = 52u;
    constexpr std::uint32_t fileoff = 0x100u;
    const auto payload = static_cast<std::uint32_t>(words.size() * 4u);
    Bytes bytes(fileoff + payload, 0u);
    bytes[0]=0x7F; bytes[1]='E'; bytes[2]='L'; bytes[3]='F'; bytes[4]=1; bytes[5]=1; bytes[6]=1;
    put_u16(bytes,16,2); put_u16(bytes,18,8); put_u32(bytes,20,1); put_u32(bytes,24,base); put_u32(bytes,28,phoff);
    put_u16(bytes,40,52); put_u16(bytes,42,32); put_u16(bytes,44,1);
    put_u32(bytes,phoff+0,1); put_u32(bytes,phoff+4,fileoff); put_u32(bytes,phoff+8,base); put_u32(bytes,phoff+12,base);
    put_u32(bytes,phoff+16,payload); put_u32(bytes,phoff+20,payload); put_u32(bytes,phoff+24,flags); put_u32(bytes,phoff+28,0x1000);
    for (std::size_t i=0;i<words.size();++i) put_u32(bytes,fileoff+i*4u,words[i]);
    const auto elf=b3r::recompiler::parse_ps2_elf(bytes); expect(elf.ok(),"ELF fixture must parse");
    auto map=b3r::runtime::Ps2MemoryMap::from_elf(*elf.image); expect(map.ok(),"memory fixture must map");
    return std::move(*map.memory);
}

template <typename T, typename Pred>
bool any_of(const std::vector<T>& items, Pred pred) {
    return std::any_of(items.begin(), items.end(), pred);
}
} // namespace

int main() {
    using namespace b3r::analysis;

    {
        // 1000: BEQ -> 1010, fallthrough 1008; 1008: JAL 1100, continuation 1010; 1010: JR ra.
        auto memory = make_memory({
            i_type(0x04, 4, 0, 3), 0u,
            j_type(0x03, 0x1100u), 0u,
            r_type(31,0,0,0,0x08), 0u,
        });
        const auto result = analyze_r5900_reachability(memory, 0x1000u);
        expect(result.ok(), "reachable graph must analyze");
        const auto& graph=*result.graph;
        expect(graph.blocks.size()==3u, "branch/fallthrough/call continuation must discover three blocks");
        expect(graph.blocks[0].start_pc==0x1000u && graph.blocks[1].start_pc==0x1010u && graph.blocks[2].start_pc==0x1008u,
               "worklist discovery order must be deterministic");
        expect(graph.calls.size()==1u, "direct call must be recorded once");
        expect(graph.calls[0].target==0x1100u && !graph.calls[0].indirect, "direct call target must be evidence only");
        expect(!any_of(graph.blocks, [](const auto& b){ return b.start_pc==0x1100u; }), "call target must not be traversed as same-flow block");
        expect(any_of(graph.issues, [](const auto& i){ return i.kind==R5900ReachabilityIssueKind::UnresolvedIndirectExit && i.source_block==0x1010u; }),
               "JR exit must remain unresolved evidence");
    }

    {
        auto memory = make_memory({j_type(0x02,0x1000u),0u});
        const auto result=analyze_r5900_reachability(memory,0x1000u);
        expect(result.ok() && result.graph->blocks.size()==1u, "direct self-loop must terminate worklist without duplicate blocks");
    }

    {
        auto memory = make_memory({j_type(0x02,0x2000u),0u});
        const auto result=analyze_r5900_reachability(memory,0x1000u);
        expect(result.ok(), "unmapped successor must not discard valid source block");
        expect(result.graph->blocks.size()==1u, "unmapped successor must not become a block");
        expect(any_of(result.graph->issues, [](const auto& i){ return i.kind==R5900ReachabilityIssueKind::TargetAnalysisFailed && i.target==0x2000u; }),
               "unmapped target failure must be recorded");
    }

    {
        auto memory = make_memory({i_type(0x04,4,0,3),0u,0u,0u,r_type(31,0,0,0,0x08),0u});
        R5900ReachabilityOptions options{}; options.max_blocks=1u;
        const auto result=analyze_r5900_reachability(memory,0x1000u,options);
        expect(result.ok() && result.graph->blocks.size()==1u, "block limit must bound traversal");
        expect(any_of(result.graph->issues, [](const auto& i){ return i.kind==R5900ReachabilityIssueKind::BlockLimitReached; }),
               "block limit truncation must be explicit evidence");
    }

    {
        // Entry block contains 1004, then branches backward to 1004: discovered leader overlaps existing block.
        auto memory = make_memory({0u,0u,i_type(0x04,4,0,0xFFFE),0u,r_type(31,0,0,0,0x08),0u});
        const auto result=analyze_r5900_reachability(memory,0x1000u);
        expect(result.ok(), "overlapping leader case must remain analyzable");
        expect(any_of(result.graph->issues, [](const auto& i){ return i.kind==R5900ReachabilityIssueKind::LeaderInsideBlock && i.target==0x1004u; }),
               "leader inside previously discovered linear block must be surfaced");
    }

    {
        auto memory=make_memory({0u},6u);
        const auto result=analyze_r5900_reachability(memory,0x1000u);
        expect(!result.ok() && result.error==R5900ReachabilityError::EntryAnalysisFailed,
               "invalid entry point must be fatal rather than an empty graph");
    }

    std::cout << "r5900_reachability_tests: PASS\n";
    return EXIT_SUCCESS;
}
