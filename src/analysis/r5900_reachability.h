#pragma once

#include "analysis/r5900_control_flow.h"
#include "runtime/ps2_memory_map.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace b3r::analysis {

enum class R5900ReachabilityError {
    None = 0,
    InvalidBlockLimit,
    EntryAnalysisFailed,
};

enum class R5900ReachabilityIssueKind {
    UnresolvedIndirectExit = 0,
    TargetAnalysisFailed,
    BlockLimitReached,
    LeaderInsideBlock,
    LeaderInsideDelaySlot,
};

struct R5900ReachabilityCall {
    std::uint32_t source_block{};
    std::uint32_t call_pc{};
    bool indirect{};
    std::optional<std::uint32_t> target{};
};

struct R5900ReachabilityIssue {
    R5900ReachabilityIssueKind kind{R5900ReachabilityIssueKind::TargetAnalysisFailed};
    std::uint32_t source_block{};
    std::optional<std::uint32_t> target{};
    std::optional<std::uint32_t> related_block{};
    R5900ControlFlowError analysis_error{R5900ControlFlowError::None};
};

struct R5900ReachabilityGraph {
    std::uint32_t entry_pc{};
    std::vector<R5900BasicBlock> blocks{};
    std::vector<R5900ReachabilityCall> calls{};
    std::vector<R5900ReachabilityIssue> issues{};
};

struct R5900ReachabilityOptions {
    std::size_t max_blocks{4096};
    R5900ControlFlowOptions block_options{};
};

struct R5900ReachabilityResult {
    R5900ReachabilityError error{R5900ReachabilityError::None};
    std::string message{};
    std::optional<R5900ReachabilityGraph> graph{};

    [[nodiscard]] bool ok() const noexcept {
        return error == R5900ReachabilityError::None && graph.has_value();
    }
};

[[nodiscard]] R5900ReachabilityResult
analyze_r5900_reachability(const runtime::Ps2MemoryMap& memory,
                           std::uint32_t entry_pc,
                           R5900ReachabilityOptions options = {});

} // namespace b3r::analysis
