#include "analysis/r5900_analysis_report.h"

#include "recompiler/r5900_decoder.h"

#include <algorithm>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace b3r::analysis {
namespace {

std::string hex32(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setfill('0') << std::setw(8) << value;
    return out.str();
}

std::string hex_opcode(std::uint8_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setfill('0') << std::setw(2)
        << static_cast<unsigned int>(value);
    return out.str();
}

const char* block_end_name(R5900BlockEndKind kind) noexcept {
    switch (kind) {
    case R5900BlockEndKind::InstructionLimit: return "InstructionLimit";
    case R5900BlockEndKind::ConditionalBranch: return "ConditionalBranch";
    case R5900BlockEndKind::DirectJump: return "DirectJump";
    case R5900BlockEndKind::DirectCall: return "DirectCall";
    case R5900BlockEndKind::IndirectJump: return "IndirectJump";
    case R5900BlockEndKind::IndirectCall: return "IndirectCall";
    case R5900BlockEndKind::Trap: return "Trap";
    case R5900BlockEndKind::UnsupportedInstruction: return "UnsupportedInstruction";
    }
    return "Unknown";
}

const char* edge_name(R5900EdgeKind kind) noexcept {
    switch (kind) {
    case R5900EdgeKind::BranchTaken: return "BranchTaken";
    case R5900EdgeKind::BranchNotTaken: return "BranchNotTaken";
    case R5900EdgeKind::DirectJump: return "DirectJump";
    case R5900EdgeKind::DirectCall: return "DirectCall";
    case R5900EdgeKind::CallContinuation: return "CallContinuation";
    case R5900EdgeKind::IndirectJump: return "IndirectJump";
    case R5900EdgeKind::IndirectCall: return "IndirectCall";
    case R5900EdgeKind::Fallthrough: return "Fallthrough";
    }
    return "Unknown";
}

const char* issue_name(R5900ReachabilityIssueKind kind) noexcept {
    switch (kind) {
    case R5900ReachabilityIssueKind::UnresolvedIndirectExit: return "UnresolvedIndirectExit";
    case R5900ReachabilityIssueKind::TargetAnalysisFailed: return "TargetAnalysisFailed";
    case R5900ReachabilityIssueKind::BlockLimitReached: return "BlockLimitReached";
    case R5900ReachabilityIssueKind::LeaderInsideBlock: return "LeaderInsideBlock";
    case R5900ReachabilityIssueKind::LeaderInsideDelaySlot: return "LeaderInsideDelaySlot";
    }
    return "Unknown";
}

const char* control_flow_error_name(R5900ControlFlowError error) noexcept {
    switch (error) {
    case R5900ControlFlowError::None: return "None";
    case R5900ControlFlowError::InvalidInstructionLimit: return "InvalidInstructionLimit";
    case R5900ControlFlowError::UnalignedStart: return "UnalignedStart";
    case R5900ControlFlowError::UnmappedInstruction: return "UnmappedInstruction";
    case R5900ControlFlowError::NonExecutableInstruction: return "NonExecutableInstruction";
    case R5900ControlFlowError::MissingDelaySlot: return "MissingDelaySlot";
    case R5900ControlFlowError::NonExecutableDelaySlot: return "NonExecutableDelaySlot";
    }
    return "Unknown";
}

using InstructionHistogram = std::map<std::string, std::size_t>;
using UnknownPrimaryHistogram = std::map<std::uint8_t, std::size_t>;

struct UnknownSite {
    std::uint32_t pc{};
    std::uint32_t raw{};
};

void count_site(const R5900InstructionSite& site,
                std::size_t& instructions,
                std::size_t& decoded,
                std::size_t& unknown,
                InstructionHistogram& instruction_histogram,
                UnknownPrimaryHistogram& unknown_primary_histogram,
                std::vector<UnknownSite>& unknown_sites) {
    ++instructions;

    const auto instruction_name =
        std::string(recompiler::r5900_instruction_name(site.decoded.instruction));
    ++instruction_histogram[instruction_name];

    if (site.decoded.instruction == recompiler::R5900Instruction::Unknown) {
        ++unknown;
        const auto primary_opcode =
            static_cast<std::uint8_t>((site.decoded.raw >> 26u) & 0x3Fu);
        ++unknown_primary_histogram[primary_opcode];
        unknown_sites.push_back(UnknownSite{site.pc, site.decoded.raw});
    } else {
        ++decoded;
    }
}

} // namespace

