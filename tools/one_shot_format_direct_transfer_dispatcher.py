from pathlib import Path

path = Path('src/recompiler/windows/r5900_block_dispatcher.cpp')
text = path.read_text(encoding='utf-8')

old_words = '''        if (has_supported_transfer) {
    if (transfer_site == nullptr || !block.delay_slot.has_value()) {
        result.reason = R5900DispatchStopReason::AnalysisFailure;
        result.next_pc = current_pc;
        result.message = format_stage_error(
  "analysis",
  current_pc,
  "supported control transfer lacks terminator or delay slot");
        return result;
    }

    const auto transfer_word = memory_.read_u32(transfer_site->pc);
    if (!transfer_word.has_value()) {
        result.reason = R5900DispatchStopReason::AnalysisFailure;
        result.next_pc = transfer_site->pc;
        result.message = format_stage_error(
  "analysis",
  transfer_site->pc,
  "selected control transfer became unreadable");
        return result;
    }
    guest_words.push_back(*transfer_word);

    const auto delay_word = memory_.read_u32(block.delay_slot->pc);
    if (!delay_word.has_value()) {
        result.reason = R5900DispatchStopReason::AnalysisFailure;
        result.next_pc = block.delay_slot->pc;
        result.message = format_stage_error(
  "analysis", block.delay_slot->pc, "selected delay slot became unreadable");
        return result;
    }
    guest_words.push_back(*delay_word);
}
'''
new_words = '''        if (has_supported_transfer) {
            if (transfer_site == nullptr || !block.delay_slot.has_value()) {
                result.reason = R5900DispatchStopReason::AnalysisFailure;
                result.next_pc = current_pc;
                result.message = format_stage_error(
                    "analysis",
                    current_pc,
                    "supported control transfer lacks terminator or delay slot");
                return result;
            }

            const auto transfer_word = memory_.read_u32(transfer_site->pc);
            if (!transfer_word.has_value()) {
                result.reason = R5900DispatchStopReason::AnalysisFailure;
                result.next_pc = transfer_site->pc;
                result.message = format_stage_error(
                    "analysis",
                    transfer_site->pc,
                    "selected control transfer became unreadable");
                return result;
            }
            guest_words.push_back(*transfer_word);

            const auto delay_word = memory_.read_u32(block.delay_slot->pc);
            if (!delay_word.has_value()) {
                result.reason = R5900DispatchStopReason::AnalysisFailure;
                result.next_pc = block.delay_slot->pc;
                result.message = format_stage_error(
                    "analysis",
                    block.delay_slot->pc,
                    "selected delay slot became unreadable");
                return result;
            }
            guest_words.push_back(*delay_word);
        }
'''
if text.count(old_words) != 1:
    raise SystemExit('guest-word formatting anchor mismatch')
text = text.replace(old_words, new_words, 1)

