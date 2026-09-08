#include "analysis/ps2_pad_activation_report.h"
#include "tools/burnout3_analyze_options.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "burnout3_analyze_options_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

b3r::tools::Burnout3AnalyzeOptionsResult parse(std::initializer_list<std::string_view> args) {
    const std::vector<std::string_view> values(args);
    return b3r::tools::parse_burnout3_analyze_options(values);
}

constexpr std::array<std::uint32_t, 6> kReportActivationPcs{
    0x00101000u,
    0x00102000u,
    0x00103000u,
    0x00104000u,
    0x00105000u,
    0x00106000u,
};

b3r::analysis::PadBindingConfidence report_activation_confidence(std::size_t index) {
    using b3r::analysis::PadBindingConfidence;
    if (index == 0u || index == 4u || index == 5u) {
        return PadBindingConfidence::Trusted;
    }
    if (index == 2u) {
        return PadBindingConfidence::Unresolved;
    }
    return PadBindingConfidence::Candidate;
}

b3r::analysis::PadBindingDiscoveryResult report_activation_discovery() {
    using namespace b3r::analysis;
    PadBindingDiscoveryResult discovery{};
    for (std::size_t i = 0; i < discovery.resolutions.size(); ++i) {
        const auto function = static_cast<PadBindingFunction>(i);
        auto& resolution = discovery.resolutions[i];
        resolution.function = function;
        resolution.confidence = report_activation_confidence(i);
        resolution.evidence.push_back({
            function,
            i == 0u ? PadBindingEvidenceKind::ElfSymbol
                    : PadBindingEvidenceKind::StaticFingerprint,
            kReportActivationPcs[i],
            i == 0u ? 1000u : 100u,
            "synthetic-activation-report",
        });
    }
    return discovery;
}

b3r::analysis::PadRuntimeConfirmationResult report_activation_runtime() {
    using namespace b3r::analysis;
    PadRuntimeConfirmationResult runtime{};
    for (std::size_t i = 0; i < runtime.functions.size(); ++i) {
        auto& item = runtime.functions[i];
        item.function = static_cast<PadBindingFunction>(i);
        item.static_confidence = report_activation_confidence(i);
        item.runtime_status = PadRuntimeConfirmationStatus::RuntimeConfirmed;
        item.guest_pc = kReportActivationPcs[i];
        item.calls_observed = 1u;
        item.compatible_calls = 1u;
    }
    return runtime;
}

b3r::analysis::Ps2PadActivationDecision ready_activation_decision() {
    return b3r::analysis::make_ps2_pad_activation_decision(
        report_activation_discovery(), report_activation_runtime());
}

