#include "analysis/ps2_pad_activation.h"
#include "analysis/ps2_pad_runtime_report.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "ps2_pad_runtime_report_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void test_complete_canonical_report() {
    using namespace b3r::analysis;

    PadRuntimeConfirmationResult result{};
    for (std::size_t index = 0; index < result.functions.size(); ++index) {
        result.functions[index].function = static_cast<PadBindingFunction>(index);
    }

    auto& init = result.functions[0];
    init.static_confidence = PadBindingConfidence::Trusted;
    init.runtime_status = PadRuntimeConfirmationStatus::Unobserved;
    init.pc_evidence.push_back({PadBindingFunction::PadInit, 0x00abcdefu});

    auto& open = result.functions[1];
    open.static_confidence = PadBindingConfidence::Candidate;
    open.runtime_status = PadRuntimeConfirmationStatus::ObservedIncompatible;
    open.calls_observed = 3u;
    open.incompatible_calls = 3u;
    open.pc_evidence.push_back(
        {PadBindingFunction::PadPortOpen, 0x00124500u, 3u, 0u, 3u});

    auto& state = result.functions[2];
    state.static_confidence = PadBindingConfidence::Candidate;
    state.runtime_status = PadRuntimeConfirmationStatus::RuntimeConfirmed;
    state.guest_pc = 0x001234abu;
    state.calls_observed = 12u;
    state.compatible_calls = 12u;
    state.pc_evidence.push_back(
        {PadBindingFunction::PadGetState, 0x001234abu, 12u, 12u, 0u});

    auto& read = result.functions[3];
    read.runtime_status = PadRuntimeConfirmationStatus::RuntimeAmbiguous;
    read.calls_observed = 17u;
    read.compatible_calls = 16u;
    read.incompatible_calls = 1u;
    read.pc_evidence.push_back(
        {PadBindingFunction::PadRead, 0x00126000u, 2u, 1u, 1u});
    read.pc_evidence.push_back(
        {PadBindingFunction::PadRead, 0x00124500u, 15u, 15u, 0u});

    auto& close = result.functions[4];
    close.pc_evidence.push_back({PadBindingFunction::PadPortClose, 0x0000000au});

    const std::string expected =
        "PAD_RUNTIME_CONFIRMATION_V0\n"
        "PAD_RUNTIME function=padInit static_confidence=trusted runtime_status=unobserved pc=none observed=0 compatible=0 incompatible=0\n"
        "PAD_RUNTIME_PC function=padInit pc=0x00abcdef observed=0 compatible=0 incompatible=0\n"
        "PAD_RUNTIME function=padPortOpen static_confidence=candidate runtime_status=observed_incompatible pc=none observed=3 compatible=0 incompatible=3\n"
        "PAD_RUNTIME_PC function=padPortOpen pc=0x00124500 observed=3 compatible=0 incompatible=3\n"
        "PAD_RUNTIME function=padGetState static_confidence=candidate runtime_status=runtime_confirmed pc=0x001234ab observed=12 compatible=12 incompatible=0\n"
        "PAD_RUNTIME_PC function=padGetState pc=0x001234ab observed=12 compatible=12 incompatible=0\n"
        "PAD_RUNTIME function=padRead static_confidence=unresolved runtime_status=runtime_ambiguous pc=none observed=17 compatible=16 incompatible=1\n"
        "PAD_RUNTIME_PC function=padRead pc=0x00124500 observed=15 compatible=15 incompatible=0\n"
        "PAD_RUNTIME_PC function=padRead pc=0x00126000 observed=2 compatible=1 incompatible=1\n"
        "PAD_RUNTIME function=padPortClose static_confidence=unresolved runtime_status=unobserved pc=none observed=0 compatible=0 incompatible=0\n"
        "PAD_RUNTIME_PC function=padPortClose pc=0x0000000a observed=0 compatible=0 incompatible=0\n"
        "PAD_RUNTIME function=padEnd static_confidence=unresolved runtime_status=unobserved pc=none observed=0 compatible=0 incompatible=0\n";

    const auto formatted = format_ps2_pad_runtime_confirmation(result);
    expect(formatted == expected, "complete report must match the canonical byte format");
    expect(format_ps2_pad_runtime_confirmation(result) == formatted,
           "formatting the same result twice must be byte-identical");
    expect(formatted.find("score=") == std::string::npos &&
               formatted.find("args=") == std::string::npos &&
               formatted.find("bytes=") == std::string::npos &&
               formatted.find("hle=") == std::string::npos,
           "report must exclude non-runtime confirmation details");
}

