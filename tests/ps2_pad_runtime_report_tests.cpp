#include "analysis/ps2_pad_activation.h"
#include "analysis/ps2_pad_runtime_report.h"

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

} // namespace

int main() {
    test_complete_canonical_report();
    test_nonconfirmed_status_never_exposes_pc();
    test_pad_activation_ready_requires_six_evidence_backed_distinct_confirmations();
    std::cout << "ps2_pad_runtime_report_tests: PASS\n";
    return EXIT_SUCCESS;
}
