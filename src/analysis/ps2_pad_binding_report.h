#pragma once

#include "analysis/ps2_pad_binding_discovery.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace b3r::analysis {
namespace ps2_pad_binding_report_detail {

[[nodiscard]] inline const char* evidence_kind_name(PadBindingEvidenceKind kind) noexcept {
    switch (kind) {
    case PadBindingEvidenceKind::ElfSymbol:
        return "elf_symbol";
    case PadBindingEvidenceKind::StaticFingerprint:
        return "static_fingerprint";
    }
    return "unknown";
}

[[nodiscard]] inline const char* confidence_name(PadBindingConfidence confidence) noexcept {
    switch (confidence) {
    case PadBindingConfidence::Unresolved:
        return "unresolved";
    case PadBindingConfidence::Candidate:
        return "candidate";
    case PadBindingConfidence::Trusted:
        return "trusted";
    }
    return "unknown";
}

[[nodiscard]] inline std::string format_pc(std::uint32_t pc) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::nouppercase << std::setfill('0')
           << std::setw(8) << pc;
    return stream.str();
}

[[nodiscard]] inline std::uint32_t max_score(
    std::span<const PadBindingEvidence> evidence) noexcept {
    std::uint32_t result = 0u;
    for (const auto& item : evidence) {
        result = std::max(result, item.score);
    }
    return result;
}

} // namespace ps2_pad_binding_report_detail

[[nodiscard]] inline std::string
render_ps2_pad_binding_report(const PadBindingDiscoveryResult& result) {
    using namespace ps2_pad_binding_report_detail;

    std::ostringstream out;
    out << "PAD_BINDINGS_V0 1\n";

    for (std::size_t index = 0; index < result.resolutions.size(); ++index) {
        const auto function = static_cast<PadBindingFunction>(index);
        const auto& source = result.resolutions[index];

        std::vector<PadBindingEvidence> evidence{};
        evidence.reserve(source.evidence.size());
        for (const auto& item : source.evidence) {
            if (item.function == function) {
                evidence.push_back(item);
            }
        }
        std::sort(evidence.begin(), evidence.end(), ps2_pad_binding_detail::evidence_less);

        out << "PAD_BINDING function=" << ps2_pad_binding_detail::function_name(function)
            << " confidence=" << confidence_name(source.confidence)
            << " pc=";
        if (source.guest_pc.has_value()) {
            out << format_pc(*source.guest_pc);
        } else {
            out << "none";
        }
        out << " evidence_count=" << evidence.size()
            << " max_score=" << max_score(evidence)
            << '\n';

        for (const auto& item : evidence) {
            out << "PAD_BINDING_EVIDENCE function="
                << ps2_pad_binding_detail::function_name(function)
                << " kind=" << evidence_kind_name(item.kind)
                << " pc=" << format_pc(item.guest_pc)
                << " score=" << item.score
                << " detail=" << item.detail
                << '\n';
        }
    }

    std::vector<std::string> diagnostics = result.diagnostics;
    std::sort(diagnostics.begin(), diagnostics.end());
    diagnostics.erase(std::unique(diagnostics.begin(), diagnostics.end()), diagnostics.end());
    for (const auto& diagnostic : diagnostics) {
        out << "PAD_BINDING_DIAGNOSTIC " << diagnostic << '\n';
    }

    out << "PAD_BINDINGS_END\n";
    return out.str();
}

} // namespace b3r::analysis