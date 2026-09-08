#pragma once

#include "analysis/ps2_pad_binding_discovery.h"
#include "analysis/ps2_pad_runtime_confirmation.h"
#include "runtime/ps2_pad_hle_service.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace b3r::analysis {

enum class PadActivationEligibility : std::uint8_t {
    Rejected,
    Eligible,
};

enum class PadActivationReadiness : std::uint8_t {
    NotReady,
    Ready,
};

enum class PadActivationReason : std::uint8_t {
    EligibleRuntimeConfirmed,
    Unobserved,
    ObservedIncompatible,
    RuntimeAmbiguous,
    MissingGuestPc,
    ZeroGuestPc,
    InputMismatch,
    PcNotInDiscoveryEvidence,
};

struct PadActivationFunctionDecision {
    PadBindingFunction function{};
    PadBindingConfidence static_confidence{PadBindingConfidence::Unresolved};
    PadRuntimeConfirmationStatus runtime_status{
        PadRuntimeConfirmationStatus::Unobserved};
    PadActivationEligibility eligibility{PadActivationEligibility::Rejected};
    PadActivationReason reason{PadActivationReason::Unobserved};
    std::optional<std::uint32_t> guest_pc{};
};

struct Ps2PadActivationDecision {
    std::array<PadActivationFunctionDecision, 6> functions{};
    PadActivationReadiness readiness{PadActivationReadiness::NotReady};
    std::optional<runtime::Ps2PadHleBindings> bindings{};
    std::vector<std::string> diagnostics{};
};

namespace ps2_pad_activation_detail {

[[nodiscard]] inline std::string format_activation_pc(std::uint32_t pc) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << pc;
    return out.str();
}

inline void normalize_diagnostics(std::vector<std::string>& diagnostics) {
    std::sort(diagnostics.begin(), diagnostics.end());
    diagnostics.erase(std::unique(diagnostics.begin(), diagnostics.end()),
                      diagnostics.end());
}

} // namespace ps2_pad_activation_detail

[[nodiscard]] inline Ps2PadActivationDecision
make_ps2_pad_activation_decision(
    const PadBindingDiscoveryResult& discovery,
    const PadRuntimeConfirmationResult& runtime_result) {
    Ps2PadActivationDecision decision{};

    for (std::size_t i = 0; i < decision.functions.size(); ++i) {
        const auto expected = static_cast<PadBindingFunction>(i);
        const auto& discovery_source = discovery.resolutions[i];
        const auto& runtime_source = runtime_result.functions[i];
        auto& output = decision.functions[i];

        output.function = expected;
        output.runtime_status = runtime_source.runtime_status;

        bool input_mismatch = false;
        if (discovery_source.function != expected) {
            input_mismatch = true;
            decision.diagnostics.push_back(
                std::string("input_mismatch function=") +
                ps2_pad_binding_detail::function_name(expected) +
                " field=discovery_function");
        }
        if (runtime_source.function != expected) {
            input_mismatch = true;
            decision.diagnostics.push_back(
                std::string("input_mismatch function=") +
                ps2_pad_binding_detail::function_name(expected) +
                " field=runtime_function");
        }
        if (runtime_source.static_confidence != discovery_source.confidence) {
            input_mismatch = true;
            decision.diagnostics.push_back(
                std::string("input_mismatch function=") +
                ps2_pad_binding_detail::function_name(expected) +
                " field=static_confidence");
        }
        if (input_mismatch) {
            output.reason = PadActivationReason::InputMismatch;
            output.guest_pc.reset();
            continue;
        }

        output.static_confidence = discovery_source.confidence;

        switch (runtime_source.runtime_status) {
        case PadRuntimeConfirmationStatus::Unobserved:
            output.reason = PadActivationReason::Unobserved;
            continue;
        case PadRuntimeConfirmationStatus::ObservedIncompatible:
            output.reason = PadActivationReason::ObservedIncompatible;
            continue;
        case PadRuntimeConfirmationStatus::RuntimeAmbiguous:
            output.reason = PadActivationReason::RuntimeAmbiguous;
            continue;
        case PadRuntimeConfirmationStatus::RuntimeConfirmed:
            break;
        }

        if (!runtime_source.guest_pc.has_value()) {
            output.reason = PadActivationReason::MissingGuestPc;
            continue;
        }
        if (*runtime_source.guest_pc == 0u) {
            output.reason = PadActivationReason::ZeroGuestPc;
            continue;
        }

        const auto selected_pc = *runtime_source.guest_pc;
        const bool evidence_backed = std::any_of(
            discovery_source.evidence.begin(), discovery_source.evidence.end(),
            [&](const PadBindingEvidence& evidence) {
                return evidence.function == expected &&
                       evidence.guest_pc == selected_pc;
            });
        if (!evidence_backed) {
            output.reason = PadActivationReason::PcNotInDiscoveryEvidence;
            output.guest_pc.reset();
            decision.diagnostics.push_back(
                std::string("pc_not_in_discovery_evidence function=") +
                ps2_pad_binding_detail::function_name(expected) +
                " pc=" +
                ps2_pad_activation_detail::format_activation_pc(selected_pc));
            continue;
        }

        output.eligibility = PadActivationEligibility::Eligible;
        output.reason = PadActivationReason::EligibleRuntimeConfirmed;
        output.guest_pc = selected_pc;
    }

    const auto eligible_count = std::count_if(
        decision.functions.begin(), decision.functions.end(),
        [](const PadActivationFunctionDecision& item) {
            return item.eligibility == PadActivationEligibility::Eligible;
        });

    if (eligible_count != decision.functions.size()) {
        decision.diagnostics.push_back("incomplete_activation_set");
        ps2_pad_activation_detail::normalize_diagnostics(decision.diagnostics);
        return decision;
    }

    std::array<std::uint32_t, 6> selected_pcs{};
    for (std::size_t i = 0; i < selected_pcs.size(); ++i) {
        selected_pcs[i] = decision.functions[i].guest_pc.value();
    }

    auto sorted_pcs = selected_pcs;
    std::sort(sorted_pcs.begin(), sorted_pcs.end());
    bool duplicate_found = false;
    for (std::size_t i = 0; i < sorted_pcs.size();) {
        std::size_t next = i + 1u;
        while (next < sorted_pcs.size() && sorted_pcs[next] == sorted_pcs[i]) {
            ++next;
        }
        if (next - i >= 2u) {
            duplicate_found = true;
            decision.diagnostics.push_back(
                std::string("activation guest PC ") +
                ps2_pad_activation_detail::format_activation_pc(sorted_pcs[i]) +
                " is selected by multiple PAD functions");
        }
        i = next;
    }

    if (duplicate_found) {
        ps2_pad_activation_detail::normalize_diagnostics(decision.diagnostics);
        return decision;
    }

    decision.readiness = PadActivationReadiness::Ready;
    decision.bindings = runtime::Ps2PadHleBindings{
        selected_pcs[0],
        selected_pcs[1],
        selected_pcs[2],
        selected_pcs[3],
        selected_pcs[4],
        selected_pcs[5],
    };
    ps2_pad_activation_detail::normalize_diagnostics(decision.diagnostics);
    return decision;
}

} // namespace b3r::analysis