std::size_t count_occurrences(const std::string& text, std::string_view needle) {
    std::size_t count{};
    std::size_t position{};
    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

void test_pad_activation_ready_report() {
    using namespace b3r::analysis;
    const auto decision = ready_activation_decision();
    const std::string expected =
        "PAD_ACTIVATION_V0 readiness=ready eligible=6 required=6\n"
        "PAD_ACTIVATION function=padInit static_confidence=trusted runtime_status=runtime_confirmed eligibility=eligible pc=0x00101000 reason=eligible_runtime_confirmed\n"
        "PAD_ACTIVATION function=padPortOpen static_confidence=candidate runtime_status=runtime_confirmed eligibility=eligible pc=0x00102000 reason=eligible_runtime_confirmed\n"
        "PAD_ACTIVATION function=padGetState static_confidence=unresolved runtime_status=runtime_confirmed eligibility=eligible pc=0x00103000 reason=eligible_runtime_confirmed\n"
        "PAD_ACTIVATION function=padRead static_confidence=candidate runtime_status=runtime_confirmed eligibility=eligible pc=0x00104000 reason=eligible_runtime_confirmed\n"
        "PAD_ACTIVATION function=padPortClose static_confidence=trusted runtime_status=runtime_confirmed eligibility=eligible pc=0x00105000 reason=eligible_runtime_confirmed\n"
        "PAD_ACTIVATION function=padEnd static_confidence=trusted runtime_status=runtime_confirmed eligibility=eligible pc=0x00106000 reason=eligible_runtime_confirmed\n"
        "PAD_ACTIVATION_BINDINGS padInit=0x00101000 padPortOpen=0x00102000 padGetState=0x00103000 padRead=0x00104000 padPortClose=0x00105000 padEnd=0x00106000\n"
        "PAD_ACTIVATION_END\n";

    const auto formatted = format_ps2_pad_activation_decision(decision);
    expect(formatted == expected,
           "ready activation report must match the canonical byte format");
    expect(format_ps2_pad_activation_decision(decision) == formatted,
           "activation report must be byte-identical on repeated render");
}

void test_pad_activation_report_defends_against_malformed_decisions() {
    using namespace b3r::analysis;

    auto rejected = ready_activation_decision();
    auto& read = rejected.functions[static_cast<std::size_t>(PadBindingFunction::PadRead)];
    read.eligibility = PadActivationEligibility::Rejected;
    read.reason = PadActivationReason::InputMismatch;
    read.guest_pc = 0x00abcdefu;
    rejected.readiness = PadActivationReadiness::NotReady;
    rejected.diagnostics = {"zeta", "alpha", "zeta"};
    const auto rejected_text = format_ps2_pad_activation_decision(rejected);
    expect(rejected_text.find(
               "PAD_ACTIVATION function=padRead static_confidence=candidate runtime_status=runtime_confirmed eligibility=rejected pc=none reason=input_mismatch") !=
               std::string::npos,
           "rejected function must render pc=none even when copied decision carries a PC");
    expect(rejected_text.find("PAD_ACTIVATION_BINDINGS") == std::string::npos,
           "NotReady decision must never render bindings even when bindings are present");
    expect(rejected_text.find("PAD_ACTIVATION_DIAGNOSTIC alpha\n") <
               rejected_text.find("PAD_ACTIVATION_DIAGNOSTIC zeta\n") &&
               count_occurrences(rejected_text, "PAD_ACTIVATION_DIAGNOSTIC zeta\n") == 1u,
           "activation diagnostics must be sorted and deduplicated defensively");

    auto missing_bindings = ready_activation_decision();
    missing_bindings.bindings.reset();
    const auto missing_bindings_text =
        format_ps2_pad_activation_decision(missing_bindings);
    expect(missing_bindings_text.find("PAD_ACTIVATION_BINDINGS") == std::string::npos,
           "Ready decision without bindings must not fabricate a bindings line");

    auto provenance = ready_activation_decision();
    auto& open = provenance.functions[static_cast<std::size_t>(PadBindingFunction::PadPortOpen)];
    open.eligibility = PadActivationEligibility::Rejected;
    open.reason = PadActivationReason::PcNotInDiscoveryEvidence;
    provenance.readiness = PadActivationReadiness::NotReady;
    const auto provenance_text = format_ps2_pad_activation_decision(provenance);
    expect(provenance_text.find("reason=pc_not_in_discovery_evidence") != std::string::npos,
           "provenance rejection reason must use canonical report spelling");
}

void test_pad_activation_report_structure_and_exclusions() {
    using namespace b3r::analysis;
    const auto text = format_ps2_pad_activation_decision(ready_activation_decision());
    expect(count_occurrences(text, "PAD_ACTIVATION function=") == 6u,
           "activation report must contain exactly six canonical function lines");

    const auto init = text.find("function=padInit ");
    const auto open = text.find("function=padPortOpen ");
    const auto state = text.find("function=padGetState ");
    const auto read = text.find("function=padRead ");
    const auto close = text.find("function=padPortClose ");
    const auto end = text.find("function=padEnd ");
    expect(init < open && open < state && state < read && read < close && close < end,
           "activation function lines must remain in canonical enum order");

    constexpr std::array<std::string_view, 6> forbidden{
        "score=", "args=", "bytes=", "ram=", "code=", "hash="};
    for (const auto token : forbidden) {
        expect(text.find(token) == std::string::npos,
               "activation report must exclude non-policy diagnostic payloads");
    }
    expect(format_ps2_pad_activation_decision(ready_activation_decision()) == text,
           "canonical activation rendering must remain byte-identical");
}

} // namespace