void test_nonconfirmed_status_never_exposes_pc() {
    using namespace b3r::analysis;

    PadRuntimeConfirmationResult result{};
    for (std::size_t index = 0; index < result.functions.size(); ++index) {
        result.functions[index].function = static_cast<PadBindingFunction>(index);
    }

    auto& read = result.functions[static_cast<std::size_t>(PadBindingFunction::PadRead)];
    read.runtime_status = PadRuntimeConfirmationStatus::RuntimeAmbiguous;
    read.guest_pc = 0x0012aaaau;

    const auto formatted = format_ps2_pad_runtime_confirmation(result);
    expect(formatted.find(
               "PAD_RUNTIME function=padRead static_confidence=unresolved runtime_status=runtime_ambiguous pc=none") !=
               std::string::npos,
           "non-confirmed runtime statuses must always render pc=none");
}

constexpr std::array<std::uint32_t, 6> kActivationPcs{
    0x00101000u,
    0x00102000u,
    0x00103000u,
    0x00104000u,
    0x00105000u,
    0x00106000u,
};

b3r::analysis::PadBindingConfidence activation_confidence(std::size_t index) {
    using b3r::analysis::PadBindingConfidence;
    if (index == 0u || index == 4u || index == 5u) {
        return PadBindingConfidence::Trusted;
    }
    if (index == 2u) {
        return PadBindingConfidence::Unresolved;
    }
    return PadBindingConfidence::Candidate;
}

b3r::analysis::PadBindingDiscoveryResult activation_discovery(
    const std::array<std::uint32_t, 6>& pcs) {
    using namespace b3r::analysis;
    PadBindingDiscoveryResult discovery{};
    for (std::size_t i = 0; i < discovery.resolutions.size(); ++i) {
        const auto function = static_cast<PadBindingFunction>(i);
        auto& resolution = discovery.resolutions[i];
        resolution.function = function;
        resolution.confidence = activation_confidence(i);
        resolution.evidence.push_back(PadBindingEvidence{
            function,
            i == 0u ? PadBindingEvidenceKind::ElfSymbol
                    : PadBindingEvidenceKind::StaticFingerprint,
            pcs[i],
            i == 0u ? 1000u : 100u,
            "synthetic-activation",
        });
    }
    return discovery;
}

b3r::analysis::PadRuntimeConfirmationResult activation_runtime(
    const std::array<std::uint32_t, 6>& pcs) {
    using namespace b3r::analysis;
    PadRuntimeConfirmationResult runtime{};
    for (std::size_t i = 0; i < runtime.functions.size(); ++i) {
        auto& result = runtime.functions[i];
        result.function = static_cast<PadBindingFunction>(i);
        result.static_confidence = activation_confidence(i);
        result.runtime_status = PadRuntimeConfirmationStatus::RuntimeConfirmed;
        result.guest_pc = pcs[i];
        result.calls_observed = 1u;
        result.compatible_calls = 1u;
    }
    return runtime;
}

std::size_t activation_index(b3r::analysis::PadBindingFunction function) {
    return static_cast<std::size_t>(function);
}

bool has_diagnostic(const b3r::analysis::Ps2PadActivationDecision& decision,
                    const std::string& text) {
    return std::find(decision.diagnostics.begin(), decision.diagnostics.end(), text) !=
           decision.diagnostics.end();
}

void test_pad_activation_ready_requires_six_evidence_backed_distinct_confirmations() {
    using namespace b3r::analysis;
    const auto discovery = activation_discovery(kActivationPcs);
    const auto runtime = activation_runtime(kActivationPcs);
    const auto decision = make_ps2_pad_activation_decision(discovery, runtime);

    expect(decision.readiness == PadActivationReadiness::Ready,
           "six consistent evidence-backed confirmed distinct PCs must be activation-ready");
    expect(decision.bindings.has_value(),
           "Ready activation must materialize complete bindings");
    expect(decision.bindings->pad_init == kActivationPcs[0] &&
               decision.bindings->pad_port_open == kActivationPcs[1] &&
               decision.bindings->pad_get_state == kActivationPcs[2] &&
               decision.bindings->pad_read == kActivationPcs[3] &&
               decision.bindings->pad_port_close == kActivationPcs[4] &&
               decision.bindings->pad_end == kActivationPcs[5],
           "bindings must exactly match the six confirmed evidence PCs");
}

