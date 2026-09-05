#pragma once

#include "recompiler/r5900_ir.h"

#include <cstdint>

namespace b3r::test_support {

using namespace b3r::recompiler;

inline R5900IrOperand gpr(std::uint8_t index) {
    R5900IrOperand value{};
    value.kind = R5900IrOperandKind::Gpr;
    value.gpr_index = index;
    return value;
}

inline R5900IrOperand immediate(std::int64_t value) {
    R5900IrOperand operand{};
    operand.kind = R5900IrOperandKind::Immediate;
    operand.immediate = value;
    return operand;
}

inline R5900IrInstruction nop(std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::Nop;
    return ir;
}

inline R5900IrInstruction addiu(std::uint8_t rt,
                                std::uint8_t rs,
                                std::int16_t imm,
                                std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::AddWordSignExtend;
    ir.destination = R5900IrDestination{R5900IrDestinationKind::Gpr, rt};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
    ir.inputs = {gpr(rs), immediate(imm)};
    return ir;
}

inline R5900IrInstruction store128(std::uint8_t base,
                                   std::uint8_t source,
                                   std::int16_t imm,
                                   std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::Store128;
    ir.inputs = {gpr(base), gpr(source), immediate(imm)};
    return ir;
}

inline R5900IrBlock direct_jump(std::uint32_t pc,
                                std::uint32_t target,
                                R5900IrInstruction delay) {
    R5900IrBlock block{};
    block.terminator.guest_pc = pc;
    block.terminator.kind = R5900IrTerminatorKind::DirectJump;
    block.terminator.target_pc = target;
    block.terminator.delay_slot = {delay};
    return block;
}

inline R5900IrBlock direct_call(std::uint32_t pc,
                                std::uint32_t target,
                                R5900IrInstruction delay) {
    auto block = direct_jump(pc, target, delay);
    block.terminator.kind = R5900IrTerminatorKind::DirectCall;
    block.terminator.link_pc = pc + 8u;
    return block;
}

} // namespace b3r::test_support
