from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    if text.count(old) != 1:
        raise SystemExit(f"expected one match in {path}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


# Recognize JR/JALR as supported final transfers.
replace_once(
    "src/recompiler/windows/r5900_block_dispatcher.cpp",
    """        const bool has_supported_jal =\n            block.end_kind == analysis::R5900BlockEndKind::DirectCall &&\n            !block.instructions.empty() &&\n            block.instructions.back().decoded.instruction == R5900Instruction::Jal;\n        const bool has_supported_transfer =\n            has_supported_beq || has_supported_j || has_supported_jal;""",
    """        const bool has_supported_jal =
            block.end_kind == analysis::R5900BlockEndKind::DirectCall &&
            !block.instructions.empty() &&
            block.instructions.back().decoded.instruction == R5900Instruction::Jal;
        const bool has_supported_jr =
            block.end_kind == analysis::R5900BlockEndKind::IndirectJump &&
            !block.instructions.empty() &&
            block.instructions.back().decoded.instruction == R5900Instruction::Jr;
        const bool has_supported_jalr =
            block.end_kind == analysis::R5900BlockEndKind::IndirectCall &&
            !block.instructions.empty() &&
            block.instructions.back().decoded.instruction == R5900Instruction::Jalr;
        const bool has_supported_transfer =
            has_supported_beq || has_supported_j || has_supported_jal ||
            has_supported_jr || has_supported_jalr;""",
)

# Split direct and indirect terminator lowering.
replace_once(
    "src/recompiler/windows/r5900_block_dispatcher.cpp",
    """                const auto target =\n                    transfer_site->decoded.direct_target(transfer_site->pc);\n                if (!target.has_value()) {\n                    result.reason = R5900DispatchStopReason::AnalysisFailure;\n                    result.next_pc = transfer_site->pc;\n                    result.message = format_stage_error(\n                        \"analysis\",\n                        transfer_site->pc,\n                        \"decoded supported control transfer unexpectedly lacks direct target\");\n                    return result;\n                }\n\n                ir_block.terminator.guest_pc = transfer_site->pc;\n                ir_block.terminator.guest_raw = transfer_site->decoded.raw;\n                if (has_supported_beq) {\n                    ir_block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;\n                    ir_block.terminator.inputs = {\n                        dispatcher_gpr(transfer_site->decoded.rs),\n                        dispatcher_gpr(transfer_site->decoded.rt),\n                    };\n                    ir_block.terminator.taken_pc = *target;\n                    ir_block.terminator.fallthrough_pc = transfer_site->pc + 8u;\n                } else {\n                    ir_block.terminator.kind = has_supported_j\n                        ? R5900IrTerminatorKind::DirectJump\n                        : R5900IrTerminatorKind::DirectCall;\n                    ir_block.terminator.target_pc = *target;\n                    if (has_supported_jal) {\n                        ir_block.terminator.link_pc = transfer_site->pc + 8u;\n                    }\n                }""",
    """                ir_block.terminator.guest_pc = transfer_site->pc;
                ir_block.terminator.guest_raw = transfer_site->decoded.raw;

                if (has_supported_beq || has_supported_j || has_supported_jal) {
                    const auto target =
                        transfer_site->decoded.direct_target(transfer_site->pc);
                    if (!target.has_value()) {
                        result.reason = R5900DispatchStopReason::AnalysisFailure;
                        result.next_pc = transfer_site->pc;
                        result.message = format_stage_error(
                            "analysis",
                            transfer_site->pc,
                            "decoded supported direct control transfer unexpectedly lacks target");
                        return result;
                    }

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
                } else {
                    ir_block.terminator.kind = has_supported_jr
                        ? R5900IrTerminatorKind::IndirectJump
                        : R5900IrTerminatorKind::IndirectCall;
                    ir_block.terminator.inputs = {
                        dispatcher_gpr(transfer_site->decoded.rs),
                    };
                    if (has_supported_jalr) {
                        ir_block.terminator.link_gpr = transfer_site->decoded.rd;
                        ir_block.terminator.link_pc = transfer_site->pc + 8u;
                    }
                }""",
)

# Generalize SQ delay diagnostic.
replace_once(
    "src/recompiler/windows/r5900_block_dispatcher.cpp",
    """                        has_supported_beq\n                            ? \"SQ in a BEQ delay slot is outside dispatcher v0 scope\"\n                            : \"SQ in a J/JAL delay slot is outside dispatcher v0 scope\");""",
    """                        has_supported_beq
                            ? "SQ in a BEQ delay slot is outside dispatcher v0 scope"
                            : (has_supported_j || has_supported_jal)
                                ? "SQ in a J/JAL delay slot is outside dispatcher v0 scope"
                                : "SQ in a JR/JALR delay slot is outside dispatcher v0 scope");""",
)

# Direct-transfer tests must use BNE as the unsupported sentinel now.
direct_path = Path("tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp")
text = direct_path.read_text(encoding="utf-8")
text = text.replace("const auto jr31 = r_type(31u, 0u, 0u, 0u, 0x08u);",
                    "const auto bne_boundary = i_type(0x05u, 0u, 0u, 0u);")
text = text.replace("jr31, 0u,", "bne_boundary, 0u,")
text = text.replace("jr31, 0u,\n", "bne_boundary, 0u,\n")
old_loop = """    {
        const std::vector<std::uint32_t> indirect = {
            r_type(31u, 0u, 0u, 0u, 0x08u),
            r_type(31u, 0u, 30u, 0u, 0x09u),
        };
        for (const auto transfer : indirect) {
            auto memory = make_memory({transfer, i_type(0x09u, 0u, 9u, 7u)}, base);
            R5900BlockDispatcher dispatcher(memory);
            R5900IrExecutionState state{};
            const auto result = dispatcher.run(base, state, 1u);
            expect(result.reason == R5900DispatchStopReason::ControlFlow &&
                       result.next_pc == base && result.blocks_executed == 0u &&
                       result.instructions_executed == 0u,
                   "entry JR/JALR must remain unsupported control-flow boundary");
            expect(state.gpr[9].low64 == 0u,
                   "JR/JALR mapped delay must not execute");
        }
    }

"""
if text.count(old_loop) != 1:
    raise SystemExit("obsolete JR/JALR unsupported loop not found")
text = text.replace(old_loop, "", 1)
direct_path.write_text(text, encoding="utf-8")

print("Task 4 GREEN patch applied")
