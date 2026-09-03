#include "analysis/r5900_control_flow.h"
#include "recompiler/ps2_elf.h"
#include "runtime/ps2_memory_map.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_control_flow_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

constexpr std::uint32_t r_type(std::uint8_t rs,
                               std::uint8_t rt,
                               std::uint8_t rd,
                               std::uint8_t sa,
                               std::uint8_t funct) {
    return (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) |
           funct;
}

constexpr std::uint32_t i_type(std::uint8_t op, std::uint8_t rs, std::uint8_t rt, std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           imm;
}

b3r::runtime::Ps2MemoryMap make_memory(const std::vector<std::uint32_t>& words,
                                        std::uint32_t flags = 5u,
                                        std::uint32_t guest_base = 0x1000u) {
    constexpr std::uint32_t kProgramHeaderOffset = 52u;
    constexpr std::uint32_t kSegmentOffset = 0x100u;
    const auto payload_size = static_cast<std::uint32_t>(words.size() * 4u);

    Bytes bytes(kSegmentOffset + payload_size, 0u);
    bytes[0] = 0x7F;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 1;
    bytes[5] = 1;
    bytes[6] = 1;
    put_u16(bytes, 16, 2);
    put_u16(bytes, 18, 8);
    put_u32(bytes, 20, 1);
    put_u32(bytes, 24, guest_base);
    put_u32(bytes, 28, kProgramHeaderOffset);
    put_u16(bytes, 40, 52);
    put_u16(bytes, 42, 32);
    put_u16(bytes, 44, 1);

    put_u32(bytes, kProgramHeaderOffset + 0, 1);
    put_u32(bytes, kProgramHeaderOffset + 4, kSegmentOffset);
    put_u32(bytes, kProgramHeaderOffset + 8, guest_base);
    put_u32(bytes, kProgramHeaderOffset + 12, guest_base);
    put_u32(bytes, kProgramHeaderOffset + 16, payload_size);
    put_u32(bytes, kProgramHeaderOffset + 20, payload_size);
    put_u32(bytes, kProgramHeaderOffset + 24, flags);
    put_u32(bytes, kProgramHeaderOffset + 28, 0x1000);

    for (std::size_t i = 0; i < words.size(); ++i) {
        put_u32(bytes, kSegmentOffset + (i * 4u), words[i]);
    }

    const auto elf_result = b3r::recompiler::parse_ps2_elf(bytes);
    expect(elf_result.ok(), "synthetic ELF must parse");
    const auto map_result = b3r::runtime::Ps2MemoryMap::from_elf(*elf_result.image);
    expect(map_result.ok(), "synthetic ELF memory map must build");
    return std::move(*map_result.memory);
}

} // namespace

