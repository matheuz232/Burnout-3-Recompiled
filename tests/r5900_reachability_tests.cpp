#include "analysis/ps2_pad_fingerprint.h"
#include "analysis/r5900_reachability.h"
#include "recompiler/ps2_elf.h"
#include "runtime/ps2_memory_map.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>
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

b3r::analysis::R5900InstructionSite site(std::uint32_t pc, std::uint32_t word) {
    return {pc, b3r::recompiler::decode_r5900(word)};
}

b3r::analysis::R5900BasicBlock block(
    std::uint32_t start_pc,
    std::vector<std::pair<std::uint32_t, std::uint32_t>> words,
    std::vector<b3r::analysis::R5900ControlFlowEdge> edges = {}) {
    b3r::analysis::R5900BasicBlock result{};
    result.start_pc = start_pc;
    for (const auto& [pc, word] : words) {
        result.instructions.push_back(site(pc, word));
    }
    result.edges = std::move(edges);
    return result;
}

const b3r::analysis::PadFingerprintCandidate* find_candidate(
    const std::vector<b3r::analysis::PadFingerprintCandidate>& candidates,
    b3r::analysis::PadBindingFunction function,
    std::uint32_t pc) {
    const auto it = std::find_if(candidates.begin(), candidates.end(),
        [function, pc](const auto& candidate) {
            return candidate.function == function && candidate.guest_pc == pc;
        });
    return it == candidates.end() ? nullptr : &*it;
}

void test_pad_fingerprint_scores() {
    using namespace b3r::analysis;

    PadFingerprintFeatures init{};
    init.rpc_bind_new_1 = true;
    init.rpc_bind_new_2 = true;
    init.command_init = true;
    init.direct_call_count = 2u;
    expect(score_pad_init(init) == 170u, "padInit score must match fixed weights");

    PadFingerprintFeatures open{};
    open.command_open = true;
    open.alignment_mask_0x3f = true;
    open.port_bound_2 = true;
    open.slot_bound_8 = true;
    expect(score_pad_port_open(open) == 170u, "padPortOpen score must match fixed weights");

    PadFingerprintFeatures state{};
    state.state_stable_6 = true;
    state.port_bound_2 = true;
    state.slot_bound_8 = true;
    expect(score_pad_get_state(state) == 120u, "padGetState score must match fixed weights");

    PadFingerprintFeatures read{};
    read.copies_32_bytes = true;
    read.port_bound_2 = true;
    read.slot_bound_8 = true;
    expect(score_pad_read(read) == 120u, "padRead score must match fixed weights");

    PadFingerprintFeatures close{};
    close.command_close = true;
    close.port_bound_2 = true;
    close.slot_bound_8 = true;
    expect(score_pad_port_close(close) == 140u, "padPortClose score must match fixed weights");

    PadFingerprintFeatures end{};
    end.command_end = true;
    expect(score_pad_end(end) == 120u, "padEnd command must produce fixed score 120");

    PadFingerprintFeatures weak{};
    weak.port_bound_2 = true;
    expect(score_pad_get_state(weak) < kPadFingerprintThreshold,
           "below-threshold state evidence must remain weak");
}

