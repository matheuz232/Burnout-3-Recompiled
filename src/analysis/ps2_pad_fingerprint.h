#pragma once

#include "analysis/ps2_pad_binding_discovery.h"
#include "analysis/r5900_reachability.h"
#include "runtime/ps2_memory_map.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace b3r::analysis {

inline constexpr std::uint32_t kPadBindRpcId1New = 0x80000100u;
inline constexpr std::uint32_t kPadBindRpcId2New = 0x80000101u;
inline constexpr std::uint32_t kPadBindRpcId1Old = 0x8000010fu;
inline constexpr std::uint32_t kPadBindRpcId2Old = 0x8000011fu;
inline constexpr std::uint32_t kPadRpcCommandOpenNew = 0x01u;
inline constexpr std::uint32_t kPadRpcCommandCloseNew = 0x0eu;
inline constexpr std::uint32_t kPadRpcCommandEndNew = 0x0fu;
inline constexpr std::uint32_t kPadRpcCommandInit = 0x10u;
inline constexpr std::uint32_t kPadStateStable = 0x06u;
inline constexpr std::uint32_t kPadFingerprintThreshold = 100u;

struct PadFingerprintFeatures {
    bool rpc_bind_new_1{};
    bool rpc_bind_new_2{};
    bool rpc_bind_old_1{};
    bool rpc_bind_old_2{};
    bool command_init{};
    bool command_open{};
    bool command_close{};
    bool command_end{};
    bool alignment_mask_0x3f{};
    bool port_bound_2{};
    bool slot_bound_8{};
    bool state_stable_6{};
    bool copies_32_bytes{};
    std::size_t direct_call_count{};
};

struct PadFingerprintCandidate {
    PadBindingFunction function{};
    std::uint32_t guest_pc{};
    std::uint32_t score{};
    PadFingerprintFeatures features{};
};

[[nodiscard]] inline std::uint32_t score_pad_init(const PadFingerprintFeatures& f) noexcept {
    const std::uint32_t rpc_count =
        static_cast<std::uint32_t>(f.rpc_bind_new_1) +
        static_cast<std::uint32_t>(f.rpc_bind_new_2) +
        static_cast<std::uint32_t>(f.rpc_bind_old_1) +
        static_cast<std::uint32_t>(f.rpc_bind_old_2);
    std::uint32_t score = 0u;
    if (rpc_count >= 1u) {
        score += 60u;
    }
    if (rpc_count >= 2u) {
        score += 40u;
    }
    if (f.command_init) {
        score += 50u;
    }
    if (f.direct_call_count >= 2u) {
        score += 20u;
    }
    return score;
}

[[nodiscard]] inline std::uint32_t score_pad_port_open(const PadFingerprintFeatures& f) noexcept {
    return (f.command_open ? 40u : 0u) +
           (f.alignment_mask_0x3f ? 70u : 0u) +
           (f.port_bound_2 ? 30u : 0u) +
           (f.slot_bound_8 ? 30u : 0u);
}

[[nodiscard]] inline std::uint32_t score_pad_get_state(const PadFingerprintFeatures& f) noexcept {
    return (f.state_stable_6 ? 80u : 0u) +
           (f.port_bound_2 ? 20u : 0u) +
           (f.slot_bound_8 ? 20u : 0u);
}

[[nodiscard]] inline std::uint32_t score_pad_read(const PadFingerprintFeatures& f) noexcept {
    return (f.copies_32_bytes ? 80u : 0u) +
           (f.port_bound_2 ? 20u : 0u) +
           (f.slot_bound_8 ? 20u : 0u);
}

[[nodiscard]] inline std::uint32_t score_pad_port_close(const PadFingerprintFeatures& f) noexcept {
    return (f.command_close ? 100u : 0u) +
           (f.port_bound_2 ? 20u : 0u) +
           (f.slot_bound_8 ? 20u : 0u);
}

