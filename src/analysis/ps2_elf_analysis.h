#pragma once

#include "analysis/r5900_reachability.h"
#include "recompiler/ps2_elf.h"
#include "runtime/ps2_memory_map.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace b3r::analysis {

enum class Ps2ElfAnalysisError {
    None = 0,
    ElfParseFailed,
    MemoryMapFailed,
    ReachabilityFailed,
};

struct Ps2ElfAnalysisResult {
    Ps2ElfAnalysisError error{Ps2ElfAnalysisError::None};
    std::string message{};
    std::optional<std::string> report{};
    recompiler::Ps2ElfError elf_error{recompiler::Ps2ElfError::None};
    runtime::Ps2MemoryMapBuildError memory_error{runtime::Ps2MemoryMapBuildError::None};
    R5900ReachabilityError reachability_error{R5900ReachabilityError::None};

    [[nodiscard]] bool ok() const noexcept {
        return error == Ps2ElfAnalysisError::None && report.has_value();
    }
};

[[nodiscard]] Ps2ElfAnalysisResult
analyze_ps2_elf(std::span<const std::uint8_t> bytes,
                R5900ReachabilityOptions options = {});

} // namespace b3r::analysis