std::string render_r5900_analysis_report(const R5900ReachabilityGraph& graph) {
    std::vector<const R5900BasicBlock*> blocks;
    blocks.reserve(graph.blocks.size());
    for (const auto& block : graph.blocks) {
        blocks.push_back(&block);
    }
    std::sort(blocks.begin(), blocks.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->start_pc < rhs->start_pc;
    });

    std::size_t instruction_count = 0;
    std::size_t decoded_count = 0;
    std::size_t unknown_count = 0;
    InstructionHistogram instruction_histogram{};
    UnknownPrimaryHistogram unknown_primary_histogram{};
    std::vector<UnknownSite> unknown_sites{};
    for (const auto* block : blocks) {
        for (const auto& instruction : block->instructions) {
            count_site(instruction,
                       instruction_count,
                       decoded_count,
                       unknown_count,
                       instruction_histogram,
                       unknown_primary_histogram,
                       unknown_sites);
        }
        if (block->delay_slot.has_value()) {
            count_site(*block->delay_slot,
                       instruction_count,
                       decoded_count,
                       unknown_count,
                       instruction_histogram,
                       unknown_primary_histogram,
                       unknown_sites);
        }
    }
    std::sort(unknown_sites.begin(), unknown_sites.end(), [](const auto& lhs, const auto& rhs) {
        return std::tuple{lhs.pc, lhs.raw} < std::tuple{rhs.pc, rhs.raw};
    });

    const auto indirect_exit_count = static_cast<std::size_t>(std::count_if(
        graph.issues.begin(), graph.issues.end(), [](const auto& issue) {
            return issue.kind == R5900ReachabilityIssueKind::UnresolvedIndirectExit;
        }));

    std::ostringstream out;
    out << "ENTRY " << hex32(graph.entry_pc) << '\n'
        << "BLOCKS " << graph.blocks.size() << '\n'
        << "INSTRUCTIONS " << instruction_count << '\n'
        << "DECODED " << decoded_count << '\n'
        << "UNKNOWN " << unknown_count << '\n'
        << "CALLS " << graph.calls.size() << '\n'
        << "INDIRECT_EXITS " << indirect_exit_count << '\n'
        << "CFG_ISSUES " << graph.issues.size() << '\n'
        << "INSTRUCTION_HISTOGRAM " << instruction_histogram.size() << '\n';

    for (const auto& [name, count] : instruction_histogram) {
        out << "  " << name << ' ' << count << '\n';
    }

    out << "UNKNOWN_PRIMARY_OPCODES " << unknown_primary_histogram.size() << '\n';
    for (const auto& [opcode, count] : unknown_primary_histogram) {
        out << "  " << hex_opcode(opcode) << ' ' << count << '\n';
    }

    out << "UNKNOWN_SITES " << unknown_sites.size() << '\n';
    for (const auto& site : unknown_sites) {
        const auto primary = static_cast<std::uint8_t>((site.raw >> 26u) & 0x3Fu);
        const auto rs = static_cast<std::uint8_t>((site.raw >> 21u) & 0x1Fu);
        const auto rt = static_cast<std::uint8_t>((site.raw >> 16u) & 0x1Fu);
        const auto rd = static_cast<std::uint8_t>((site.raw >> 11u) & 0x1Fu);
        const auto sa = static_cast<std::uint8_t>((site.raw >> 6u) & 0x1Fu);
        const auto funct = static_cast<std::uint8_t>(site.raw & 0x3Fu);
        out << "  PC " << hex32(site.pc)
            << " RAW " << hex32(site.raw)
            << " PRIMARY " << hex_opcode(primary)
            << " RS " << hex_opcode(rs)
            << " RT " << hex_opcode(rt)
            << " RD " << hex_opcode(rd)
            << " SA " << hex_opcode(sa)
            << " FUNCT " << hex_opcode(funct) << '\n';
    }
    out << '\n';

    for (const auto* block : blocks) {
        out << "BLOCK " << hex32(block->start_pc) << " END " << block_end_name(block->end_kind) << '\n';
        for (const auto& instruction : block->instructions) {
            out << "  " << hex32(instruction.pc) << ' '
                << recompiler::r5900_instruction_name(instruction.decoded.instruction)
                << " RAW " << hex32(instruction.decoded.raw) << '\n';
        }
        if (block->delay_slot.has_value()) {
            out << "  DELAY " << hex32(block->delay_slot->pc) << ' '
                << recompiler::r5900_instruction_name(block->delay_slot->decoded.instruction)
                << " RAW " << hex32(block->delay_slot->decoded.raw)
                << " FALLTHROUGH " << (block->delay_slot_executes_on_fallthrough ? "yes" : "no") << '\n';
        }

        auto edges = block->edges;
        std::sort(edges.begin(), edges.end(), [](const auto& lhs, const auto& rhs) {
            return std::tuple{lhs.kind, lhs.target} < std::tuple{rhs.kind, rhs.target};
        });
        for (const auto& edge : edges) {
            out << "  EDGE " << edge_name(edge.kind) << ' ';
            if (edge.target.has_value()) {
                out << hex32(*edge.target);
            } else {
                out << "unresolved";
            }
            out << '\n';
        }
        out << '\n';
    }

    auto calls = graph.calls;
    std::sort(calls.begin(), calls.end(), [](const auto& lhs, const auto& rhs) {
        return std::tuple{lhs.source_block, lhs.call_pc, lhs.indirect, lhs.target} <
               std::tuple{rhs.source_block, rhs.call_pc, rhs.indirect, rhs.target};
    });
    for (const auto& call : calls) {
        out << "CALL " << hex32(call.source_block) << " PC " << hex32(call.call_pc) << ' ';
        if (call.indirect) {
            out << "INDIRECT unresolved";
        } else {
            out << "DIRECT ";
            if (call.target.has_value()) {
                out << hex32(*call.target);
            } else {
                out << "unresolved";
            }
        }
        out << '\n';
    }
    if (!calls.empty()) {
        out << '\n';
    }

    auto issues = graph.issues;
    std::sort(issues.begin(), issues.end(), [](const auto& lhs, const auto& rhs) {
        return std::tuple{lhs.kind, lhs.source_block, lhs.target, lhs.related_block, lhs.analysis_error} <
               std::tuple{rhs.kind, rhs.source_block, rhs.target, rhs.related_block, rhs.analysis_error};
    });
    for (const auto& issue : issues) {
        out << "ISSUE " << issue_name(issue.kind) << " SOURCE " << hex32(issue.source_block);
        if (issue.target.has_value()) {
            out << " TARGET " << hex32(*issue.target);
        }
        if (issue.related_block.has_value()) {
            out << " RELATED " << hex32(*issue.related_block);
        }
        if (issue.analysis_error != R5900ControlFlowError::None) {
            out << " ERROR " << control_flow_error_name(issue.analysis_error);
        }
        out << '\n';
    }

    return out.str();
}

} // namespace b3r::analysis