void test_pad_activation_rejects_nonconfirmed_states() {
    using namespace b3r::analysis;
    const std::array<std::pair<PadRuntimeConfirmationStatus, PadActivationReason>, 3> cases{{
        {PadRuntimeConfirmationStatus::Unobserved, PadActivationReason::Unobserved},
        {PadRuntimeConfirmationStatus::ObservedIncompatible,
         PadActivationReason::ObservedIncompatible},
        {PadRuntimeConfirmationStatus::RuntimeAmbiguous,
         PadActivationReason::RuntimeAmbiguous},
    }};

    for (const auto& [status, reason] : cases) {
        const auto discovery = activation_discovery(kActivationPcs);
        auto runtime = activation_runtime(kActivationPcs);
        auto& read = runtime.functions[activation_index(PadBindingFunction::PadRead)];
        read.runtime_status = status;
        read.guest_pc = 0x00abcdefu;
        const auto decision = make_ps2_pad_activation_decision(discovery, runtime);
        const auto& output = decision.functions[activation_index(PadBindingFunction::PadRead)];
        expect(output.eligibility == PadActivationEligibility::Rejected &&
                   output.reason == reason && !output.guest_pc.has_value(),
               "non-confirmed runtime status must reject and clear PC");
        expect(decision.readiness == PadActivationReadiness::NotReady &&
                   !decision.bindings.has_value() &&
                   has_diagnostic(decision, "incomplete_activation_set"),
               "non-confirmed function must block atomic activation");
    }
}

void test_pad_activation_rejects_missing_and_zero_pc() {
    using namespace b3r::analysis;
    const auto discovery = activation_discovery(kActivationPcs);

    auto missing = activation_runtime(kActivationPcs);
    missing.functions[activation_index(PadBindingFunction::PadRead)].guest_pc.reset();
    const auto missing_decision = make_ps2_pad_activation_decision(discovery, missing);
    expect(missing_decision.functions[activation_index(PadBindingFunction::PadRead)].reason ==
               PadActivationReason::MissingGuestPc &&
               !missing_decision.bindings.has_value(),
           "RuntimeConfirmed with absent PC must reject atomically");

    auto zero = activation_runtime(kActivationPcs);
    zero.functions[activation_index(PadBindingFunction::PadRead)].guest_pc = 0u;
    const auto zero_decision = make_ps2_pad_activation_decision(discovery, zero);
    expect(zero_decision.functions[activation_index(PadBindingFunction::PadRead)].reason ==
               PadActivationReason::ZeroGuestPc &&
               !zero_decision.bindings.has_value(),
           "RuntimeConfirmed with zero PC must reject atomically");
}

void test_pad_activation_accepts_all_static_confidence_levels() {
    using namespace b3r::analysis;
    const auto decision = make_ps2_pad_activation_decision(
        activation_discovery(kActivationPcs), activation_runtime(kActivationPcs));
    for (std::size_t i = 0; i < decision.functions.size(); ++i) {
        expect(decision.functions[i].eligibility == PadActivationEligibility::Eligible &&
                   decision.functions[i].static_confidence == activation_confidence(i),
               "Trusted, Candidate and Unresolved confidence must all remain eligible when confirmed");
    }
}

