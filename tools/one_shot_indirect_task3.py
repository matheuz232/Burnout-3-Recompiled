from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    if text.count(old) != 1:
        raise SystemExit(f"expected exactly one match in {path}: {old[:120]!r}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "src/recompiler/windows/r5900_x64_backend.cpp",
    """void emit_mov_edx_imm32(std::vector<std::uint8_t>& bytes,\n                        std::uint32_t immediate) {\n    bytes.push_back(0xbau);\n    emit_u32(bytes, immediate);\n}\n\nvoid emit_mov_rax_imm64""",
    """void emit_mov_edx_imm32(std::vector<std::uint8_t>& bytes,
                        std::uint32_t immediate) {
    bytes.push_back(0xbau);
    emit_u32(bytes, immediate);
}

void emit_store_eax_to_rsp_30(std::vector<std::uint8_t>& bytes) {
    // mov dword ptr [rsp+0x30], eax
    bytes.insert(bytes.end(), {0x89u, 0x44u, 0x24u, 0x30u});
}

void emit_load_eax_from_rsp_30(std::vector<std::uint8_t>& bytes) {
    // mov eax, dword ptr [rsp+0x30]
    bytes.insert(bytes.end(), {0x8bu, 0x44u, 0x24u, 0x30u});
}

void emit_mov_rax_imm64""",
)

replace_once(
    "src/recompiler/windows/r5900_x64_backend.cpp",
    """PendingX64Code compile_direct_transfer_code(const R5900IrBlock& block) {\n    const bool helper_frame =\n        sequence_needs_helper(block.body) ||\n        sequence_needs_helper(block.terminator.delay_slot);\n\n    std::vector<std::uint8_t> bytes;\n    bytes.reserve(128u +\n        block.body.size() * 128u +\n        block.terminator.delay_slot.size() * 256u);\n\n    if (helper_frame) {\n        emit_helper_frame_prologue(bytes);\n    }\n    emit_zero_gpr0(bytes);\n\n    const auto body_emitted = emit_ir_sequence(\n        bytes,\n        block.body,\n        0u,\n        helper_frame);\n    if (!body_emitted.ok()) {\n        return pending_failure(body_emitted.error, body_emitted.message);\n    }\n\n    if (block.terminator.kind == R5900IrTerminatorKind::DirectCall) {\n        emit_mov_eax_imm32(bytes, block.terminator.link_pc);\n        emit_store_rax_to_state(bytes, gpr_low64_offset(31u));\n    }\n\n    emit_zero_gpr0(bytes);\n    const auto delay_emitted = emit_ir_sequence(\n        bytes,\n        block.terminator.delay_slot,\n        block.body.size() + 1u,\n        helper_frame);\n    if (!delay_emitted.ok()) {\n        return pending_failure(delay_emitted.error, delay_emitted.message);\n    }\n\n    emit_zero_gpr0(bytes);\n    emit_mov_eax_imm32(bytes, block.terminator.target_pc);\n    if (helper_frame) {\n        emit_helper_frame_epilogue(bytes);\n    }\n    bytes.push_back(0xc3u);\n    return publish_code(bytes);\n}\n\n} // namespace""",
    """PendingX64Code compile_direct_transfer_code(const R5900IrBlock& block) {
    const bool helper_frame =
        sequence_needs_helper(block.body) ||
        sequence_needs_helper(block.terminator.delay_slot);

    std::vector<std::uint8_t> bytes;
    bytes.reserve(128u +
        block.body.size() * 128u +
        block.terminator.delay_slot.size() * 256u);

    if (helper_frame) {
        emit_helper_frame_prologue(bytes);
    }
    emit_zero_gpr0(bytes);

    const auto body_emitted = emit_ir_sequence(
        bytes,
        block.body,
        0u,
        helper_frame);
    if (!body_emitted.ok()) {
        return pending_failure(body_emitted.error, body_emitted.message);
    }

    if (block.terminator.kind == R5900IrTerminatorKind::DirectCall) {
        emit_mov_eax_imm32(bytes, block.terminator.link_pc);
        emit_store_rax_to_state(bytes, gpr_low64_offset(31u));
    }

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
    emit_mov_eax_imm32(bytes, block.terminator.target_pc);
    if (helper_frame) {
        emit_helper_frame_epilogue(bytes);
    }
    bytes.push_back(0xc3u);
    return publish_code(bytes);
}

PendingX64Code compile_indirect_transfer_code(const R5900IrBlock& block) {
    constexpr bool helper_frame = true;

    std::vector<std::uint8_t> bytes;
    bytes.reserve(160u +
        block.body.size() * 128u +
        block.terminator.delay_slot.size() * 256u);

    emit_helper_frame_prologue(bytes);
    emit_zero_gpr0(bytes);

    const auto body_emitted = emit_ir_sequence(
        bytes, block.body, 0u, helper_frame);
    if (!body_emitted.ok()) {
        return pending_failure(body_emitted.error, body_emitted.message);
    }

    emit_load_eax_from_state(
        bytes, gpr_low64_offset(block.terminator.inputs[0].gpr_index));
    emit_store_eax_to_rsp_30(bytes);

    if (block.terminator.kind == R5900IrTerminatorKind::IndirectCall &&
        block.terminator.link_gpr != 0u) {
        emit_mov_eax_imm32(bytes, block.terminator.link_pc);
        emit_store_rax_to_state(
            bytes, gpr_low64_offset(block.terminator.link_gpr));
    }

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
    emit_load_eax_from_rsp_30(bytes);
    emit_helper_frame_epilogue(bytes);
    bytes.push_back(0xc3u);
    return publish_code(bytes);
}

} // namespace""",
)