void test_pad_fingerprint_function_views_and_features() {
    using namespace b3r::analysis;

    auto memory = make_memory({0u});

    {
        R5900ReachabilityGraph graph{};
        graph.entry_pc = 0x1000u;
        graph.blocks.push_back(block(0x1000u, {}, {{R5900EdgeKind::DirectJump, 0x1010u}}));
        graph.blocks.push_back(block(0x1010u, {{0x1010u, i_type(0x0d, 0, 8, 0x000fu)}}));
        const auto candidates = scan_ps2_pad_fingerprints(memory, graph);
        expect(find_candidate(candidates, PadBindingFunction::PadEnd, 0x1000u) != nullptr,
               "function view must follow direct jump edges");
    }

    {
        R5900ReachabilityGraph graph{};
        graph.entry_pc = 0x1000u;
        graph.blocks.push_back(block(0x1000u, {}, {{R5900EdgeKind::DirectCall, 0x1100u}}));
        graph.blocks.push_back(block(0x1100u, {{0x1100u, i_type(0x0d, 0, 8, 0x000fu)}}));
        graph.calls.push_back({0x1000u, 0x1000u, false, 0x1100u});
        const auto candidates = scan_ps2_pad_fingerprints(memory, graph);
        expect(find_candidate(candidates, PadBindingFunction::PadEnd, 0x1000u) == nullptr,
               "function view must stop at direct-call roots");
        expect(find_candidate(candidates, PadBindingFunction::PadEnd, 0x1100u) != nullptr,
               "direct-call target present in graph must become its own root");
    }

    {
        R5900ReachabilityGraph graph{};
        graph.entry_pc = 0x1000u;
        graph.blocks.push_back(block(0x1000u, {{0x1000u, i_type(0x0d, 0, 8, 0x0006u)}}));
        const auto candidates = scan_ps2_pad_fingerprints(memory, graph);
        expect(find_candidate(candidates, PadBindingFunction::PadGetState, 0x1000u) == nullptr,
               "score below 100 must not emit a candidate");
    }

    {
        R5900ReachabilityGraph graph{};
        graph.entry_pc = 0x1000u;
        graph.blocks.push_back(block(0x1000u, {
            {0x1000u, i_type(0x0f, 0, 8, 0x8000u)},
            {0x1004u, i_type(0x0d, 8, 8, 0x0100u)},
            {0x1008u, i_type(0x0d, 0, 9, 0x0010u)},
        }));
        const auto candidates = scan_ps2_pad_fingerprints(memory, graph);
        const auto* candidate = find_candidate(candidates, PadBindingFunction::PadInit, 0x1000u);
        expect(candidate != nullptr && candidate->score == 110u,
               "public PAD RPC id plus init command must reach padInit threshold");
    }

    {
        R5900ReachabilityGraph graph{};
        graph.entry_pc = 0x1000u;
        graph.blocks.push_back(block(0x1000u, {
            {0x1000u, i_type(0x0d, 0, 8, 0x0001u)},
            {0x1004u, i_type(0x0c, 4, 9, 0x003fu)},
            {0x1008u, i_type(0x0b, 4, 10, 0x0002u)},
            {0x100cu, i_type(0x0b, 5, 11, 0x0008u)},
        }));
        const auto candidates = scan_ps2_pad_fingerprints(memory, graph);
        const auto* candidate = find_candidate(candidates, PadBindingFunction::PadPortOpen, 0x1000u);
        expect(candidate != nullptr && candidate->score == 170u,
               "padPortOpen public command, alignment and bounds must reach threshold");
    }

    {
        R5900ReachabilityGraph graph{};
        graph.entry_pc = 0x1000u;
        graph.blocks.push_back(block(0x1000u, {
            {0x1000u, i_type(0x0d, 0, 8, 0x0006u)},
            {0x1004u, i_type(0x0b, 4, 9, 0x0002u)},
            {0x1008u, i_type(0x0b, 5, 10, 0x0008u)},
        }));
        const auto candidates = scan_ps2_pad_fingerprints(memory, graph);
        const auto* candidate = find_candidate(candidates, PadBindingFunction::PadGetState, 0x1000u);
        expect(candidate != nullptr && candidate->score == 120u,
               "stable state plus port/slot bounds must identify padGetState candidate");
    }

    {
        R5900ReachabilityGraph graph{};
        graph.entry_pc = 0x1000u;
        graph.blocks.push_back(block(0x1000u, {
            {0x1000u, i_type(0x0d, 0, 8, 32u)},
            {0x1004u, i_type(0x0b, 4, 9, 2u)},
            {0x1008u, i_type(0x0b, 5, 10, 8u)},
            {0x100cu, i_type(0x23, 6, 11, 0u)},
            {0x1010u, i_type(0x2b, 7, 11, 0u)},
            {0x1014u, i_type(0x09, 8, 8, 0xfffcu)},
            {0x1018u, i_type(0x05, 8, 0, 0xfffcu)},
        }));
        const auto candidates = scan_ps2_pad_fingerprints(memory, graph);
        const auto* candidate = find_candidate(candidates, PadBindingFunction::PadRead, 0x1000u);
        expect(candidate != nullptr && candidate->score == 120u && candidate->features.copies_32_bytes,
               "copy loop plus port/slot bounds must identify padRead candidate");
    }

    {
        R5900ReachabilityGraph graph{};
        graph.entry_pc = 0x1000u;
        graph.blocks.push_back(block(0x1000u, {{0x1000u, i_type(0x0d, 0, 8, 32u)}}));
        const auto candidates = scan_ps2_pad_fingerprints(memory, graph);
        expect(find_candidate(candidates, PadBindingFunction::PadRead, 0x1000u) == nullptr,
               "bare immediate 32 must not imply a copy loop");
    }

    {
        R5900ReachabilityGraph graph{};
        graph.entry_pc = 0x1000u;
        graph.blocks.push_back(block(0x1000u, {{0x1000u, i_type(0x0d, 0, 8, 0x000eu)}}));
        const auto candidates = scan_ps2_pad_fingerprints(memory, graph);
        const auto* candidate = find_candidate(candidates, PadBindingFunction::PadPortClose, 0x1000u);
        expect(candidate != nullptr && candidate->score == 100u,
               "public close command alone must meet its fixed candidate threshold");
    }

    {
        R5900ReachabilityGraph graph{};
        graph.entry_pc = 0x1000u;
        graph.blocks.push_back(block(0x1000u, {{0x1000u, i_type(0x0d, 0, 8, 0x000fu)}}));
        const auto candidates = scan_ps2_pad_fingerprints(memory, graph);
        const auto* candidate = find_candidate(candidates, PadBindingFunction::PadEnd, 0x1000u);
        expect(candidate != nullptr && candidate->score == 120u,
               "public end command must identify padEnd candidate");
    }

    {
        R5900ReachabilityGraph graph{};
        graph.entry_pc = 0x1000u;
        graph.blocks.push_back(block(0x1000u, {{0x1000u, i_type(0x0d, 0, 8, 0x000fu)}}));
        graph.blocks.push_back(block(0x1100u, {{0x1100u, i_type(0x0d, 0, 8, 0x000fu)}}));
        graph.calls.push_back({0x1000u, 0x1000u, false, 0x1100u});
        const auto candidates = scan_ps2_pad_fingerprints(memory, graph);
        const auto count = static_cast<std::size_t>(std::count_if(
            candidates.begin(), candidates.end(), [](const auto& candidate) {
                return candidate.function == PadBindingFunction::PadEnd;
            }));
        expect(count == 2u, "multiple candidates for the same PAD function must be preserved");
    }

    {
        R5900ReachabilityGraph graph{};
        graph.entry_pc = 0x1000u;
        graph.blocks.push_back(block(0x1000u, {}));
        graph.blocks.push_back(block(0x1200u, {{0x1200u, i_type(0x0d, 0, 8, 0x000fu)}}));
        const auto candidates = scan_ps2_pad_fingerprints(memory, graph);
        expect(candidates.empty(), "unreachable graph blocks must not be scanned as function roots");
    }

    {
        PadFingerprintCandidate candidate{};
        candidate.function = PadBindingFunction::PadRead;
        candidate.guest_pc = 0x1000u;
        candidate.score = 120u;
        candidate.features.port_bound_2 = true;
        candidate.features.slot_bound_8 = true;
        candidate.features.copies_32_bytes = true;
        const auto evidence = make_ps2_pad_fingerprint_evidence({&candidate, 1u});
        expect(evidence.size() == 1u &&
               evidence[0].detail == "port_bound_2,slot_bound_8,copies_32_bytes",
               "fingerprint evidence detail must follow feature declaration order");
    }
}

} // namespace

int main() {
    using namespace b3r::analysis;

    test_pad_fingerprint_scores();
    test_pad_fingerprint_function_views_and_features();

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