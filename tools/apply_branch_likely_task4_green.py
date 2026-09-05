from pathlib import Path

path = Path("src/recompiler/windows/r5900_block_dispatcher.cpp")
text = path.read_text(encoding="utf-8")

old = '''        const bool has_supported_bne =
            block.end_kind == analysis::R5900BlockEndKind::ConditionalBranch &&
            !block.instructions.empty() &&
            block.instructions.back().decoded.instruction == R5900Instruction::Bne;
        const bool has_supported_j =
'''
new = '''        const bool has_supported_bne =
            block.end_kind == analysis::R5900BlockEndKind::ConditionalBranch &&
            !block.instructions.empty() &&
            block.instructions.back().decoded.instruction == R5900Instruction::Bne;
        const bool has_supported_beql =
            block.end_kind == analysis::R5900BlockEndKind::ConditionalBranch &&
            !block.instructions.empty() &&
            block.instructions.back().decoded.instruction == R5900Instruction::Beql;
        const bool has_supported_bnel =
            block.end_kind == analysis::R5900BlockEndKind::ConditionalBranch &&
            !block.instructions.empty() &&
            block.instructions.back().decoded.instruction == R5900Instruction::Bnel;
        const bool has_supported_j =
'''
if old not in text:
    raise SystemExit("supported branch insertion anchor not found")
text = text.replace(old, new, 1)

old = '''        const bool has_supported_transfer =
            has_supported_beq || has_supported_bne || has_supported_j ||
            has_supported_jal || has_supported_jr || has_supported_jalr;
'''
new = '''        const bool has_supported_transfer =
            has_supported_beq || has_supported_bne || has_supported_beql ||
            has_supported_bnel || has_supported_j || has_supported_jal ||
            has_supported_jr || has_supported_jalr;
'''
if old not in text:
    raise SystemExit("supported transfer anchor not found")
text = text.replace(old, new, 1)

old = '''                if (has_supported_beq || has_supported_bne ||
                    has_supported_j || has_supported_jal) {
'''
new = '''                if (has_supported_beq || has_supported_bne ||
                    has_supported_beql || has_supported_bnel ||
                    has_supported_j || has_supported_jal) {
'''
if old not in text:
    raise SystemExit("direct target group anchor not found")
text = text.replace(old, new, 1)

old = '''                    if (has_supported_beq || has_supported_bne) {
                        ir_block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;
                        ir_block.terminator.inputs = {
                            dispatcher_gpr(transfer_site->decoded.rs),
                            dispatcher_gpr(transfer_site->decoded.rt),
                        };
                        if (has_supported_beq) {
                            ir_block.terminator.taken_pc = *target;
                            ir_block.terminator.fallthrough_pc = transfer_site->pc + 8u;
                        } else {
                            ir_block.terminator.taken_pc = transfer_site->pc + 8u;
                            ir_block.terminator.fallthrough_pc = *target;
                        }
                    } else {
'''
new = '''                    if (has_supported_beq || has_supported_bne ||
                        has_supported_beql || has_supported_bnel) {
                        ir_block.terminator.inputs = {
                            dispatcher_gpr(transfer_site->decoded.rs),
                            dispatcher_gpr(transfer_site->decoded.rt),
                        };
                        if (has_supported_beq) {
                            ir_block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;
                            ir_block.terminator.taken_pc = *target;
                            ir_block.terminator.fallthrough_pc = transfer_site->pc + 8u;
                        } else if (has_supported_bne) {
                            ir_block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;
                            ir_block.terminator.taken_pc = transfer_site->pc + 8u;
                            ir_block.terminator.fallthrough_pc = *target;
                        } else if (has_supported_beql) {
                            ir_block.terminator.kind =
                                R5900IrTerminatorKind::BranchEqualLikely64;
                            ir_block.terminator.taken_pc = *target;
                            ir_block.terminator.fallthrough_pc = transfer_site->pc + 8u;
                        } else {
                            ir_block.terminator.kind =
                                R5900IrTerminatorKind::BranchNotEqualLikely64;
                            ir_block.terminator.taken_pc = *target;
                            ir_block.terminator.fallthrough_pc = transfer_site->pc + 8u;
                        }
                    } else {
'''
if old not in text:
    raise SystemExit("branch lowering anchor not found")
text = text.replace(old, new, 1)

old = '''                        (has_supported_beq || has_supported_bne)
                            ? "SQ in a BEQ/BNE delay slot is outside dispatcher v0 scope"
'''
new = '''                        (has_supported_beq || has_supported_bne ||
                         has_supported_beql || has_supported_bnel)
                            ? "SQ in a BEQ/BNE/BEQL/BNEL delay slot is outside dispatcher v0 scope"
'''
if old not in text:
    raise SystemExit("SQ branch diagnostic anchor not found")
text = text.replace(old, new, 1)

path.write_text(text, encoding="utf-8")
