#include "analysis/ps2_pad_binding_report.h"
#include "analysis/ps2_pad_runtime_confirmation.h"
#include "analysis/r5900_analysis_report.h"
#include "recompiler/ps2_elf.h"
#include "runtime/ps2_memory_map.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_analysis_report_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset + 0u] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0u] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

b3r::runtime::Ps2MemoryMap make_pad_runtime_memory() {
    constexpr std::uint32_t phoff = 52u;
    constexpr std::uint32_t payload_offset = 0x100u;
    constexpr std::uint32_t base = 0x00100000u;
    Bytes bytes(payload_offset + 4u, 0u);
    bytes[0] = 0x7fu; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
    bytes[4] = 1u; bytes[5] = 1u; bytes[6] = 1u;
    put_u16(bytes, 16u, 2u);
    put_u16(bytes, 18u, 8u);
    put_u32(bytes, 20u, 1u);
    put_u32(bytes, 24u, base);
    put_u32(bytes, 28u, phoff);
    put_u16(bytes, 40u, 52u);
    put_u16(bytes, 42u, 32u);
    put_u16(bytes, 44u, 1u);
    put_u32(bytes, phoff + 0u, 1u);
    put_u32(bytes, phoff + 4u, payload_offset);
    put_u32(bytes, phoff + 8u, base);
    put_u32(bytes, phoff + 12u, base);
    put_u32(bytes, phoff + 16u, 4u);
    put_u32(bytes, phoff + 20u, 4u);
    put_u32(bytes, phoff + 24u, 5u);
    put_u32(bytes, phoff + 28u, 0x1000u);

    auto parsed = b3r::recompiler::parse_ps2_elf(bytes);
    expect(parsed.ok(), "PAD runtime synthetic ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "PAD runtime synthetic ELF must map");
    return std::move(*built.memory);
}

b3r::analysis::R5900InstructionSite site(std::uint32_t pc,
                                        b3r::recompiler::R5900Instruction instruction,
                                        std::uint32_t raw) {
    b3r::analysis::R5900InstructionSite result{};
    result.pc = pc;
    result.decoded.raw = raw;
    result.decoded.instruction = instruction;
    return result;
}

b3r::analysis::R5900ReachabilityGraph make_graph(bool reverse_order) {
    using namespace b3r::analysis;
    using b3r::recompiler::R5900Instruction;

    R5900ReachabilityGraph graph{};
    graph.entry_pc = 0x1000u;

    R5900BasicBlock first{};
    first.start_pc = 0x1000u;
    first.instructions.push_back(site(0x1000u, R5900Instruction::Beq, 0x10800003u));
    first.delay_slot = site(0x1004u, R5900Instruction::Unknown, 0x4BEF1234u);
    first.end_kind = R5900BlockEndKind::ConditionalBranch;
    first.delay_slot_executes_on_fallthrough = true;
    first.edges.push_back(R5900ControlFlowEdge{R5900EdgeKind::BranchTaken, 0x1010u});
    first.edges.push_back(R5900ControlFlowEdge{R5900EdgeKind::BranchNotTaken, 0x1008u});

    R5900BasicBlock second{};
    second.start_pc = 0x1010u;
    second.instructions.push_back(site(0x1010u, R5900Instruction::Unknown, 0x712A4CC1u));
    second.end_kind = R5900BlockEndKind::UnsupportedInstruction;

    graph.blocks = {second, first};
    graph.calls = {
        R5900ReachabilityCall{0x1040u, 0x1044u, false, std::nullopt},
        R5900ReachabilityCall{0x1030u, 0x1034u, false, 0x3000u},
        R5900ReachabilityCall{0x1020u, 0x1024u, true, std::nullopt},
        R5900ReachabilityCall{0x1018u, 0x1018u, false, 0x2000u},
        R5900ReachabilityCall{0x1008u, 0x1008u, false, 0x2000u},
    };
    graph.issues = {
        R5900ReachabilityIssue{R5900ReachabilityIssueKind::TargetAnalysisFailed,
                              0x1000u,
                              0x3000u,
                              std::nullopt,
                              R5900ControlFlowError::UnmappedInstruction},
        R5900ReachabilityIssue{R5900ReachabilityIssueKind::UnresolvedIndirectExit,
                              0x1020u,
                              std::nullopt,
                              std::nullopt,
                              R5900ControlFlowError::None},
    };

    if (reverse_order) {
        std::reverse(graph.blocks.begin(), graph.blocks.end());
        std::reverse(graph.calls.begin(), graph.calls.end());
        std::reverse(graph.issues.begin(), graph.issues.end());
        std::reverse(graph.blocks.front().edges.begin(), graph.blocks.front().edges.end());
        std::reverse(graph.blocks.back().edges.begin(), graph.blocks.back().edges.end());
    }

    return graph;
}

void test_pad_binding_report() {
    using namespace b3r::analysis;

    PadBindingDiscoveryResult result{};
    for (std::size_t i = 0; i < result.resolutions.size(); ++i) {
        result.resolutions[i].function = static_cast<PadBindingFunction>(i);
    }
    auto& init = result.resolutions[static_cast<std::size_t>(PadBindingFunction::PadInit)];
    init.confidence = PadBindingConfidence::Trusted;
    init.guest_pc = 0x00102000u;
    init.evidence.push_back({PadBindingFunction::PadInit,
                             PadBindingEvidenceKind::ElfSymbol,
                             0x00102000u,
                             1000u,
                             "padInit"});

    const std::string expected =
        "PAD_BINDINGS_V0 1\n"
        "PAD_BINDING function=padInit confidence=trusted pc=0x00102000 evidence_count=1 max_score=1000\n"
        "PAD_BINDING_EVIDENCE function=padInit kind=elf_symbol pc=0x00102000 score=1000 detail=padInit\n"
        "PAD_BINDING function=padPortOpen confidence=unresolved pc=none evidence_count=0 max_score=0\n"
        "PAD_BINDING function=padGetState confidence=unresolved pc=none evidence_count=0 max_score=0\n"
        "PAD_BINDING function=padRead confidence=unresolved pc=none evidence_count=0 max_score=0\n"
        "PAD_BINDING function=padPortClose confidence=unresolved pc=none evidence_count=0 max_score=0\n"
        "PAD_BINDING function=padEnd confidence=unresolved pc=none evidence_count=0 max_score=0\n"
        "PAD_BINDINGS_END\n";

    const auto rendered = render_ps2_pad_binding_report(result);
    expect(rendered == expected,
           "minimal trusted symbol report must match the stable PAD_BINDINGS_V0 format");
    expect(render_ps2_pad_binding_report(result) == rendered,
           "repeated PAD binding report rendering must be byte-identical");

    PadBindingDiscoveryResult ambiguous{};
    for (std::size_t i = 0; i < ambiguous.resolutions.size(); ++i) {
        ambiguous.resolutions[i].function = static_cast<PadBindingFunction>(i);
    }
    auto& read = ambiguous.resolutions[static_cast<std::size_t>(PadBindingFunction::PadRead)];
    read.confidence = PadBindingConfidence::Candidate;
    read.evidence = {
        {PadBindingFunction::PadRead, PadBindingEvidenceKind::StaticFingerprint,
         0x00126000u, 125u, "slot_bound_8,copies_32_bytes"},
        {PadBindingFunction::PadRead, PadBindingEvidenceKind::StaticFingerprint,
         0x00125000u, 130u, "port_bound_2,slot_bound_8,copies_32_bytes"},
    };
    ambiguous.diagnostics = {"z diagnostic", "a diagnostic"};
    const auto ambiguous_report = render_ps2_pad_binding_report(ambiguous);
    expect(ambiguous_report.find(
               "PAD_BINDING function=padRead confidence=candidate pc=none evidence_count=2 max_score=130") != std::string::npos,
           "ambiguous static candidates must render pc=none and retain max score");
    expect(ambiguous_report.find("pc=0x00125000") < ambiguous_report.find("pc=0x00126000"),
           "PAD evidence must render in deterministic guest-PC order");
    expect(ambiguous_report.find("PAD_BINDING_DIAGNOSTIC a diagnostic") <
               ambiguous_report.find("PAD_BINDING_DIAGNOSTIC z diagnostic"),
           "PAD diagnostics must render in lexical order");
}

b3r::analysis::PadBindingDiscoveryResult discovery_for(
    b3r::analysis::PadBindingFunction function,
    std::uint32_t pc,
    b3r::analysis::PadBindingConfidence confidence =
        b3r::analysis::PadBindingConfidence::Candidate) {
    using namespace b3r::analysis;
    PadBindingDiscoveryResult discovery{};
    for (std::size_t i = 0; i < discovery.resolutions.size(); ++i) {
        discovery.resolutions[i].function = static_cast<PadBindingFunction>(i);
    }
    auto& resolution = discovery.resolutions[static_cast<std::size_t>(function)];
    resolution.confidence = confidence;
    resolution.guest_pc = pc;
    resolution.evidence.push_back({function,
                                   PadBindingEvidenceKind::StaticFingerprint,
                                   pc,
                                   100u,
                                   "synthetic-runtime"});
    return discovery;
}

b3r::recompiler::R5900CallObservation observation_for(
    std::uint32_t target,
    std::uint64_t a0 = 0u,
    std::uint64_t a1 = 0u,
    std::uint64_t a2 = 0u,
    std::uint64_t a3 = 0u,
    std::uint32_t call_pc = 0x00120000u) {
    b3r::recompiler::R5900CallObservation observation{};
    observation.call_pc = call_pc;
    observation.target_pc = target;
    observation.return_pc = call_pc + 8u;
    observation.args = {a0, a1, a2, a3};
    return observation;
}

b3r::analysis::PadRuntimeFunctionResult classify_one(
    b3r::analysis::PadBindingFunction function,
    std::uint32_t pc,
    const b3r::recompiler::R5900CallObservation& observation,
    const b3r::runtime::Ps2MemoryMap& memory) {
    using namespace b3r::analysis;
    const auto discovery = discovery_for(function, pc);
    Ps2PadRuntimeConfirmation confirmation(discovery, memory);
    confirmation.observe(observation);
    return confirmation.result().functions[static_cast<std::size_t>(function)];
}

void test_pad_runtime_confirmation_contract() {
    using namespace b3r::analysis;

    constexpr std::uint32_t pc = 0x00128000u;
    const b3r::runtime::Ps2MemoryMap empty_memory{};
    const auto resolved = classify_one(
        PadBindingFunction::PadInit, pc, observation_for(pc, 0u), empty_memory);
    expect(resolved.static_confidence == PadBindingConfidence::Candidate,
           "runtime confirmation must preserve static confidence");
    expect(resolved.runtime_status == PadRuntimeConfirmationStatus::RuntimeConfirmed,
           "compatible evidence-backed padInit call must runtime-confirm");
    expect(resolved.guest_pc.has_value() && *resolved.guest_pc == pc,
           "runtime-confirmed padInit must expose the unique compatible PC");
    expect(resolved.calls_observed == 1u && resolved.compatible_calls == 1u &&
               resolved.incompatible_calls == 0u,
           "runtime-confirmed padInit counters mismatch");

    const auto low32_zero = classify_one(
        PadBindingFunction::PadInit,
        pc,
        observation_for(pc, 0x100000000ull),
        empty_memory);
    expect(low32_zero.runtime_status == PadRuntimeConfirmationStatus::RuntimeConfirmed,
           "PAD ABI must interpret arguments through low32 like the HLE service");

    const auto bad_init = classify_one(
        PadBindingFunction::PadInit, pc, observation_for(pc, 1u), empty_memory);
    expect(bad_init.runtime_status ==
               PadRuntimeConfirmationStatus::ObservedIncompatible &&
               bad_init.incompatible_calls == 1u,
           "padInit nonzero mode must classify incompatible");
}

void test_pad_runtime_abi_matrix() {
    using namespace b3r::analysis;
    auto memory = make_pad_runtime_memory();
    constexpr std::uint32_t pc = 0x00129000u;
    constexpr std::uint32_t valid_area = 0x00120000u;
    constexpr std::uint32_t valid_read = 0x00121000u;
    constexpr std::uint32_t crossing_area = 0x01ffff80u;
    constexpr std::uint32_t crossing_read = 0x01fffff0u;

    auto status = [&](PadBindingFunction function,
                      std::uint64_t a0,
                      std::uint64_t a1,
                      std::uint64_t a2,
                      std::uint64_t a3 = 0u) {
        return classify_one(function, pc,
                            observation_for(pc, a0, a1, a2, a3),
                            memory).runtime_status;
    };

    expect(status(PadBindingFunction::PadPortOpen, 0u, 0u, valid_area) ==
               PadRuntimeConfirmationStatus::RuntimeConfirmed,
           "valid padPortOpen ABI must confirm");
    expect(status(PadBindingFunction::PadPortOpen, 1u, 0u, valid_area) ==
               PadRuntimeConfirmationStatus::ObservedIncompatible,
           "padPortOpen port 1 must reject");
    expect(status(PadBindingFunction::PadPortOpen, 0u, 1u, valid_area) ==
               PadRuntimeConfirmationStatus::ObservedIncompatible,
           "padPortOpen slot 1 must reject");
    expect(status(PadBindingFunction::PadPortOpen, 0u, 0u, 0u) ==
               PadRuntimeConfirmationStatus::ObservedIncompatible,
           "padPortOpen null area must reject");
    expect(status(PadBindingFunction::PadPortOpen, 0u, 0u, valid_area + 1u) ==
               PadRuntimeConfirmationStatus::ObservedIncompatible,
           "padPortOpen misaligned area must reject");
    expect(status(PadBindingFunction::PadPortOpen, 0u, 0u, crossing_area) ==
               PadRuntimeConfirmationStatus::ObservedIncompatible,
           "padPortOpen 256-byte range crossing RAM end must reject");

    expect(status(PadBindingFunction::PadGetState, 0u, 0u, 0u) ==
               PadRuntimeConfirmationStatus::RuntimeConfirmed,
           "valid padGetState ABI must confirm");
    expect(status(PadBindingFunction::PadGetState, 1u, 0u, 0u) ==
               PadRuntimeConfirmationStatus::ObservedIncompatible,
           "padGetState port 1 must reject");
    expect(status(PadBindingFunction::PadGetState, 0u, 1u, 0u) ==
               PadRuntimeConfirmationStatus::ObservedIncompatible,
           "padGetState slot 1 must reject");

    expect(status(PadBindingFunction::PadRead, 0u, 0u, valid_read) ==
               PadRuntimeConfirmationStatus::RuntimeConfirmed,
           "valid padRead ABI must confirm");
    expect(status(PadBindingFunction::PadRead, 1u, 0u, valid_read) ==
               PadRuntimeConfirmationStatus::ObservedIncompatible,
           "padRead port 1 must reject");
    expect(status(PadBindingFunction::PadRead, 0u, 1u, valid_read) ==
               PadRuntimeConfirmationStatus::ObservedIncompatible,
           "padRead slot 1 must reject");
    expect(status(PadBindingFunction::PadRead, 0u, 0u, 0u) ==
               PadRuntimeConfirmationStatus::ObservedIncompatible,
           "padRead null destination must reject");
    expect(status(PadBindingFunction::PadRead, 0u, 0u, crossing_read) ==
               PadRuntimeConfirmationStatus::ObservedIncompatible,
           "padRead 32-byte range crossing RAM end must reject");

    expect(status(PadBindingFunction::PadPortClose, 0u, 0u, 0u) ==
               PadRuntimeConfirmationStatus::RuntimeConfirmed,
           "valid padPortClose ABI must confirm");
    expect(status(PadBindingFunction::PadPortClose, 1u, 0u, 0u) ==
               PadRuntimeConfirmationStatus::ObservedIncompatible,
           "padPortClose port 1 must reject");
    expect(status(PadBindingFunction::PadPortClose, 0u, 1u, 0u) ==
               PadRuntimeConfirmationStatus::ObservedIncompatible,
           "padPortClose slot 1 must reject");

    expect(status(PadBindingFunction::PadEnd, 0x11u, 0x22u, 0x33u, 0x44u) ==
               PadRuntimeConfirmationStatus::RuntimeConfirmed,
           "padEnd must accept arbitrary v0 arguments");
}

void test_pad_runtime_evidence_aggregation() {
    using namespace b3r::analysis;
    auto memory = make_pad_runtime_memory();
    constexpr std::uint32_t read_pc = 0x0012a000u;
    constexpr std::uint32_t second_pc = 0x0012a100u;
    constexpr std::uint32_t valid_read = 0x00121000u;

    auto discovery = discovery_for(PadBindingFunction::PadRead, read_pc);
    auto& read = discovery.resolutions[static_cast<std::size_t>(PadBindingFunction::PadRead)];
    read.evidence.push_back({PadBindingFunction::PadRead,
                             PadBindingEvidenceKind::ElfSymbol,
                             read_pc,
                             1000u,
                             "duplicate-same-pc"});
    Ps2PadRuntimeConfirmation confirmation(discovery, memory);
    auto initial = confirmation.result();
    expect(initial.functions[static_cast<std::size_t>(PadBindingFunction::PadRead)]
                   .pc_evidence.size() == 1u,
           "duplicate static evidence for one function/PC must deduplicate");

    confirmation.observe(observation_for(read_pc, 1u, 0u, valid_read,
                                         0u, 0x00120010u));
    confirmation.observe(observation_for(read_pc, 0u, 0u, valid_read,
                                         0u, 0x00120020u));
    confirmation.observe(observation_for(read_pc, 1u, 0u, valid_read,
                                         0u, 0x00120030u));
    confirmation.observe(observation_for(read_pc, 0u, 0u, valid_read,
                                         0u, 0x00120040u));
    const auto mixed = confirmation.result();
    const auto& read_result = mixed.functions[
        static_cast<std::size_t>(PadBindingFunction::PadRead)];
    expect(read_result.runtime_status == PadRuntimeConfirmationStatus::RuntimeConfirmed &&
               read_result.calls_observed == 4u && read_result.compatible_calls == 2u &&
               read_result.incompatible_calls == 2u,
           "mixed observations at one compatible PC must remain RuntimeConfirmed");
    expect(read_result.pc_evidence.front().first_incompatible->call_pc == 0x00120010u &&
               read_result.pc_evidence.front().first_compatible->call_pc == 0x00120020u,
           "first compatible/incompatible observations must not be replaced");

    auto ambiguous = discovery_for(PadBindingFunction::PadRead, read_pc);
    auto& ambiguous_read = ambiguous.resolutions[
        static_cast<std::size_t>(PadBindingFunction::PadRead)];
    ambiguous_read.guest_pc.reset();
    ambiguous_read.evidence.push_back({PadBindingFunction::PadRead,
                                       PadBindingEvidenceKind::StaticFingerprint,
                                       second_pc,
                                       999u,
                                       "higher-score-runtime-tie"});
    Ps2PadRuntimeConfirmation ambiguous_confirmation(ambiguous, memory);
    ambiguous_confirmation.observe(observation_for(read_pc, 0u, 0u, valid_read));
    ambiguous_confirmation.observe(observation_for(second_pc, 1u, 0u, valid_read));
    const auto unique = ambiguous_confirmation.result();
    const auto& unique_read = unique.functions[
        static_cast<std::size_t>(PadBindingFunction::PadRead)];
    expect(unique_read.runtime_status == PadRuntimeConfirmationStatus::RuntimeConfirmed &&
               unique_read.guest_pc == read_pc,
           "one compatible PC plus incompatible alternate must resolve uniquely");

    ambiguous_confirmation.observe(observation_for(second_pc, 0u, 0u, valid_read));
    const auto tied = ambiguous_confirmation.result();
    const auto& tied_read = tied.functions[
        static_cast<std::size_t>(PadBindingFunction::PadRead)];
    expect(tied_read.runtime_status == PadRuntimeConfirmationStatus::RuntimeAmbiguous &&
               !tied_read.guest_pc.has_value(),
           "two compatible PCs must remain RuntimeAmbiguous regardless of static score");

    auto trusted = discovery_for(PadBindingFunction::PadInit,
                                 0x0012b000u,
                                 PadBindingConfidence::Trusted);
    Ps2PadRuntimeConfirmation trusted_confirmation(trusted, memory);
    const auto trusted_result = trusted_confirmation.result().functions[
        static_cast<std::size_t>(PadBindingFunction::PadInit)];
    expect(trusted_result.static_confidence == PadBindingConfidence::Trusted &&
               trusted_result.runtime_status == PadRuntimeConfirmationStatus::Unobserved,
           "trusted static evidence may remain runtime-unobserved");

    auto shared = discovery_for(PadBindingFunction::PadInit, 0x0012c000u);
    auto& end = shared.resolutions[static_cast<std::size_t>(PadBindingFunction::PadEnd)];
    end.function = PadBindingFunction::PadEnd;
    end.confidence = PadBindingConfidence::Candidate;
    end.guest_pc = 0x0012c000u;
    end.evidence.push_back({PadBindingFunction::PadEnd,
                            PadBindingEvidenceKind::StaticFingerprint,
                            0x0012c000u,
                            100u,
                            "shared-pc"});
    Ps2PadRuntimeConfirmation shared_confirmation(shared, memory);
    shared_confirmation.observe(observation_for(0x0012c000u, 1u));
    const auto shared_result = shared_confirmation.result();
    expect(shared_result.functions[static_cast<std::size_t>(PadBindingFunction::PadInit)]
                   .runtime_status == PadRuntimeConfirmationStatus::ObservedIncompatible,
           "same numeric PC must classify padInit independently");
    expect(shared_result.functions[static_cast<std::size_t>(PadBindingFunction::PadEnd)]
                   .runtime_status == PadRuntimeConfirmationStatus::RuntimeConfirmed,
           "same numeric PC must classify padEnd independently");

    const auto unrelated_discovery = discovery_for(PadBindingFunction::PadRead, read_pc);
    Ps2PadRuntimeConfirmation unrelated(unrelated_discovery, memory);
    unrelated.observe(observation_for(0x0012ffffu, 0u, 0u, valid_read));
    expect(unrelated.result().functions[static_cast<std::size_t>(PadBindingFunction::PadRead)]
                   .runtime_status == PadRuntimeConfirmationStatus::Unobserved,
           "unrelated call target must be ignored completely");

    std::size_t saturated = std::numeric_limits<std::size_t>::max();
    ps2_pad_runtime_confirmation_detail::saturating_increment(saturated);
    expect(saturated == std::numeric_limits<std::size_t>::max(),
           "saturating increment must not wrap");
    expect(ps2_pad_runtime_confirmation_detail::saturating_add(
               std::numeric_limits<std::size_t>::max() - 1u, 2u) ==
               std::numeric_limits<std::size_t>::max(),
           "saturating add must clamp at size_t max");
}

} // namespace

