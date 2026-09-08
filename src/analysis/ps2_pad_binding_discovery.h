#pragma once

#include "analysis/elf32_metadata.h"
#include "recompiler/ps2_elf.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace b3r::analysis {

enum class PadBindingFunction : std::uint8_t {
    PadInit,
    PadPortOpen,
    PadGetState,
    PadRead,
    PadPortClose,
    PadEnd,
};

enum class PadBindingEvidenceKind : std::uint8_t {
    ElfSymbol,
    StaticFingerprint,
};

enum class PadBindingConfidence : std::uint8_t {
    Unresolved,
    Candidate,
    Trusted,
};

struct PadBindingEvidence {
    PadBindingFunction function{};
    PadBindingEvidenceKind kind{PadBindingEvidenceKind::ElfSymbol};
    std::uint32_t guest_pc{};
    std::uint32_t score{};
    std::string detail{};
};

struct PadBindingResolution {
    PadBindingFunction function{};
    PadBindingConfidence confidence{PadBindingConfidence::Unresolved};
    std::optional<std::uint32_t> guest_pc{};
    std::vector<PadBindingEvidence> evidence{};
};

struct PadSymbolEvidenceResult {
    std::vector<PadBindingEvidence> evidence{};
    std::vector<std::string> diagnostics{};
};

struct PadBindingDiscoveryResult {
    std::array<PadBindingResolution, 6> resolutions{};
    std::vector<std::string> diagnostics{};
};

