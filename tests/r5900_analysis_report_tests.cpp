#include "analysis/ps2_pad_binding_report.h"
#include "analysis/r5900_analysis_report.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_analysis_report_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
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
                             0u,
                             "padInit"});

    const std::string expected =
        "PAD_BINDINGS_V0 1\n"
        "PAD_BINDING function=padInit confidence=trusted pc=0x00102000 evidence_count=1 max_score=0\n"
        "PAD_BINDING_EVIDENCE function=padInit kind=elf_symbol pc=0x00102000 score=0 detail=padInit\n"
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

    std::cout << "r5900_analysis_report_tests: PASS\n";
    return EXIT_SUCCESS;
}