int main() {
    using namespace b3r::tools;

    {
        const auto result = parse({"--elf", "SLUS_210.50", "--output", "analysis.txt", "--max-blocks", "8192", "--follow-direct-calls"});
        expect(result.ok(), "complete CLI options must parse");
        expect(result.options->elf_path == "SLUS_210.50", "ELF path must be retained");
        expect(result.options->output_path.has_value() && *result.options->output_path == "analysis.txt",
               "output path must be retained");
        expect(result.options->max_blocks == 8192u, "max block limit must parse as a positive integer");
        expect(result.options->follow_direct_calls, "--follow-direct-calls must enable direct callee traversal");
        expect(!result.options->pad_bindings, "PAD discovery must remain disabled unless explicitly requested");
        expect(!result.options->show_help, "normal invocation must not request help");
    }

    {
        const auto result = parse({"--elf", "game.elf"});
        expect(result.ok(), "ELF-only invocation must use defaults");
        expect(result.options->max_blocks == 4096u, "default max block limit must remain bounded");
        expect(!result.options->output_path.has_value(), "output must default to stdout");
        expect(!result.options->follow_direct_calls, "direct call traversal must remain disabled by default");
        expect(!result.options->pad_bindings, "PAD binding discovery must default off");
    }

    {
        const auto result = parse({"--elf", "game.elf", "--pad-bindings"});
        expect(result.ok(), "--pad-bindings must be accepted as a value-less option");
        expect(result.options->pad_bindings,
               "--pad-bindings must enable opt-in PAD binding discovery");
    }

    {
        const auto result = parse({"--elf", "game.elf", "--pad-bindings", "--pad-bindings"});
        expect(!result.ok(), "duplicate --pad-bindings must fail");
        expect(result.error == Burnout3AnalyzeOptionError::DuplicateOption,
               "duplicate --pad-bindings must use DuplicateOption");
    }

    {
        const std::string usage = burnout3_analyze_usage();
        expect(usage.find("[--pad-bindings]") != std::string::npos,
               "usage must advertise the opt-in --pad-bindings flag");
    }

    {
        const auto result = parse({"--help"});
        expect(result.ok(), "help must not require an ELF path");
        expect(result.options->show_help, "help flag must be preserved");
    }

    {
        const auto result = parse({"--output", "analysis.txt"});
        expect(!result.ok(), "normal invocation without --elf must fail");
        expect(result.error == Burnout3AnalyzeOptionError::MissingElfPath,
               "missing ELF path must have a specific error");
    }

    {
        const auto result = parse({"--elf"});
        expect(!result.ok(), "option without a value must fail");
        expect(result.error == Burnout3AnalyzeOptionError::MissingValue,
               "missing option values must have a specific error");
    }

    {
        const auto result = parse({"--elf", "game.elf", "--max-blocks", "0"});
        expect(!result.ok(), "zero block limit must fail");
        expect(result.error == Burnout3AnalyzeOptionError::InvalidMaxBlocks,
               "invalid block limit must have a specific error");
    }

    {
        const auto result = parse({"--elf", "game.elf", "--wat"});
        expect(!result.ok(), "unknown options must fail");
        expect(result.error == Burnout3AnalyzeOptionError::UnknownOption,
               "unknown option must have a specific error");
    }

    test_pad_activation_ready_report();
    test_pad_activation_report_defends_against_malformed_decisions();
    test_pad_activation_report_structure_and_exclusions();

    std::cout << "burnout3_analyze_options_tests: PASS\n";
    return EXIT_SUCCESS;
}
