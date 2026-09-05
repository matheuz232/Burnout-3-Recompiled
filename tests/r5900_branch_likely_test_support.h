#pragma once

#include "r5900_direct_transfer_test_support.h"

namespace b3r::test_support {

inline R5900IrBlock branch_equal_likely(std::uint32_t pc,
                                        std::uint8_t rs,
                                        std::uint8_t rt,
                                        std::uint32_t target,
                                        R5900IrInstruction delay) {
    R5900IrBlock block{};
    block.terminator.guest_pc = pc;
    block.terminator.kind = R5900IrTerminatorKind::BranchEqualLikely64;
    block.terminator.inputs = {gpr(rs), gpr(rt)};
    block.terminator.taken_pc = target;
    block.terminator.fallthrough_pc = pc + 8u;
    block.terminator.delay_slot = {delay};
    return block;
}

inline R5900IrBlock branch_not_equal_likely(std::uint32_t pc,
                                            std::uint8_t rs,
                                            std::uint8_t rt,
                                            std::uint32_t target,
                                            R5900IrInstruction delay) {
    auto block = branch_equal_likely(pc, rs, rt, target, delay);
    block.terminator.kind = R5900IrTerminatorKind::BranchNotEqualLikely64;
    return block;
}

} // namespace b3r::test_support
