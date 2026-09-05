from pathlib import Path

path = Path('src/recompiler/windows/r5900_block_dispatcher.cpp')
text = path.read_text(encoding='utf-8')

replacements = [
    (
'''        const bool has_supported_beq =\n            block.end_kind == analysis::R5900BlockEndKind::ConditionalBranch &&\n            !block.instructions.empty() &&\n            block.instructions.back().decoded.instruction == R5900Instruction::Beq;\n        const bool has_supported_j =\n''',
'''        const bool has_supported_beq =\n            block.end_kind == analysis::R5900BlockEndKind::ConditionalBranch &&\n            !block.instructions.empty() &&\n            block.instructions.back().decoded.instruction == R5900Instruction::Beq;\n        const bool has_supported_bne =\n            block.end_kind == analysis::R5900BlockEndKind::ConditionalBranch &&\n            !block.instructions.empty() &&\n            block.instructions.back().decoded.instruction == R5900Instruction::Bne;\n        const bool has_supported_j =\n'''),
    (
'''        const bool has_supported_transfer =\n            has_supported_beq || has_supported_j || has_supported_jal ||\n            has_supported_jr || has_supported_jalr;\n''',
'''        const bool has_supported_transfer =\n            has_supported_beq || has_supported_bne || has_supported_j ||\n            has_supported_jal || has_supported_jr || has_supported_jalr;\n'''),
    (
'''                if (has_supported_beq || has_supported_j || has_supported_jal) {\n''',
'''                if (has_supported_beq || has_supported_bne ||\n                    has_supported_j || has_supported_jal) {\n'''),
    (
'''                    if (has_supported_beq) {\n                        ir_block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;\n                        ir_block.terminator.inputs = {\n                            dispatcher_gpr(transfer_site->decoded.rs),\n                            dispatcher_gpr(transfer_site->decoded.rt),\n                        };\n                        ir_block.terminator.taken_pc = *target;\n                        ir_block.terminator.fallthrough_pc = transfer_site->pc + 8u;\n                    } else {\n''',
'''                    if (has_supported_beq || has_supported_bne) {\n                        ir_block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;\n                        ir_block.terminator.inputs = {\n                            dispatcher_gpr(transfer_site->decoded.rs),\n                            dispatcher_gpr(transfer_site->decoded.rt),\n                        };\n                        if (has_supported_beq) {\n                            ir_block.terminator.taken_pc = *target;\n                            ir_block.terminator.fallthrough_pc = transfer_site->pc + 8u;\n                        } else {\n                            ir_block.terminator.taken_pc = transfer_site->pc + 8u;\n                            ir_block.terminator.fallthrough_pc = *target;\n                        }\n                    } else {\n'''),
]

for old, new in replacements:
    if text.count(old) != 1:
        raise SystemExit(f'expected exactly one dispatcher patch site, found {text.count(old)}')
    text = text.replace(old, new, 1)

path.write_text(text, encoding='utf-8')
