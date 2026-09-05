from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    if text.count(old) != 1:
        raise SystemExit(f"expected exactly one match in {path}: {old[:100]!r}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "src/recompiler/r5900_ir_executor.cpp",
    """    case R5900IrTerminatorKind::DirectCall: {\n        state.gpr[31].low64 =\n            static_cast<std::uint64_t>(block.terminator.link_pc);\n        normalize_zero(state);\n        const auto delay_result =\n            execute_ir_sequence(block.terminator.delay_slot, context);\n        if (!delay_result.ok()) {\n            return map_block_execution_failure(delay_result);\n        }\n        return {R5900IrExecutionError::None,\n                {},\n                block.terminator.target_pc};\n    }\n\n    default:""",
    """    case R5900IrTerminatorKind::DirectCall: {
        state.gpr[31].low64 =
            static_cast<std::uint64_t>(block.terminator.link_pc);
        normalize_zero(state);
        const auto delay_result =
            execute_ir_sequence(block.terminator.delay_slot, context);
        if (!delay_result.ok()) {
            return map_block_execution_failure(delay_result);
        }
        return {R5900IrExecutionError::None,
                {},
                block.terminator.target_pc};
    }

    case R5900IrTerminatorKind::IndirectJump: {
        const auto target = static_cast<std::uint32_t>(
            state.gpr[block.terminator.inputs[0].gpr_index].low64);
        const auto delay_result =
            execute_ir_sequence(block.terminator.delay_slot, context);
        if (!delay_result.ok()) {
            return map_block_execution_failure(delay_result);
        }
        return {R5900IrExecutionError::None, {}, target};
    }

    case R5900IrTerminatorKind::IndirectCall: {
        const auto target = static_cast<std::uint32_t>(
            state.gpr[block.terminator.inputs[0].gpr_index].low64);
        const auto link_gpr = block.terminator.link_gpr;
        if (link_gpr != 0u) {
            state.gpr[link_gpr].low64 =
                static_cast<std::uint64_t>(block.terminator.link_pc);
        }
        normalize_zero(state);
        const auto delay_result =
            execute_ir_sequence(block.terminator.delay_slot, context);
        if (!delay_result.ok()) {
            return map_block_execution_failure(delay_result);
        }
        return {R5900IrExecutionError::None, {}, target};
    }

    default:""",
)

replace_once(
    "CMakeLists.txt",
    """  add_test(NAME r5900_ir_direct_transfer_executor_tests\n    COMMAND r5900_ir_direct_transfer_executor_tests)\n\n  add_executable(ps2_memory_map_tests""",
    """  add_test(NAME r5900_ir_direct_transfer_executor_tests
    COMMAND r5900_ir_direct_transfer_executor_tests)

  add_executable(r5900_ir_indirect_transfer_executor_tests
    tests/r5900_ir_indirect_transfer_executor_tests.cpp
  )
  target_link_libraries(r5900_ir_indirect_transfer_executor_tests PRIVATE b3r_recompiler)
  add_test(NAME r5900_ir_indirect_transfer_executor_tests
    COMMAND r5900_ir_indirect_transfer_executor_tests)

  add_executable(ps2_memory_map_tests""",
)

replace_once(
    "tests/r5900_ir_direct_transfer_executor_tests.cpp",
    """    {
        R5900IrExecutionState state{};
        state.gpr[5] = {0x1234000012345678ull, 0xfeedfacefeedfaceull};
        const auto block = indirect_jump(0x00109000u, 5u,
                                         nop(0x00109004u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok() && result.next_pc == 0x12345678u,
               "indirect JR must execute and return low32 target");
    }

""",
    "",
)

print("Task 2 patch applied")
