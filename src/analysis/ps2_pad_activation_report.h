#pragma once

#include "analysis/ps2_pad_activation.h"
#include "analysis/ps2_pad_binding_report.h"
#include "analysis/ps2_pad_runtime_report.h"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

namespace b3r::analysis {
namespace ps2_pad_activation_report_detail {

[[nodiscard]] inline const char* eligibility_name(
    PadActivationEligibility eligibility) noexcept {
    switch (eligibility) {
    case PadActivationEligibility::Rejected:
        return "rejected";
    case PadActivationEligibility::Eligible:
        return "eligible";
    }
    return "unknown";
}

[[nodiscard]] inline const char* readiness_name(
    PadActivationReadiness readiness) noexcept {
    switch (readiness) {
    case PadActivationReadiness::NotReady:
        return "not_ready";
    case PadActivationReadiness::Ready:
        return "ready";
    }
    return "unknown";
}

[[nodiscard]] inline const char* reason_name(
    PadActivationReason reason) noexcept {
    switch (reason) {
    case PadActivationReason::EligibleRuntimeConfirmed:
        return "eligible_runtime_confirmed";
    case PadActivationReason::Unobserved:
        return "unobserved";
    case PadActivationReason::ObservedIncompatible:
        return "observed_incompatible";
    case PadActivationReason::RuntimeAmbiguous:
        return "runtime_ambiguous";
    case PadActivationReason::MissingGuestPc:
        return "missing_guest_pc";
    case PadActivationReason::ZeroGuestPc:
        return "zero_guest_pc";
    case PadActivationReason::InputMismatch:
        return "input_mismatch";
    case PadActivationReason::PcNotInDiscoveryEvidence:
        return "pc_not_in_discovery_evidence";
    }
    return "unknown";
}

} // namespace ps2_pad_activation_report_detail

[[nodiscard]] inline std::string format_ps2_pad_activation_decision(
    const Ps2PadActivationDecision& decision) {
    using ps2_pad_activation_report_detail::eligibility_name;
    using ps2_pad_activation_report_detail::readiness_name;
    using ps2_pad_activation_report_detail::reason_name;
    using ps2_pad_binding_report_detail::confidence_name;
    using ps2_pad_binding_report_detail::format_pc;
    using ps2_pad_runtime_report_detail::status_name;

    const auto eligible_count = std::count_if(
        decision.functions.begin(), decision.functions.end(),
        [](const PadActivationFunctionDecision& item) {
            return item.eligibility == PadActivationEligibility::Eligible;
        });

    std::ostringstream out;
    out << "PAD_ACTIVATION_V0 readiness=" << readiness_name(decision.readiness)
        << " eligible=" << eligible_count << " required=6\n";

    for (std::size_t index = 0; index < decision.functions.size(); ++index) {
        const auto function = static_cast<PadBindingFunction>(index);
        const auto& source = decision.functions[index];
        out << "PAD_ACTIVATION function="
            << ps2_pad_binding_detail::function_name(function)
            << " static_confidence=" << confidence_name(source.static_confidence)
            << " runtime_status=" << status_name(source.runtime_status)
            << " eligibility=" << eligibility_name(source.eligibility)
            << " pc=";
        if (source.eligibility == PadActivationEligibility::Eligible &&
            source.guest_pc.has_value()) {
            out << format_pc(*source.guest_pc);
        } else {
            out << "none";
        }
        out << " reason=" << reason_name(source.reason) << '\n';
    }

    if (decision.readiness == PadActivationReadiness::Ready &&
        decision.bindings.has_value()) {
        const auto& bindings = *decision.bindings;
        out << "PAD_ACTIVATION_BINDINGS"
            << " padInit=" << format_pc(bindings.pad_init)
            << " padPortOpen=" << format_pc(bindings.pad_port_open)
            << " padGetState=" << format_pc(bindings.pad_get_state)
            << " padRead=" << format_pc(bindings.pad_read)
            << " padPortClose=" << format_pc(bindings.pad_port_close)
            << " padEnd=" << format_pc(bindings.pad_end)
            << '\n';
    }

    std::vector<std::string> diagnostics = decision.diagnostics;
    std::sort(diagnostics.begin(), diagnostics.end());
    diagnostics.erase(std::unique(diagnostics.begin(), diagnostics.end()),
                      diagnostics.end());
    for (const auto& diagnostic : diagnostics) {
        out << "PAD_ACTIVATION_DIAGNOSTIC " << diagnostic << '\n';
    }

    out << "PAD_ACTIVATION_END\n";
    return out.str();
}

} // namespace b3r::analysis