namespace ps2_pad_binding_detail {

[[nodiscard]] inline const char* function_name(PadBindingFunction function) noexcept {
    switch (function) {
    case PadBindingFunction::PadInit:
        return "padInit";
    case PadBindingFunction::PadPortOpen:
        return "padPortOpen";
    case PadBindingFunction::PadGetState:
        return "padGetState";
    case PadBindingFunction::PadRead:
        return "padRead";
    case PadBindingFunction::PadPortClose:
        return "padPortClose";
    case PadBindingFunction::PadEnd:
        return "padEnd";
    }
    return "unknown";
}

[[nodiscard]] inline std::optional<PadBindingFunction>
function_for_symbol(std::string_view name) noexcept {
    struct Entry {
        std::string_view name;
        PadBindingFunction function;
    };

    constexpr std::array<Entry, 12> entries{{
        {"padInit", PadBindingFunction::PadInit},
        {"_padInit", PadBindingFunction::PadInit},
        {"padPortOpen", PadBindingFunction::PadPortOpen},
        {"_padPortOpen", PadBindingFunction::PadPortOpen},
        {"padGetState", PadBindingFunction::PadGetState},
        {"_padGetState", PadBindingFunction::PadGetState},
        {"padRead", PadBindingFunction::PadRead},
        {"_padRead", PadBindingFunction::PadRead},
        {"padPortClose", PadBindingFunction::PadPortClose},
        {"_padPortClose", PadBindingFunction::PadPortClose},
        {"padEnd", PadBindingFunction::PadEnd},
        {"_padEnd", PadBindingFunction::PadEnd},
    }};

    for (const auto& entry : entries) {
        if (name == entry.name) {
            return entry.function;
        }
    }
    return std::nullopt;
}

[[nodiscard]] inline bool is_file_backed_executable_pc(
    const recompiler::Ps2ElfImage& image,
    std::uint32_t pc) noexcept {
    constexpr std::uint32_t kPfExecute = 0x1u;
    for (const auto& segment : image.load_segments()) {
        if ((segment.flags & kPfExecute) == 0u) {
            continue;
        }
        const std::uint64_t begin = segment.virtual_address;
        const std::uint64_t end = begin + static_cast<std::uint64_t>(segment.file_size);
        const std::uint64_t address = pc;
        if (address >= begin && address < end) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool evidence_less(const PadBindingEvidence& lhs,
                                        const PadBindingEvidence& rhs) noexcept {
    if (lhs.function != rhs.function) {
        return lhs.function < rhs.function;
    }
    if (lhs.kind != rhs.kind) {
        return lhs.kind < rhs.kind;
    }
    if (lhs.guest_pc != rhs.guest_pc) {
        return lhs.guest_pc < rhs.guest_pc;
    }
    if (lhs.score != rhs.score) {
        return lhs.score < rhs.score;
    }
    return lhs.detail < rhs.detail;
}

[[nodiscard]] inline bool evidence_equal(const PadBindingEvidence& lhs,
                                         const PadBindingEvidence& rhs) noexcept {
    return lhs.function == rhs.function && lhs.kind == rhs.kind &&
           lhs.guest_pc == rhs.guest_pc && lhs.score == rhs.score &&
           lhs.detail == rhs.detail;
}

[[nodiscard]] inline std::vector<std::uint32_t> distinct_pcs(
    std::span<const PadBindingEvidence> evidence,
    PadBindingEvidenceKind kind) {
    std::vector<std::uint32_t> pcs{};
    for (const auto& item : evidence) {
        if (item.kind == kind) {
            pcs.push_back(item.guest_pc);
        }
    }
    std::sort(pcs.begin(), pcs.end());
    pcs.erase(std::unique(pcs.begin(), pcs.end()), pcs.end());
    return pcs;
}

} // namespace ps2_pad_binding_detail

[[nodiscard]] inline PadSymbolEvidenceResult
collect_ps2_pad_symbol_evidence(const Elf32MetadataResult& metadata,
                                const recompiler::Ps2ElfImage& image) {
    PadSymbolEvidenceResult result{};
    if (metadata.status != Elf32MetadataStatus::Available) {
        return result;
    }

    for (const auto& symbol : metadata.symbols) {
        const auto function = ps2_pad_binding_detail::function_for_symbol(symbol.name);
        if (!function.has_value()) {
            continue;
        }
        if (symbol.value == 0u) {
            continue;
        }
        if (symbol.type != kSttFunc && symbol.type != kSttNotype) {
            continue;
        }
        if (!ps2_pad_binding_detail::is_file_backed_executable_pc(image, symbol.value)) {
            result.diagnostics.push_back(
                std::string("PAD symbol ") + symbol.name +
                " rejected because guest PC is outside executable file-backed PT_LOAD memory");
            continue;
        }

        result.evidence.push_back(PadBindingEvidence{
            *function,
            PadBindingEvidenceKind::ElfSymbol,
            symbol.value,
            1000u,
            symbol.name,
        });
    }

    std::sort(result.evidence.begin(), result.evidence.end(),
              ps2_pad_binding_detail::evidence_less);
    std::sort(result.diagnostics.begin(), result.diagnostics.end());
    return result;
}

[[nodiscard]] inline PadBindingDiscoveryResult
resolve_ps2_pad_binding_evidence(std::span<const PadBindingEvidence> evidence,
                                 std::span<const std::string> diagnostics = {}) {
    PadBindingDiscoveryResult result{};
    result.diagnostics.assign(diagnostics.begin(), diagnostics.end());

    for (std::size_t index = 0; index < result.resolutions.size(); ++index) {
        result.resolutions[index].function = static_cast<PadBindingFunction>(index);
    }

    std::vector<PadBindingEvidence> sorted(evidence.begin(), evidence.end());
    std::sort(sorted.begin(), sorted.end(), ps2_pad_binding_detail::evidence_less);

    for (std::size_t index = 0; index < result.resolutions.size(); ++index) {
        const auto function = static_cast<PadBindingFunction>(index);
        std::vector<PadBindingEvidence> function_evidence{};
        for (const auto& item : sorted) {
            if (item.function == function) {
                function_evidence.push_back(item);
            }
        }

        const auto symbol_pcs = ps2_pad_binding_detail::distinct_pcs(
            function_evidence, PadBindingEvidenceKind::ElfSymbol);
        const auto fingerprint_pcs = ps2_pad_binding_detail::distinct_pcs(
            function_evidence, PadBindingEvidenceKind::StaticFingerprint);

        auto& resolution = result.resolutions[index];
        if (symbol_pcs.size() >= 2u) {
            resolution.confidence = PadBindingConfidence::Unresolved;
            resolution.guest_pc.reset();
            result.diagnostics.push_back(
                std::string("multiple ELF symbol PCs for ") +
                ps2_pad_binding_detail::function_name(function));
        } else if (symbol_pcs.size() == 1u) {
            resolution.confidence = PadBindingConfidence::Trusted;
            resolution.guest_pc = symbol_pcs.front();
            const bool has_conflict = std::any_of(
                fingerprint_pcs.begin(), fingerprint_pcs.end(),
                [&](std::uint32_t pc) { return pc != symbol_pcs.front(); });
            if (has_conflict) {
                result.diagnostics.push_back(
                    std::string("static fingerprint conflicts with trusted ELF symbol for ") +
                    ps2_pad_binding_detail::function_name(function));
            }
        } else if (fingerprint_pcs.size() == 1u) {
            resolution.confidence = PadBindingConfidence::Candidate;
            resolution.guest_pc = fingerprint_pcs.front();
        } else if (fingerprint_pcs.size() >= 2u) {
            resolution.confidence = PadBindingConfidence::Candidate;
            resolution.guest_pc.reset();
            result.diagnostics.push_back(
                std::string("multiple static fingerprint PCs for ") +
                ps2_pad_binding_detail::function_name(function));
        }

        function_evidence.erase(
            std::unique(function_evidence.begin(), function_evidence.end(),
                        ps2_pad_binding_detail::evidence_equal),
            function_evidence.end());
        resolution.evidence = std::move(function_evidence);
    }

    std::sort(result.diagnostics.begin(), result.diagnostics.end());
    result.diagnostics.erase(
        std::unique(result.diagnostics.begin(), result.diagnostics.end()),
        result.diagnostics.end());
    return result;
}

} // namespace b3r::analysis
