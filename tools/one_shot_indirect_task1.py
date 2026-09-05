from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    if text.count(old) != 1:
        raise SystemExit(f"expected exactly one match in {path}: {old[:80]!r}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")


# Typed IR.
replace_once(
    "src/recompiler/r5900_ir.h",
    """enum class R5900IrTerminatorKind {\n    Fallthrough = 0,\n    BranchEqual64,\n    DirectJump,\n    DirectCall,\n};""",
    """enum class R5900IrTerminatorKind {\n    Fallthrough = 0,\n    BranchEqual64,\n    DirectJump,\n    DirectCall,\n    IndirectJump,\n    IndirectCall,\n};""",
)
replace_once(
    "src/recompiler/r5900_ir.h",
    """    std::uint32_t target_pc{};\n    std::uint32_t link_pc{};\n    std::vector<R5900IrInstruction> delay_slot{};""",
    """    std::uint32_t target_pc{};\n    std::uint32_t link_pc{};\n    std::uint8_t link_gpr{};\n    std::vector<R5900IrInstruction> delay_slot{};""",
)

# Shared test helpers.
replace_once(
    "tests/r5900_direct_transfer_test_support.h",
    "\n} // namespace b3r::test_support\n",
    """
inline R5900IrBlock indirect_jump(std::uint32_t pc,
                                  std::uint8_t rs,
                                  R5900IrInstruction delay) {
    R5900IrBlock block{};
    block.terminator.guest_pc = pc;
    block.terminator.kind = R5900IrTerminatorKind::IndirectJump;
    block.terminator.inputs = {gpr(rs)};
    block.terminator.delay_slot = {delay};
    return block;
}

inline R5900IrBlock indirect_call(std::uint32_t pc,
                                  std::uint8_t rs,
                                  std::uint8_t rd,
                                  R5900IrInstruction delay) {
    auto block = indirect_jump(pc, rs, delay);
    block.terminator.kind = R5900IrTerminatorKind::IndirectCall;
    block.terminator.link_pc = pc + 8u;
    block.terminator.link_gpr = rd;
    return block;
}

} // namespace b3r::test_support
""",
)

# Existing terminators reject link_gpr.
replace_once(
    "src/recompiler/r5900_ir_validation.cpp",
    """            terminator.taken_pc != 0u ||\n            terminator.target_pc != 0u ||\n            terminator.link_pc != 0u) {""",
    """            terminator.taken_pc != 0u ||\n            terminator.target_pc != 0u ||\n            terminator.link_pc != 0u ||\n            terminator.link_gpr != 0u) {""",
)
replace_once(
    "src/recompiler/r5900_ir_validation.cpp",
    """            terminator.target_pc != 0u ||\n            terminator.link_pc != 0u ||\n            terminator.inputs.size() != 2u ||""",
    """            terminator.target_pc != 0u ||\n            terminator.link_pc != 0u ||\n            terminator.link_gpr != 0u ||\n            terminator.inputs.size() != 2u ||""",
)
replace_once(
    "src/recompiler/r5900_ir_validation.cpp",
    """            terminator.fallthrough_pc != 0u ||\n            terminator.link_pc != 0u ||\n            (terminator.target_pc & 0x3u) != 0u) {""",
    """            terminator.fallthrough_pc != 0u ||\n            terminator.link_pc != 0u ||\n            terminator.link_gpr != 0u ||\n            (terminator.target_pc & 0x3u) != 0u) {""",
)
replace_once(
    "src/recompiler/r5900_ir_validation.cpp",
    """            terminator.fallthrough_pc != 0u ||\n            (terminator.target_pc & 0x3u) != 0u ||\n            (terminator.link_pc & 0x3u) != 0u ||""",
    """            terminator.fallthrough_pc != 0u ||\n            terminator.link_gpr != 0u ||\n            (terminator.target_pc & 0x3u) != 0u ||\n            (terminator.link_pc & 0x3u) != 0u ||""",
)