replace_once(
    "src/recompiler/windows/r5900_x64_backend.cpp",
    """    case R5900IrTerminatorKind::DirectJump:\n    case R5900IrTerminatorKind::DirectCall:\n        pending = compile_direct_transfer_code(block);\n        break;\n    default:""",
    """    case R5900IrTerminatorKind::DirectJump:
    case R5900IrTerminatorKind::DirectCall:
        pending = compile_direct_transfer_code(block);
        break;
    case R5900IrTerminatorKind::IndirectJump:
    case R5900IrTerminatorKind::IndirectCall:
        pending = compile_indirect_transfer_code(block);
        break;
    default:""",
)

replace_once(
    "CMakeLists.txt",
    """    add_test(NAME r5900_x64_direct_transfer_windows_tests\n      COMMAND r5900_x64_direct_transfer_windows_tests)\n\n    add_executable(r5900_x64_startup_integer_windows_tests""",
    """    add_test(NAME r5900_x64_direct_transfer_windows_tests
      COMMAND r5900_x64_direct_transfer_windows_tests)

    add_executable(r5900_x64_indirect_transfer_windows_tests
      tests/r5900_x64_indirect_transfer_windows_tests.cpp
    )
    target_link_libraries(r5900_x64_indirect_transfer_windows_tests PRIVATE
      b3r_recompiler_x64
    )
    add_test(NAME r5900_x64_indirect_transfer_windows_tests
      COMMAND r5900_x64_indirect_transfer_windows_tests)

    add_executable(r5900_x64_startup_integer_windows_tests""",
)

replace_once(
    "tests/r5900_x64_direct_transfer_windows_tests.cpp",
    """    {
        auto state = sentinel_state();
        state.gpr[5].low64 = 0x0010a000u;
        const auto block = indirect_jump(0x00107c00u, 5u,
                                         nop(0x00107c04u));
        auto compiled = compile_r5900_ir_x64(block);
        expect(compiled.ok() && compiled.block.has_value(),
               "native indirect JR block must compile");
    }
""",
    "",
)

print("Task 3 patch applied")
