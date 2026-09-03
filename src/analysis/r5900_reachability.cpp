#include "analysis/r5900_reachability.h"

#include <algorithm>
#include <deque>
#include <set>
#include <tuple>
#include <utility>

namespace b3r::analysis {
namespace {

struct PendingTarget {
    std::uint32_t pc{};
    std::uint32_t source_block{};
    bool is_entry{};
};

R5900ReachabilityResult fail(R5900ReachabilityError error, std::string message) {
    R5900ReachabilityResult result{};
    result.error = error;
    result.message = std::move(message);
    return result;
}

R5900ReachabilityResult succeed(R5900ReachabilityGraph graph) {
    R5900ReachabilityResult result{};
    result.graph = std::move(graph);
    return result;
}

void add_issue(R5900ReachabilityGraph& graph,
               R5900ReachabilityIssueKind kind,
               std::uint32_t source_block,
               std::optional<std::uint32_t> target = std::nullopt,
               std::optional<std::uint32_t> related_block = std::nullopt,
               R5900ControlFlowError analysis_error = R5900ControlFlowError::None) {
    graph.issues.push_back(R5900ReachabilityIssue{
        kind,
        source_block,
        target,
        related_block,
        analysis_error,
    });
}

std::uint32_t terminator_pc(const R5900BasicBlock& block) noexcept {
    return block.instructions.empty() ? block.start_pc : block.instructions.back().pc;
}

void detect_overlapping_leaders(R5900ReachabilityGraph& graph) {
    using IssueKey = std::tuple<int, std::uint32_t, std::uint32_t>;
    std::set<IssueKey> emitted;

    for (const auto& leader_block : graph.blocks) {
        const std::uint32_t leader = leader_block.start_pc;

        for (const auto& containing_block : graph.blocks) {
            if (leader_block.start_pc == containing_block.start_pc) {
                continue;
            }

            const auto instruction_it = std::find_if(
                containing_block.instructions.begin(),
                containing_block.instructions.end(),
                [leader](const R5900InstructionSite& site) { return site.pc == leader; });

            if (instruction_it != containing_block.instructions.end() &&
                instruction_it != containing_block.instructions.begin()) {
                const IssueKey key{
                    static_cast<int>(R5900ReachabilityIssueKind::LeaderInsideBlock),
                    leader,
                    containing_block.start_pc,
                };
                if (emitted.insert(key).second) {
                    add_issue(graph,
                              R5900ReachabilityIssueKind::LeaderInsideBlock,
                              leader_block.start_pc,
                              leader,
                              containing_block.start_pc);
                }
            }

            if (containing_block.delay_slot.has_value() && containing_block.delay_slot->pc == leader) {
                const IssueKey key{
                    static_cast<int>(R5900ReachabilityIssueKind::LeaderInsideDelaySlot),
                    leader,
                    containing_block.start_pc,
                };
                if (emitted.insert(key).second) {
                    add_issue(graph,
                              R5900ReachabilityIssueKind::LeaderInsideDelaySlot,
                              leader_block.start_pc,
                              leader,
                              containing_block.start_pc);
                }
            }
        }
    }
}

} // namespace

R5900ReachabilityResult analyze_r5900_reachability(const runtime::Ps2MemoryMap& memory,
                                                    std::uint32_t entry_pc,
                                                    R5900ReachabilityOptions options) {
    if (options.max_blocks == 0u) {
        return fail(R5900ReachabilityError::InvalidBlockLimit,
                    "R5900 reachability block limit must be non-zero");
    }

    R5900ReachabilityGraph graph{};
    graph.entry_pc = entry_pc;
    graph.blocks.reserve(std::min<std::size_t>(options.max_blocks, 256u));

    std::deque<PendingTarget> worklist;
    std::set<std::uint32_t> scheduled;

    worklist.push_back(PendingTarget{entry_pc, entry_pc, true});
    scheduled.insert(entry_pc);

    while (!worklist.empty()) {
        const PendingTarget pending = worklist.front();
        worklist.pop_front();

        if (graph.blocks.size() >= options.max_blocks) {
            add_issue(graph,
                      R5900ReachabilityIssueKind::BlockLimitReached,
                      pending.source_block,
                      pending.pc);
            break;
        }

        const auto block_result = analyze_r5900_basic_block(memory, pending.pc, options.block_options);
        if (!block_result.ok()) {
            if (pending.is_entry) {
                return fail(R5900ReachabilityError::EntryAnalysisFailed,
                            "R5900 entry basic-block analysis failed: " + block_result.message);
            }

            add_issue(graph,
                      R5900ReachabilityIssueKind::TargetAnalysisFailed,
                      pending.source_block,
                      pending.pc,
                      std::nullopt,
                      block_result.error);
            continue;
        }

        graph.blocks.push_back(*block_result.block);
        const auto& block = graph.blocks.back();
        const std::uint32_t call_pc = terminator_pc(block);

        for (const auto& edge : block.edges) {
            switch (edge.kind) {
            case R5900EdgeKind::BranchTaken:
            case R5900EdgeKind::BranchNotTaken:
            case R5900EdgeKind::DirectJump:
            case R5900EdgeKind::CallContinuation:
            case R5900EdgeKind::Fallthrough:
                if (edge.target.has_value() && scheduled.insert(*edge.target).second) {
                    worklist.push_back(PendingTarget{*edge.target, block.start_pc, false});
                }
                break;

            case R5900EdgeKind::DirectCall:
                graph.calls.push_back(R5900ReachabilityCall{
                    block.start_pc,
                    call_pc,
                    false,
                    edge.target,
                });
                break;

            case R5900EdgeKind::IndirectCall:
                graph.calls.push_back(R5900ReachabilityCall{
                    block.start_pc,
                    call_pc,
                    true,
                    std::nullopt,
                });
                add_issue(graph,
                          R5900ReachabilityIssueKind::UnresolvedIndirectExit,
                          block.start_pc);
                break;

            case R5900EdgeKind::IndirectJump:
                add_issue(graph,
                          R5900ReachabilityIssueKind::UnresolvedIndirectExit,
                          block.start_pc);
                break;
            }
        }
    }

    detect_overlapping_leaders(graph);
    return succeed(std::move(graph));
}

} // namespace b3r::analysis