[[nodiscard]] inline std::uint32_t score_pad_end(const PadFingerprintFeatures& f) noexcept {
    return f.command_end ? 120u : 0u;
}

namespace ps2_pad_fingerprint_detail {

using BlockView = std::vector<const R5900BasicBlock*>;

[[nodiscard]] inline const R5900BasicBlock*
find_block(const R5900ReachabilityGraph& graph, std::uint32_t start_pc) noexcept {
    const auto it = std::find_if(graph.blocks.begin(), graph.blocks.end(),
                                 [start_pc](const R5900BasicBlock& block) {
                                     return block.start_pc == start_pc;
                                 });
    return it == graph.blocks.end() ? nullptr : &*it;
}

[[nodiscard]] inline bool follows_function_edge(R5900EdgeKind kind) noexcept {
    switch (kind) {
    case R5900EdgeKind::BranchTaken:
    case R5900EdgeKind::BranchNotTaken:
    case R5900EdgeKind::DirectJump:
    case R5900EdgeKind::CallContinuation:
    case R5900EdgeKind::Fallthrough:
        return true;
    case R5900EdgeKind::DirectCall:
    case R5900EdgeKind::IndirectJump:
    case R5900EdgeKind::IndirectCall:
        return false;
    }
    return false;
}

[[nodiscard]] inline std::vector<std::uint32_t>
candidate_roots(const R5900ReachabilityGraph& graph) {
    std::vector<std::uint32_t> roots{};
    if (find_block(graph, graph.entry_pc) != nullptr) {
        roots.push_back(graph.entry_pc);
    }
    for (const auto& call : graph.calls) {
        if (!call.indirect && call.target.has_value() &&
            find_block(graph, *call.target) != nullptr) {
            roots.push_back(*call.target);
        }
    }
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    return roots;
}

[[nodiscard]] inline bool is_root_boundary(std::span<const std::uint32_t> roots,
                                           std::uint32_t root,
                                           std::uint32_t target) noexcept {
    return target != root && std::binary_search(roots.begin(), roots.end(), target);
}

[[nodiscard]] inline BlockView
build_function_view(const R5900ReachabilityGraph& graph,
                    std::span<const std::uint32_t> roots,
                    std::uint32_t root) {
    BlockView view{};
    std::vector<std::uint32_t> queue{root};
    std::vector<std::uint32_t> seen{};

    for (std::size_t cursor = 0; cursor < queue.size(); ++cursor) {
        const std::uint32_t pc = queue[cursor];
        if (std::find(seen.begin(), seen.end(), pc) != seen.end()) {
            continue;
        }
        seen.push_back(pc);

        const R5900BasicBlock* block = find_block(graph, pc);
        if (block == nullptr) {
            continue;
        }
        view.push_back(block);

        for (const auto& edge : block->edges) {
            if (!follows_function_edge(edge.kind) || !edge.target.has_value()) {
                continue;
            }
            const std::uint32_t target = *edge.target;
            if (is_root_boundary(roots, root, target)) {
                continue;
            }
            if (find_block(graph, target) != nullptr) {
                queue.push_back(target);
            }
        }
    }

    std::sort(view.begin(), view.end(), [](const R5900BasicBlock* lhs,
                                           const R5900BasicBlock* rhs) {
        return lhs->start_pc < rhs->start_pc;
    });
    return view;
}

[[nodiscard]] inline bool writes_gpr(const recompiler::R5900DecodedInstruction& insn,
                                     std::uint8_t reg) noexcept {
    if (reg == 0u) {
        return false;
    }
    using recompiler::R5900Instruction;
    switch (insn.instruction) {
    case R5900Instruction::Sll:
    case R5900Instruction::Srl:
    case R5900Instruction::Sra:
    case R5900Instruction::Sllv:
    case R5900Instruction::Srlv:
    case R5900Instruction::Srav:
    case R5900Instruction::Mfhi:
    case R5900Instruction::Mflo:
    case R5900Instruction::Add:
    case R5900Instruction::Addu:
    case R5900Instruction::Daddu:
    case R5900Instruction::Sub:
    case R5900Instruction::Subu:
    case R5900Instruction::And:
    case R5900Instruction::Or:
    case R5900Instruction::Xor:
    case R5900Instruction::Nor:
    case R5900Instruction::Slt:
    case R5900Instruction::Sltu:
        return insn.rd == reg;
    case R5900Instruction::Addi:
    case R5900Instruction::Addiu:
    case R5900Instruction::Slti:
    case R5900Instruction::Sltiu:
    case R5900Instruction::Andi:
    case R5900Instruction::Ori:
    case R5900Instruction::Xori:
    case R5900Instruction::Lui:
    case R5900Instruction::Daddi:
    case R5900Instruction::Daddiu:
    case R5900Instruction::Lb:
    case R5900Instruction::Lh:
    case R5900Instruction::Lwl:
    case R5900Instruction::Lw:
    case R5900Instruction::Lbu:
    case R5900Instruction::Lhu:
    case R5900Instruction::Lwr:
    case R5900Instruction::Lwu:
    case R5900Instruction::Ld:
    case R5900Instruction::Lq:
    case R5900Instruction::Ldl:
    case R5900Instruction::Ldr:
        return insn.rt == reg;
    default:
        return false;
    }
}

inline void mark_constant(PadFingerprintFeatures& features, std::uint32_t value) noexcept {
    switch (value) {
    case kPadBindRpcId1New: features.rpc_bind_new_1 = true; break;
    case kPadBindRpcId2New: features.rpc_bind_new_2 = true; break;
    case kPadBindRpcId1Old: features.rpc_bind_old_1 = true; break;
    case kPadBindRpcId2Old: features.rpc_bind_old_2 = true; break;
    case kPadRpcCommandInit: features.command_init = true; break;
    case kPadRpcCommandOpenNew: features.command_open = true; break;
    case kPadRpcCommandCloseNew: features.command_close = true; break;
    case kPadRpcCommandEndNew: features.command_end = true; break;
    case kPadStateStable: features.state_stable_6 = true; break;
    default: break;
    }
}

[[nodiscard]] inline std::vector<R5900InstructionSite>
block_sites(const R5900BasicBlock& block) {
    std::vector<R5900InstructionSite> sites = block.instructions;
    if (block.delay_slot.has_value()) {
        sites.push_back(*block.delay_slot);
    }
    std::sort(sites.begin(), sites.end(), [](const R5900InstructionSite& lhs,
                                             const R5900InstructionSite& rhs) {
        return lhs.pc < rhs.pc;
    });
    return sites;
}

inline void extract_block_constants(const R5900BasicBlock& block,
                                    PadFingerprintFeatures& features) {
    using recompiler::R5900Instruction;
    const auto sites = block_sites(block);

    for (std::size_t i = 0; i < sites.size(); ++i) {
        const auto& insn = sites[i].decoded;
        if ((insn.instruction == R5900Instruction::Ori ||
             insn.instruction == R5900Instruction::Addiu) &&
            insn.rs == 0u) {
            const std::uint32_t value = insn.instruction == R5900Instruction::Ori
                ? static_cast<std::uint32_t>(insn.immediate)
                : static_cast<std::uint32_t>(insn.signed_immediate());
            mark_constant(features, value);
        }

        if (insn.instruction == R5900Instruction::Sltiu) {
            if (insn.immediate == 2u) {
                features.port_bound_2 = true;
            }
            if (insn.immediate == 8u) {
                features.slot_bound_8 = true;
            }
        }
        if (insn.instruction == R5900Instruction::Andi && insn.immediate == 0x003fu) {
            features.alignment_mask_0x3f = true;
        }

        if (insn.instruction != R5900Instruction::Lui || insn.rt == 0u) {
            continue;
        }
        const std::uint8_t reg = insn.rt;
        const std::uint32_t high = static_cast<std::uint32_t>(insn.immediate) << 16u;
        for (std::size_t j = i + 1u; j < sites.size(); ++j) {
            const auto& next = sites[j].decoded;
            if ((next.instruction == R5900Instruction::Ori ||
                 next.instruction == R5900Instruction::Addiu) &&
                next.rs == reg && next.rt == reg) {
                const std::uint32_t value = next.instruction == R5900Instruction::Ori
                    ? high | static_cast<std::uint32_t>(next.immediate)
                    : static_cast<std::uint32_t>(
                          static_cast<std::int64_t>(high) + next.signed_immediate());
                mark_constant(features, value);
                break;
            }
            if (writes_gpr(next, reg)) {
                break;
            }
        }
    }
}

[[nodiscard]] inline bool has_copy_32_loop(const BlockView& view) {
    using recompiler::R5900Instruction;
    std::vector<R5900InstructionSite> sites{};
    for (const auto* block : view) {
        const auto block_instructions = block_sites(*block);
        sites.insert(sites.end(), block_instructions.begin(), block_instructions.end());
    }
    std::sort(sites.begin(), sites.end(), [](const R5900InstructionSite& lhs,
                                             const R5900InstructionSite& rhs) {
        return lhs.pc < rhs.pc;
    });

    struct Init {
        std::uint8_t reg{};
        std::uint32_t pc{};
    };
    std::vector<Init> initializers{};
    for (const auto& site : sites) {
        const auto& insn = site.decoded;
        if ((insn.instruction == R5900Instruction::Ori ||
             insn.instruction == R5900Instruction::Addiu) &&
            insn.rs == 0u && insn.rt != 0u && insn.immediate == 32u) {
            initializers.push_back({insn.rt, site.pc});
        }
    }

    for (const auto& branch_site : sites) {
        const auto& branch = branch_site.decoded;
        if (!branch.is_branch()) {
            continue;
        }
        const auto target = branch.direct_target(branch_site.pc);
        if (!target.has_value() || *target >= branch_site.pc) {
            continue;
        }

        bool has_load = false;
        bool has_store = false;
        for (const auto& loop_site : sites) {
            if (loop_site.pc < *target || loop_site.pc > branch_site.pc) {
                continue;
            }
            has_load = has_load ||
                loop_site.decoded.instruction_class == recompiler::R5900InstructionClass::Load;
            has_store = has_store ||
                loop_site.decoded.instruction_class == recompiler::R5900InstructionClass::Store;
        }
        if (!has_load || !has_store) {
            continue;
        }

        for (const auto& init : initializers) {
            if (init.pc > branch_site.pc) {
                continue;
            }
            const bool decremented = std::any_of(
                sites.begin(), sites.end(), [&](const R5900InstructionSite& loop_site) {
                    if (loop_site.pc < *target || loop_site.pc > branch_site.pc) {
                        return false;
                    }
                    const auto& insn = loop_site.decoded;
                    return insn.instruction == R5900Instruction::Addiu &&
                           insn.rs == init.reg && insn.rt == init.reg &&
                           (insn.signed_immediate() == -1 || insn.signed_immediate() == -4);
                });
            if (decremented) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] inline PadFingerprintFeatures
extract_features(const BlockView& view,
                 const R5900ReachabilityGraph& graph) {
    PadFingerprintFeatures features{};
    for (const auto* block : view) {
        extract_block_constants(*block, features);
    }
    features.copies_32_bytes = has_copy_32_loop(view);

    for (const auto& call : graph.calls) {
        if (call.indirect) {
            continue;
        }
        const bool source_in_view = std::any_of(
            view.begin(), view.end(), [&](const R5900BasicBlock* block) {
                return block->start_pc == call.source_block;
            });
        if (source_in_view) {
            ++features.direct_call_count;
        }
    }
    return features;
}

[[nodiscard]] inline std::uint32_t
score_for(PadBindingFunction function, const PadFingerprintFeatures& features) noexcept {
    switch (function) {
    case PadBindingFunction::PadInit: return score_pad_init(features);
    case PadBindingFunction::PadPortOpen: return score_pad_port_open(features);
    case PadBindingFunction::PadGetState: return score_pad_get_state(features);
    case PadBindingFunction::PadRead: return score_pad_read(features);
    case PadBindingFunction::PadPortClose: return score_pad_port_close(features);
    case PadBindingFunction::PadEnd: return score_pad_end(features);
    }
    return 0u;
}

inline void append_feature(std::string& detail, const char* name) {
    if (!detail.empty()) {
        detail.push_back(',');
    }
    detail += name;
}

[[nodiscard]] inline std::string feature_detail(const PadFingerprintFeatures& f) {
    std::string detail{};
    if (f.rpc_bind_new_1) append_feature(detail, "rpc_bind_new_1");
    if (f.rpc_bind_new_2) append_feature(detail, "rpc_bind_new_2");
    if (f.rpc_bind_old_1) append_feature(detail, "rpc_bind_old_1");
    if (f.rpc_bind_old_2) append_feature(detail, "rpc_bind_old_2");
    if (f.command_init) append_feature(detail, "command_init");
    if (f.command_open) append_feature(detail, "command_open");
    if (f.command_close) append_feature(detail, "command_close");
    if (f.command_end) append_feature(detail, "command_end");
    if (f.alignment_mask_0x3f) append_feature(detail, "alignment_mask_0x3f");
    if (f.port_bound_2) append_feature(detail, "port_bound_2");
    if (f.slot_bound_8) append_feature(detail, "slot_bound_8");
    if (f.state_stable_6) append_feature(detail, "state_stable_6");
    if (f.copies_32_bytes) append_feature(detail, "copies_32_bytes");
    if (f.direct_call_count != 0u) {
        append_feature(detail, "direct_call_count");
        detail += '=' + std::to_string(f.direct_call_count);
    }
    return detail;
}

} // namespace ps2_pad_fingerprint_detail

[[nodiscard]] inline std::vector<PadFingerprintCandidate>
scan_ps2_pad_fingerprints(const runtime::Ps2MemoryMap& memory,
                          const R5900ReachabilityGraph& graph) {
    (void)memory;
    std::vector<PadFingerprintCandidate> candidates{};
    const auto roots = ps2_pad_fingerprint_detail::candidate_roots(graph);

    for (const std::uint32_t root : roots) {
        const auto view = ps2_pad_fingerprint_detail::build_function_view(graph, roots, root);
        const auto features = ps2_pad_fingerprint_detail::extract_features(view, graph);
        for (std::size_t index = 0; index < 6u; ++index) {
            const auto function = static_cast<PadBindingFunction>(index);
            const std::uint32_t score = ps2_pad_fingerprint_detail::score_for(function, features);
            if (score >= kPadFingerprintThreshold) {
                candidates.push_back({function, root, score, features});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const PadFingerprintCandidate& lhs, const PadFingerprintCandidate& rhs) {
                  if (lhs.function != rhs.function) return lhs.function < rhs.function;
                  if (lhs.guest_pc != rhs.guest_pc) return lhs.guest_pc < rhs.guest_pc;
                  return lhs.score < rhs.score;
              });
    return candidates;
}

[[nodiscard]] inline std::vector<PadBindingEvidence>
make_ps2_pad_fingerprint_evidence(std::span<const PadFingerprintCandidate> candidates) {
    std::vector<PadBindingEvidence> evidence{};
    evidence.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        evidence.push_back({candidate.function,
                            PadBindingEvidenceKind::StaticFingerprint,
                            candidate.guest_pc,
                            candidate.score,
                            ps2_pad_fingerprint_detail::feature_detail(candidate.features)});
    }
    return evidence;
}

} // namespace b3r::analysis