int main() {
    using b3r::analysis::render_r5900_analysis_report;

    const std::string expected =
        "ENTRY 0x00001000\n"
        "BLOCKS 2\n"
        "INSTRUCTIONS 3\n"
        "DECODED 1\n"
        "UNKNOWN 2\n"
        "CALLS 5\n"
        "INDIRECT_EXITS 1\n"
        "CFG_ISSUES 2\n"
        "INSTRUCTION_HISTOGRAM 2\n"
        "  BEQ 1\n"
        "  UNKNOWN 2\n"
        "UNKNOWN_PRIMARY_OPCODES 2\n"
        "  0x12 1\n"
        "  0x1C 1\n"
        "UNKNOWN_SITES 2\n"
        "  PC 0x00001004 RAW 0x4BEF1234 PRIMARY 0x12 RS 0x1F RT 0x0F RD 0x02 SA 0x08 FUNCT 0x34\n"
        "  PC 0x00001010 RAW 0x712A4CC1 PRIMARY 0x1C RS 0x09 RT 0x0A RD 0x09 SA 0x13 FUNCT 0x01\n"
        "DIRECT_CALL_TARGETS 2\n"
        "  TARGET 0x00002000 CALL_SITES 2\n"
        "  TARGET 0x00003000 CALL_SITES 1\n"
        "\n"
        "BLOCK 0x00001000 END ConditionalBranch\n"
        "  0x00001000 BEQ RAW 0x10800003\n"
        "  DELAY 0x00001004 UNKNOWN RAW 0x4BEF1234 FALLTHROUGH yes\n"
        "  EDGE BranchTaken 0x00001010\n"
        "  EDGE BranchNotTaken 0x00001008\n"
        "\n"
        "BLOCK 0x00001010 END UnsupportedInstruction\n"
        "  0x00001010 UNKNOWN RAW 0x712A4CC1\n"
        "\n"
        "CALL 0x00001008 PC 0x00001008 DIRECT 0x00002000\n"
        "CALL 0x00001018 PC 0x00001018 DIRECT 0x00002000\n"
        "CALL 0x00001020 PC 0x00001024 INDIRECT unresolved\n"
        "CALL 0x00001030 PC 0x00001034 DIRECT 0x00003000\n"
        "CALL 0x00001040 PC 0x00001044 DIRECT unresolved\n"
        "\n"
        "ISSUE UnresolvedIndirectExit SOURCE 0x00001020\n"
        "ISSUE TargetAnalysisFailed SOURCE 0x00001000 TARGET 0x00003000 ERROR UnmappedInstruction\n";

    const auto ordered = render_r5900_analysis_report(make_graph(false));
    expect(ordered == expected,
           "report must aggregate resolved direct call targets by static call-site count");

    const auto reordered = render_r5900_analysis_report(make_graph(true));
    expect(reordered == expected,
           "direct-call target aggregation must remain deterministic regardless of graph container order");

    test_pad_binding_report();
    test_pad_runtime_confirmation_contract();
    test_pad_runtime_abi_matrix();
    test_pad_runtime_evidence_aggregation();

    std::cout << "r5900_analysis_report_tests: PASS\n";
    return EXIT_SUCCESS;
}
