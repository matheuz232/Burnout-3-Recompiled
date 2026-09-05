from pathlib import Path

backend = Path("src/recompiler/windows/r5900_x64_backend.cpp")
text = backend.read_text(encoding="utf-8")

anchor = """PendingX64Code compile_direct_transfer_code(const R5900IrBlock& block) {\n"""
addition = r'''PendingX64Code compile_likely_branch_code(const R5900IrBlock& block) {
    const bool helper_frame =
        sequence_needs_helper(block.body) ||
        sequence_needs_helper(block.terminator.delay_slot);

    std::vector<std::uint8_t> bytes;
    bytes.reserve(160u +
                  block.body.size() * 128u +
                  block.terminator.delay_slot.size() * 256u);

    if (helper_frame) {
        emit_helper_frame_prologue(bytes);
    }
    emit_zero_gpr0(bytes);

    const auto body_emitted = emit_ir_sequence(bytes,
                                               block.body,
                                               0u,
                                               helper_frame);
    if (!body_emitted.ok()) {
        return pending_failure(body_emitted.error, body_emitted.message);
    }

    emit_load_rax_from_state(
        bytes,
        gpr_low64_offset(block.terminator.inputs[0].gpr_index));
    emit_load_rdx_from_state(
        bytes,
        gpr_low64_offset(block.terminator.inputs[1].gpr_index));
    bytes.insert(bytes.end(), {0x48u, 0x39u, 0xd0u}); // cmp rax, rdx

    // Branch around the complete architectural delay path when the likely
    // predicate is not satisfied. BEQL is not-taken on !=; BNEL on ==.
    bytes.insert(bytes.end(), {0x0fu});
    bytes.push_back(
        block.terminator.kind == R5900IrTerminatorKind::BranchEqualLikely64
            ? 0x85u // jne rel32
            : 0x84u); // je rel32
    const auto not_taken_rel32_offset = bytes.size();
    emit_u32(bytes, 0u);

    emit_zero_gpr0(bytes);
    const auto delay_emitted = emit_ir_sequence(
        bytes,
        block.terminator.delay_slot,
        block.body.size() + 1u,
        helper_frame);
    if (!delay_emitted.ok()) {
        return pending_failure(delay_emitted.error, delay_emitted.message);
    }
    emit_zero_gpr0(bytes);
    emit_mov_eax_imm32(bytes, block.terminator.taken_pc);
    if (helper_frame) {
        emit_helper_frame_epilogue(bytes);
    }
    bytes.push_back(0xc3u);

    const auto not_taken_offset = bytes.size();
    const auto branch_end = not_taken_rel32_offset + sizeof(std::uint32_t);
    const auto displacement =
        static_cast<std::int64_t>(not_taken_offset) -
        static_cast<std::int64_t>(branch_end);
    if (displacement < std::numeric_limits<std::int32_t>::min() ||
        displacement > std::numeric_limits<std::int32_t>::max()) {
        return pending_failure(R5900X64CompileError::UnsupportedOpcode,
                               "R5900 x64 likely-branch path exceeds rel32 range");
    }
    patch_u32(bytes,
              not_taken_rel32_offset,
              static_cast<std::uint32_t>(
                  static_cast<std::int32_t>(displacement)));

    emit_zero_gpr0(bytes);
    emit_mov_eax_imm32(bytes, block.terminator.fallthrough_pc);
    if (helper_frame) {
        emit_helper_frame_epilogue(bytes);
    }
    bytes.push_back(0xc3u);

    return publish_code(bytes);
}

'''
if anchor not in text:
    raise SystemExit("likely emitter insertion anchor not found")
text = text.replace(anchor, addition + anchor, 1)

switch_anchor = """    case R5900IrTerminatorKind::BranchEqual64:\n        pending = compile_branch_equal_code(block);\n        break;\n"""
switch_addition = """    case R5900IrTerminatorKind::BranchEqual64:\n        pending = compile_branch_equal_code(block);\n        break;\n    case R5900IrTerminatorKind::BranchEqualLikely64:\n    case R5900IrTerminatorKind::BranchNotEqualLikely64:\n        pending = compile_likely_branch_code(block);\n        break;\n"""
if switch_anchor not in text:
    raise SystemExit("x64 compile switch anchor not found")
text = text.replace(switch_anchor, switch_addition, 1)
backend.write_text(text, encoding="utf-8")