int main() {
    using namespace b3r::analysis;
    using b3r::recompiler::R5900Instruction;

    {
        auto memory = make_memory({
            0u,
            i_type(0x09, 29, 29, 0xFFF0),
            i_type(0x04, 4, 0, 2),
            0u,
            0u,
            0u,
        });
        const auto result = analyze_r5900_basic_block(memory, 0x1000u);
        expect(result.ok(), "conditional branch block must analyze");
        const auto& block = *result.block;
        expect(block.instructions.size() == 3u, "linear body must include terminator but exclude delay slot");
        expect(block.instructions.back().decoded.instruction == R5900Instruction::Beq, "BEQ must terminate block");
        expect(block.delay_slot.has_value() && block.delay_slot->pc == 0x100Cu, "delay slot must be stored separately");
        expect(block.end_kind == R5900BlockEndKind::ConditionalBranch, "BEQ block end kind must be conditional");
        expect(block.delay_slot_executes_on_fallthrough, "normal branch delay slot executes on both paths");
        expect(block.edges.size() == 2u, "conditional branch must expose two edges");
        expect(block.edges[0].kind == R5900EdgeKind::BranchTaken && block.edges[0].target == 0x1014u,
               "taken edge must use decoded branch target");
        expect(block.edges[1].kind == R5900EdgeKind::BranchNotTaken && block.edges[1].target == 0x1010u,
               "not-taken edge must skip branch and delay slot");
    }

    {
        auto memory = make_memory({i_type(0x14, 8, 9, 3), 0u, 0u, 0u, 0u});
        const auto result = analyze_r5900_basic_block(memory, 0x1000u);
        expect(result.ok(), "branch-likely block must analyze");
        expect(result.block->end_kind == R5900BlockEndKind::ConditionalBranch, "BEQL must be conditional");
        expect(!result.block->delay_slot_executes_on_fallthrough,
               "branch-likely delay slot must be marked annulled on not-taken path");
        expect(result.block->edges[0].target == 0x1010u, "branch-likely taken target must resolve");
        expect(result.block->edges[1].target == 0x1008u, "branch-likely not-taken edge must skip delay slot");
    }

    {
        const std::uint32_t j = (0x02u << 26u) | ((0x00001200u >> 2u) & 0x03FFFFFFu);
        auto memory = make_memory({j, 0u});
        const auto result = analyze_r5900_basic_block(memory, 0x1000u);
        expect(result.ok(), "direct jump block must analyze");
        expect(result.block->end_kind == R5900BlockEndKind::DirectJump, "J must end as direct jump");
        expect(result.block->edges.size() == 1u && result.block->edges[0].kind == R5900EdgeKind::DirectJump,
               "J must expose one direct jump edge");
        expect(result.block->edges[0].target == 0x1200u, "J target must be retained even when destination is not mapped");
    }

    {
        const std::uint32_t jal = (0x03u << 26u) | ((0x00001800u >> 2u) & 0x03FFFFFFu);
        auto memory = make_memory({jal, 0u});
        const auto result = analyze_r5900_basic_block(memory, 0x1000u);
        expect(result.ok(), "direct call block must analyze");
        expect(result.block->end_kind == R5900BlockEndKind::DirectCall, "JAL must end as direct call");
        expect(result.block->edges.size() == 2u, "JAL must expose call and continuation edges");
        expect(result.block->edges[0].kind == R5900EdgeKind::DirectCall && result.block->edges[0].target == 0x1800u,
               "JAL call edge must resolve");
        expect(result.block->edges[1].kind == R5900EdgeKind::CallContinuation && result.block->edges[1].target == 0x1008u,
               "JAL continuation must start after delay slot");
    }

    {
        auto memory = make_memory({r_type(31, 0, 0, 0, 0x08), 0u});
        const auto result = analyze_r5900_basic_block(memory, 0x1000u);
        expect(result.ok(), "JR block must analyze");
        expect(result.block->end_kind == R5900BlockEndKind::IndirectJump, "JR must be indirect jump");
        expect(result.block->edges.size() == 1u && result.block->edges[0].kind == R5900EdgeKind::IndirectJump,
               "JR must expose an indirect edge");
        expect(!result.block->edges[0].target.has_value(), "JR edge must not invent a target");
    }

    {
        auto memory = make_memory({r_type(25, 0, 31, 0, 0x09), 0u});
        const auto result = analyze_r5900_basic_block(memory, 0x1000u);
        expect(result.ok(), "JALR block must analyze");
        expect(result.block->end_kind == R5900BlockEndKind::IndirectCall, "JALR must be indirect call");
        expect(result.block->edges.size() == 2u, "JALR must expose indirect call and continuation");
        expect(result.block->edges[0].kind == R5900EdgeKind::IndirectCall && !result.block->edges[0].target,
               "JALR call target must remain unresolved");
        expect(result.block->edges[1].kind == R5900EdgeKind::CallContinuation && result.block->edges[1].target == 0x1008u,
               "JALR continuation must start after delay slot");
    }

    {
        auto memory = make_memory({r_type(0, 0, 0, 0, 0x0D)});
        const auto result = analyze_r5900_basic_block(memory, 0x1000u);
        expect(result.ok(), "BREAK block must analyze");
        expect(result.block->end_kind == R5900BlockEndKind::Trap, "BREAK must end as trap");
        expect(result.block->edges.empty(), "trap must have no normal successor");
    }

    {
        auto memory = make_memory({0x70000000u});
        const auto result = analyze_r5900_basic_block(memory, 0x1000u);
        expect(result.ok(), "unsupported instruction must produce evidence rather than crash analysis");
        expect(result.block->end_kind == R5900BlockEndKind::UnsupportedInstruction,
               "Unknown instruction must conservatively terminate the block");
        expect(result.block->instructions.size() == 1u, "unknown terminator must remain visible in block evidence");
        expect(result.block->edges.empty(), "unsupported instruction must not invent successors");
    }

    {
        auto memory = make_memory({0u, 0u, 0u});
        R5900ControlFlowOptions options{};
        options.max_instructions = 2u;
        const auto result = analyze_r5900_basic_block(memory, 0x1000u, options);
        expect(result.ok(), "instruction limit must produce a bounded block");
        expect(result.block->end_kind == R5900BlockEndKind::InstructionLimit, "limit must be explicit");
        expect(result.block->instructions.size() == 2u, "limit must cap linear instructions");
        expect(result.block->edges.size() == 1u && result.block->edges[0].kind == R5900EdgeKind::Fallthrough &&
                   result.block->edges[0].target == 0x1008u,
               "instruction-limit chunk must preserve fallthrough reachability");
    }

    {
        auto memory = make_memory({0u});
        const auto result = analyze_r5900_basic_block(memory, 0x1002u);
        expect(result.error == R5900ControlFlowError::UnalignedStart && !result.ok(),
               "unaligned start PC must be rejected");
    }

    {
        auto memory = make_memory({0u}, 6u);
        const auto result = analyze_r5900_basic_block(memory, 0x1000u);
        expect(result.error == R5900ControlFlowError::NonExecutableInstruction && !result.ok(),
               "analysis must reject non-executable segment bytes by default");
    }

    {
        const std::uint32_t j = (0x02u << 26u) | ((0x00001200u >> 2u) & 0x03FFFFFFu);
        auto memory = make_memory({j});
        const auto result = analyze_r5900_basic_block(memory, 0x1000u);
        expect(result.error == R5900ControlFlowError::MissingDelaySlot && !result.ok(),
               "control transfer without mapped delay slot must be rejected");
    }

    std::cout << "r5900_control_flow_tests: PASS\n";
    return EXIT_SUCCESS;
}