void test_pad_activation_rejects_input_mismatch() {
    using namespace b3r::analysis;

    auto discovery_mismatch = activation_discovery(kActivationPcs);
    const auto runtime_ok = activation_runtime(kActivationPcs);
    discovery_mismatch.resolutions[0].function = PadBindingFunction::PadEnd;
    const auto discovery_decision =
        make_ps2_pad_activation_decision(discovery_mismatch, runtime_ok);
    expect(discovery_decision.functions[0].reason == PadActivationReason::InputMismatch &&
               !discovery_decision.functions[0].guest_pc.has_value() &&
               has_diagnostic(discovery_decision,
                              "input_mismatch function=padInit field=discovery_function"),
           "discovery function mismatch must reject deterministically");

    const auto discovery_ok = activation_discovery(kActivationPcs);
    auto runtime_function_mismatch = activation_runtime(kActivationPcs);
    runtime_function_mismatch.functions[1].function = PadBindingFunction::PadEnd;
    const auto runtime_function_decision =
        make_ps2_pad_activation_decision(discovery_ok, runtime_function_mismatch);
    expect(runtime_function_decision.functions[1].reason == PadActivationReason::InputMismatch &&
               has_diagnostic(runtime_function_decision,
                              "input_mismatch function=padPortOpen field=runtime_function"),
           "runtime function mismatch must reject deterministically");

    auto runtime_confidence_mismatch = activation_runtime(kActivationPcs);
    runtime_confidence_mismatch.functions[2].static_confidence = PadBindingConfidence::Trusted;
    const auto confidence_decision =
        make_ps2_pad_activation_decision(discovery_ok, runtime_confidence_mismatch);
    expect(confidence_decision.functions[2].reason == PadActivationReason::InputMismatch &&
               has_diagnostic(confidence_decision,
                              "input_mismatch function=padGetState field=static_confidence"),
           "runtime/discovery confidence mismatch must reject deterministically");
}

void test_pad_activation_requires_same_function_discovery_evidence() {
    using namespace b3r::analysis;
    auto discovery = activation_discovery(kActivationPcs);
    const auto runtime = activation_runtime(kActivationPcs);
    auto& read_evidence = discovery.resolutions[activation_index(PadBindingFunction::PadRead)].evidence;
    read_evidence.front().function = PadBindingFunction::PadEnd;

    const auto decision = make_ps2_pad_activation_decision(discovery, runtime);
    const auto& read = decision.functions[activation_index(PadBindingFunction::PadRead)];
    expect(read.eligibility == PadActivationEligibility::Rejected &&
               read.reason == PadActivationReason::PcNotInDiscoveryEvidence &&
               !read.guest_pc.has_value(),
           "same numerical PC under another function must not satisfy activation provenance");
    expect(has_diagnostic(decision,
                          "pc_not_in_discovery_evidence function=padRead pc=0x00104000"),
           "missing same-function provenance must be diagnosed with canonical PC");
}

void test_pad_activation_duplicate_pc_blocks_global_readiness() {
    using namespace b3r::analysis;
    auto duplicate_pcs = kActivationPcs;
    duplicate_pcs[1] = duplicate_pcs[0];
    const auto duplicate = make_ps2_pad_activation_decision(
        activation_discovery(duplicate_pcs), activation_runtime(duplicate_pcs));
    expect(duplicate.functions[0].eligibility == PadActivationEligibility::Eligible &&
               duplicate.functions[1].eligibility == PadActivationEligibility::Eligible,
           "duplicate PC functions remain individually eligible");
    expect(duplicate.readiness == PadActivationReadiness::NotReady &&
               !duplicate.bindings.has_value(),
           "duplicate PCs must block global activation");
    expect(has_diagnostic(duplicate,
                          "activation guest PC 0x00101000 is selected by multiple PAD functions"),
           "duplicate PC group must emit deterministic diagnostic");

    auto triple_pcs = kActivationPcs;
    triple_pcs[1] = triple_pcs[0];
    triple_pcs[2] = triple_pcs[0];
    const auto triple = make_ps2_pad_activation_decision(
        activation_discovery(triple_pcs), activation_runtime(triple_pcs));
    const auto duplicate_count = std::count(
        triple.diagnostics.begin(), triple.diagnostics.end(),
        "activation guest PC 0x00101000 is selected by multiple PAD functions");
    expect(duplicate_count == 1,
           "one duplicated numerical PC group must emit exactly one diagnostic");
}

