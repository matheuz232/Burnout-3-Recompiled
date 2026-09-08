#pragma once

#include "analysis/ps2_pad_binding_report.h"
#include "analysis/ps2_pad_runtime_confirmation.h"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

namespace b3r::analysis {
namespace ps2_pad_runtime_report_detail {

[[nodiscard]] inline const char* status_name(
    PadRuntimeConfirmationStatus status) noexcept {
    switch (status) {
    case PadRuntimeConfirmationStatus::Unobserved:
        return "unobserved";
    case PadRuntimeConfirmationStatus::ObservedIncompatible:
        return "observed_incompatible";
    case PadRuntimeConfirmationStatus::RuntimeConfirmed:
        return "runtime_confirmed";
    case PadRuntimeConfirmationStatus::RuntimeAmbiguous:
        return "runtime_ambiguous";
    }
    return "unknown";
}

} // namespace ps2_pad_runtime_report_detail

[[nodiscard]] inline std::string format_ps2_pad_runtime_confirmation(
    const PadRuntimeConfirmationResult& result) {
    using ps2_pad_binding_report_detail::confidence_name;
    using ps2_pad_binding_report_detail::format_pc;
    using ps2_pad_runtime_report_detail::status_name;

    std::ostringstream out;
    out << "PAD_RUNTIME_CONFIRMATION_V0\n";

    for (std::size_t index = 0; index < result.functions.size(); ++index) {
        const auto function = static_cast<PadBindingFunction>(index);
        const auto& source = result.functions[index];

        out << "PAD_RUNTIME function="
            << ps2_pad_binding_detail::function_name(function)
            << " static_confidence=" << confidence_name(source.static_confidence)
            << " runtime_status=" << status_name(source.runtime_status)
            << " pc=";
        if (source.guest_pc.has_value()) {
            out << format_pc(*source.guest_pc);
        } else {
            out << "none";
        }
        out << " observed=" << source.calls_observed
            << " compatible=" << source.compatible_calls
            << " incompatible=" << source.incompatible_calls
            << '\n';

        std::vector<Ps2PadRuntimePcEvidence> pc_evidence = source.pc_evidence;
        std::sort(pc_evidence.begin(), pc_evidence.end(),
                  [](const Ps2PadRuntimePcEvidence& lhs,
                     const Ps2PadRuntimePcEvidence& rhs) {
                      return lhs.guest_pc < rhs.guest_pc;
                  });

        for (const auto& pc : pc_evidence) {
            out << "PAD_RUNTIME_PC function="
                << ps2_pad_binding_detail::function_name(function)
                << " pc=" << format_pc(pc.guest_pc)
                << " observed=" << pc.calls_observed
                << " compatible=" << pc.compatible_calls
                << " incompatible=" << pc.incompatible_calls
                << '\n';
        }
    }

    return out.str();
}

} // namespace b3r::analysis