old_ir = '''  if (has_supported_transfer) {
    if (transfer_site == nullptr || !block.delay_slot.has_value()) {
        result.reason = R5900DispatchStopReason::AnalysisFailure;
        result.next_pc = current_pc;
        result.message = format_stage_error(
  "analysis",
  current_pc,
  "supported control transfer lacks terminator or delay slot");
        return result;
    }

    const auto target =
        transfer_site->decoded.direct_target(transfer_site->pc);
    if (!target.has_value()) {
        result.reason = R5900DispatchStopReason::AnalysisFailure;
        result.next_pc = transfer_site->pc;
        result.message = format_stage_error(
  "analysis",
  transfer_site->pc,
  "decoded supported control transfer unexpectedly lacks direct target");
        return result;
    }

    ir_block.terminator.guest_pc = transfer_site->pc;
    ir_block.terminator.guest_raw = transfer_site->decoded.raw;
    if (has_supported_beq) {
        ir_block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;
        ir_block.terminator.inputs = {
  dispatcher_gpr(transfer_site->decoded.rs),
  dispatcher_gpr(transfer_site->decoded.rt),
        };
        ir_block.terminator.taken_pc = *target;
        ir_block.terminator.fallthrough_pc = transfer_site->pc + 8u;
    } else {
        ir_block.terminator.kind = has_supported_j
  ? R5900IrTerminatorKind::DirectJump
  : R5900IrTerminatorKind::DirectCall;
        ir_block.terminator.target_pc = *target;
        if (has_supported_jal) {
  ir_block.terminator.link_pc = transfer_site->pc + 8u;
        }
    }

    const auto& delay = *block.delay_slot;
    if (delay.decoded.instruction == R5900Instruction::Sq) {
        result.reason = R5900DispatchStopReason::LoweringFailure;
        result.next_pc = delay.pc;
        result.message = format_stage_error(
  "lowering",
  delay.pc,
  has_supported_beq
      ? "SQ in a BEQ delay slot is outside dispatcher v0 scope"
      : "SQ in a J/JAL delay slot is outside dispatcher v0 scope");
        return result;
    }

    const auto lowered_delay = lower_r5900_instruction(delay.decoded, delay.pc);
    if (!lowered_delay.ok() || lowered_delay.instructions.size() != 1u) {
        result.reason = R5900DispatchStopReason::LoweringFailure;
        result.next_pc = delay.pc;
        result.message = format_stage_error(
  "lowering",
  delay.pc,
  lowered_delay.ok()
      ? "control-transfer delay slot must lower to exactly one IR instruction"
      : lowered_delay.message);
        return result;
    }
    ir_block.terminator.delay_slot = lowered_delay.instructions;
} else {
    const auto fallthrough_pc =
        current_pc + static_cast<std::uint32_t>(body_sites.size() * 4u);
    ir_block.terminator.guest_pc = fallthrough_pc;
    ir_block.terminator.kind = R5900IrTerminatorKind::Fallthrough;
    ir_block.terminator.fallthrough_pc = fallthrough_pc;
}
'''
new_ir = '''            if (has_supported_transfer) {
                if (transfer_site == nullptr || !block.delay_slot.has_value()) {
                    result.reason = R5900DispatchStopReason::AnalysisFailure;
                    result.next_pc = current_pc;
                    result.message = format_stage_error(
                        "analysis",
                        current_pc,
                        "supported control transfer lacks terminator or delay slot");
                    return result;
                }

                const auto target =
                    transfer_site->decoded.direct_target(transfer_site->pc);
                if (!target.has_value()) {
                    result.reason = R5900DispatchStopReason::AnalysisFailure;
                    result.next_pc = transfer_site->pc;
                    result.message = format_stage_error(
                        "analysis",
                        transfer_site->pc,
                        "decoded supported control transfer unexpectedly lacks direct target");
                    return result;
                }

                ir_block.terminator.guest_pc = transfer_site->pc;
                ir_block.terminator.guest_raw = transfer_site->decoded.raw;
                if (has_supported_beq) {
                    ir_block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;
                    ir_block.terminator.inputs = {
                        dispatcher_gpr(transfer_site->decoded.rs),
                        dispatcher_gpr(transfer_site->decoded.rt),
                    };
                    ir_block.terminator.taken_pc = *target;
                    ir_block.terminator.fallthrough_pc = transfer_site->pc + 8u;
                } else {
                    ir_block.terminator.kind = has_supported_j
                        ? R5900IrTerminatorKind::DirectJump
                        : R5900IrTerminatorKind::DirectCall;
                    ir_block.terminator.target_pc = *target;
                    if (has_supported_jal) {
                        ir_block.terminator.link_pc = transfer_site->pc + 8u;
                    }
                }

                const auto& delay = *block.delay_slot;
                if (delay.decoded.instruction == R5900Instruction::Sq) {
                    result.reason = R5900DispatchStopReason::LoweringFailure;
                    result.next_pc = delay.pc;
                    result.message = format_stage_error(
                        "lowering",
                        delay.pc,
                        has_supported_beq
                            ? "SQ in a BEQ delay slot is outside dispatcher v0 scope"
                            : "SQ in a J/JAL delay slot is outside dispatcher v0 scope");
                    return result;
                }

                const auto lowered_delay = lower_r5900_instruction(delay.decoded, delay.pc);
                if (!lowered_delay.ok() || lowered_delay.instructions.size() != 1u) {
                    result.reason = R5900DispatchStopReason::LoweringFailure;
                    result.next_pc = delay.pc;
                    result.message = format_stage_error(
                        "lowering",
                        delay.pc,
                        lowered_delay.ok()
                            ? "control-transfer delay slot must lower to exactly one IR instruction"
                            : lowered_delay.message);
                    return result;
                }
                ir_block.terminator.delay_slot = lowered_delay.instructions;
            } else {
                const auto fallthrough_pc =
                    current_pc + static_cast<std::uint32_t>(body_sites.size() * 4u);
                ir_block.terminator.guest_pc = fallthrough_pc;
                ir_block.terminator.kind = R5900IrTerminatorKind::Fallthrough;
                ir_block.terminator.fallthrough_pc = fallthrough_pc;
            }
'''
if text.count(old_ir) != 1:
    raise SystemExit('IR formatting anchor mismatch')
text = text.replace(old_ir, new_ir, 1)

path.write_text(text, encoding='utf-8')