void test_pad_activation_incomplete_set_never_materializes_partial_bindings() {
    using namespace b3r::analysis;
    const auto discovery = activation_discovery(kActivationPcs);
    auto runtime = activation_runtime(kActivationPcs);
    auto& end = runtime.functions[activation_index(PadBindingFunction::PadEnd)];
    end.runtime_status = PadRuntimeConfirmationStatus::Unobserved;
    end.guest_pc.reset();
    const auto decision = make_ps2_pad_activation_decision(discovery, runtime);
    expect(decision.readiness == PadActivationReadiness::NotReady &&
               !decision.bindings.has_value(),
           "5/6 eligible functions must never materialize partial HLE bindings");
    expect(has_diagnostic(decision, "incomplete_activation_set"),
           "incomplete atomic set must be diagnosed");
}

void test_pad_activation_is_pure_and_deterministic() {
    using namespace b3r::analysis;
    auto discovery = activation_discovery(kActivationPcs);
    auto runtime = activation_runtime(kActivationPcs);
    const auto discovery_before = discovery;
    const auto runtime_before = runtime;

    const auto first = make_ps2_pad_activation_decision(discovery, runtime);
    const auto second = make_ps2_pad_activation_decision(discovery, runtime);

    expect(first.readiness == second.readiness &&
               first.diagnostics == second.diagnostics &&
               first.bindings.has_value() == second.bindings.has_value(),
           "identical activation inputs must produce structurally identical global decisions");
    for (std::size_t i = 0; i < first.functions.size(); ++i) {
        const auto& lhs = first.functions[i];
        const auto& rhs = second.functions[i];
        expect(lhs.function == rhs.function &&
                   lhs.static_confidence == rhs.static_confidence &&
                   lhs.runtime_status == rhs.runtime_status &&
                   lhs.eligibility == rhs.eligibility &&
                   lhs.reason == rhs.reason &&
                   lhs.guest_pc == rhs.guest_pc,
               "identical activation inputs must produce identical per-function decisions");

        expect(discovery.resolutions[i].function == discovery_before.resolutions[i].function &&
                   discovery.resolutions[i].confidence == discovery_before.resolutions[i].confidence &&
                   discovery.resolutions[i].guest_pc == discovery_before.resolutions[i].guest_pc &&
                   discovery.resolutions[i].evidence.size() == discovery_before.resolutions[i].evidence.size(),
               "activation decision must not mutate discovery input");
        expect(runtime.functions[i].function == runtime_before.functions[i].function &&
                   runtime.functions[i].static_confidence == runtime_before.functions[i].static_confidence &&
                   runtime.functions[i].runtime_status == runtime_before.functions[i].runtime_status &&
                   runtime.functions[i].guest_pc == runtime_before.functions[i].guest_pc &&
                   runtime.functions[i].calls_observed == runtime_before.functions[i].calls_observed &&
                   runtime.functions[i].compatible_calls == runtime_before.functions[i].compatible_calls &&
                   runtime.functions[i].incompatible_calls == runtime_before.functions[i].incompatible_calls,
               "activation decision must not mutate runtime input");
    }
    if (first.bindings.has_value() && second.bindings.has_value()) {
        expect(first.bindings->pad_init == second.bindings->pad_init &&
                   first.bindings->pad_port_open == second.bindings->pad_port_open &&
                   first.bindings->pad_get_state == second.bindings->pad_get_state &&
                   first.bindings->pad_read == second.bindings->pad_read &&
                   first.bindings->pad_port_close == second.bindings->pad_port_close &&
                   first.bindings->pad_end == second.bindings->pad_end,
               "identical activation inputs must produce identical complete bindings");
    }
}

} // namespace

int main() {
    test_complete_canonical_report();
    test_nonconfirmed_status_never_exposes_pc();
    test_pad_activation_ready_requires_six_evidence_backed_distinct_confirmations();
    test_pad_activation_rejects_nonconfirmed_states();
    test_pad_activation_rejects_missing_and_zero_pc();
    test_pad_activation_accepts_all_static_confidence_levels();
    test_pad_activation_rejects_input_mismatch();
    test_pad_activation_requires_same_function_discovery_evidence();
    test_pad_activation_duplicate_pc_blocks_global_readiness();
    test_pad_activation_incomplete_set_never_materializes_partial_bindings();
    test_pad_activation_is_pure_and_deterministic();
    std::cout << "ps2_pad_runtime_report_tests: PASS\n";
    return EXIT_SUCCESS;
}