# Indirect validator cases.
replace_once(
    "src/recompiler/r5900_ir_validation.cpp",
    """        return validate_single_delay_slot(terminator, terminator_index);\n\n    default:\n        return failure(R5900IrValidationError::UnsupportedOpcode,""",
    """        return validate_single_delay_slot(terminator, terminator_index);

    case R5900IrTerminatorKind::IndirectJump: {
        if (terminator.inputs.size() != 1u ||
            terminator.inputs[0].kind != R5900IrOperandKind::Gpr ||
            terminator.taken_pc != 0u ||
            terminator.fallthrough_pc != 0u ||
            terminator.target_pc != 0u ||
            terminator.link_pc != 0u ||
            terminator.link_gpr != 0u) {
            return failure(R5900IrValidationError::MalformedInstruction,
                           terminator_index,
                           terminator.guest_pc,
                           "malformed indirect-jump terminator");
        }
        const auto input = validate_operand(
            terminator.inputs[0], terminator_index, terminator.guest_pc);
        if (!input.ok()) {
            return input;
        }
        return validate_single_delay_slot(terminator, terminator_index);
    }

    case R5900IrTerminatorKind::IndirectCall: {
        if (terminator.link_gpr >= 32u) {
            return failure(R5900IrValidationError::InvalidRegister,
                           terminator_index,
                           terminator.guest_pc,
                           "invalid indirect-call link GPR");
        }
        if (terminator.inputs.size() != 1u ||
            terminator.inputs[0].kind != R5900IrOperandKind::Gpr ||
            terminator.taken_pc != 0u ||
            terminator.fallthrough_pc != 0u ||
            terminator.target_pc != 0u ||
            (terminator.link_pc & 0x3u) != 0u ||
            terminator.link_pc !=
                static_cast<std::uint32_t>(terminator.guest_pc + 8u)) {
            return failure(R5900IrValidationError::MalformedInstruction,
                           terminator_index,
                           terminator.guest_pc,
                           "malformed indirect-call terminator");
        }
        const auto input = validate_operand(
            terminator.inputs[0], terminator_index, terminator.guest_pc);
        if (!input.ok()) {
            return input;
        }
        return validate_single_delay_slot(terminator, terminator_index);
    }

    default:
        return failure(R5900IrValidationError::UnsupportedOpcode,""",
)

# Dedicated CTest target.
replace_once(
    "CMakeLists.txt",
    """  add_test(NAME r5900_ir_direct_transfer_validation_tests\n    COMMAND r5900_ir_direct_transfer_validation_tests)\n\n  add_executable(r5900_ir_direct_transfer_executor_tests""",
    """  add_test(NAME r5900_ir_direct_transfer_validation_tests
    COMMAND r5900_ir_direct_transfer_validation_tests)

  add_executable(r5900_ir_indirect_transfer_validation_tests
    tests/r5900_ir_indirect_transfer_validation_tests.cpp
  )
  target_link_libraries(r5900_ir_indirect_transfer_validation_tests PRIVATE b3r_recompiler)
  add_test(NAME r5900_ir_indirect_transfer_validation_tests
    COMMAND r5900_ir_indirect_transfer_validation_tests)

  add_executable(r5900_ir_direct_transfer_executor_tests""",
)

# Remove the temporary RED probe now that dedicated coverage is registered.
replace_once(
    "tests/r5900_ir_direct_transfer_validation_tests.cpp",
    """    {
        R5900IrBlock indirect_jump_probe{};
        indirect_jump_probe.terminator.guest_pc = 0x00108000u;
        indirect_jump_probe.terminator.kind = R5900IrTerminatorKind::IndirectJump;
        indirect_jump_probe.terminator.inputs = {gpr(5u)};
        indirect_jump_probe.terminator.link_gpr = 0u;
        indirect_jump_probe.terminator.delay_slot = {nop(0x00108004u)};
        expect(validate_r5900_ir_block(indirect_jump_probe).ok(),
               "valid IndirectJump must validate");

        auto indirect_call_probe = indirect_jump_probe;
        indirect_call_probe.terminator.guest_pc = 0x00108200u;
        indirect_call_probe.terminator.kind = R5900IrTerminatorKind::IndirectCall;
        indirect_call_probe.terminator.link_pc = 0x00108208u;
        indirect_call_probe.terminator.link_gpr = 9u;
        indirect_call_probe.terminator.delay_slot = {nop(0x00108204u)};
        expect(validate_r5900_ir_block(indirect_call_probe).ok(),
               "valid IndirectCall must validate");
    }

""",
    "",
)

print("Task 1 patch applied")
