#pragma once

#include "analysis/r5900_control_flow.h"
#include "recompiler/r5900_ir_executor.h"
#include "recompiler/windows/r5900_x64_backend.h"
#include "runtime/ps2_memory_map.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace b3r::recompiler {

enum class R5900DispatchStopReason {
    BlockBudgetExhausted,
    ControlFlow,
    UnsupportedInstruction,
    Trap,
    InvalidBlockBudget,
    AnalysisFailure,
    LoweringFailure,
    CompileFailure,
    MemoryAccessFailure,
};

struct R5900DispatchResult {
    R5900DispatchStopReason reason{R5900DispatchStopReason::AnalysisFailure};
    std::uint32_t next_pc{};
    std::size_t blocks_executed{};
    std::size_t instructions_executed{};
    std::size_t cache_hits{};
    std::size_t fast_cache_hits{};
    std::size_t cache_misses{};
    std::size_t recompilations{};
    std::string message{};
};

struct R5900BlockDispatcherOptions {
    analysis::R5900ControlFlowOptions block_options{};
};

class R5900BlockDispatcher {
public:
    explicit R5900BlockDispatcher(runtime::Ps2MemoryMap& memory,
                                  R5900BlockDispatcherOptions options = {});

    [[nodiscard]] R5900DispatchResult run(std::uint32_t start_pc,
                                          R5900IrExecutionState& state,
                                          std::size_t max_blocks);

    void clear_cache() noexcept;
    [[nodiscard]] std::size_t cache_size() const noexcept;

private:
    struct CachedBlock {
        std::uint32_t start_pc{};
        std::uint32_t end_pc_exclusive{};
        std::uint64_t fingerprint{};
        std::vector<std::uint32_t> guest_words{};
        std::size_t guest_instruction_count{};
        bool fast_replay_eligible{};
        R5900X64CompiledBlock native_block{};
    };

    runtime::Ps2MemoryMap& memory_;
    R5900BlockDispatcherOptions options_{};
    std::unordered_map<std::uint32_t, CachedBlock> cache_{};
};

} // namespace b3r::recompiler