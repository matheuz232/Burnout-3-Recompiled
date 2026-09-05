#include "recompiler/windows/r5900_x64_backend.h"

#include "recompiler/r5900_ir_validation.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

namespace b3r::recompiler {
namespace {

static_assert(std::is_standard_layout_v<R5900IrGprValue>);
static_assert(std::is_standard_layout_v<R5900IrExecutionState>);
static_assert(std::is_standard_layout_v<R5900IrExecutionContext>);
static_assert(sizeof(R5900IrGprValue) == 16u);
static_assert(offsetof(R5900IrGprValue, low64) == 0u);
static_assert(offsetof(R5900IrGprValue, high64) == 8u);
static_assert(offsetof(R5900IrExecutionState, gpr) == 0u);

constexpr std::uint32_t state_offset(std::size_t value) {
    return static_cast<std::uint32_t>(value);
}

constexpr std::uint32_t context_offset(std::size_t value) {
    return static_cast<std::uint32_t>(value);
}

constexpr std::uint32_t gpr_offset(std::uint8_t index) {
    return state_offset(offsetof(R5900IrExecutionState, gpr) +
                        static_cast<std::size_t>(index) * sizeof(R5900IrGprValue));
}

constexpr std::uint32_t gpr_low64_offset(std::uint8_t index) {
    return gpr_offset(index) + state_offset(offsetof(R5900IrGprValue, low64));
}

constexpr std::uint32_t gpr_high64_offset(std::uint8_t index) {
    return gpr_offset(index) + state_offset(offsetof(R5900IrGprValue, high64));
}

constexpr std::uint32_t hi_offset() {
    return state_offset(offsetof(R5900IrExecutionState, hi));
}

constexpr std::uint32_t lo_offset() {
    return state_offset(offsetof(R5900IrExecutionState, lo));
}

constexpr std::uint32_t hi1_offset() {
    return state_offset(offsetof(R5900IrExecutionState, hi1));
}

constexpr std::uint32_t lo1_offset() {
    return state_offset(offsetof(R5900IrExecutionState, lo1));
}

constexpr std::uint32_t sa_offset() {
    return state_offset(offsetof(R5900IrExecutionState, sa));
}

constexpr std::uint32_t fpr_offset(std::uint8_t index) {
    return state_offset(offsetof(R5900IrExecutionState, fpr) +
                        static_cast<std::size_t>(index) * sizeof(std::uint32_t));
}

constexpr std::uint32_t fcr31_offset() {
    return state_offset(offsetof(R5900IrExecutionState, fcr31));
}

constexpr std::uint32_t fp_acc_offset() {
    return state_offset(offsetof(R5900IrExecutionState, fp_acc));
}

constexpr std::uint32_t current_memory_guest_pc_offset() {
    return context_offset(offsetof(R5900IrExecutionContext, current_memory_guest_pc));
}

R5900X64CompileError map_validation_error(R5900IrValidationError error) {
    switch (error) {
    case R5900IrValidationError::MalformedInstruction:
        return R5900X64CompileError::MalformedInstruction;
    case R5900IrValidationError::InvalidRegister:
        return R5900X64CompileError::InvalidRegister;
    case R5900IrValidationError::UnsupportedOpcode:
        return R5900X64CompileError::UnsupportedOpcode;
    case R5900IrValidationError::None:
    default:
        return R5900X64CompileError::None;
    }
}

bool r5900_native_store128(R5900IrExecutionContext* context,
                           std::uint32_t address,
                           std::uint64_t low64,
                           std::uint64_t high64) noexcept {
    if (context == nullptr ||
        context->memory.user == nullptr ||
        context->memory.write128 == nullptr) {
        if (context != nullptr) {
            context->memory_fault = {
                true,
                R5900IrMemoryAccessKind::Store,
                context->current_memory_guest_pc,
                address,
                16u,
            };
        }
        return false;
    }

    if (!context->memory.write128(context->memory.user,
                                  address,
                                  low64,
                                  high64)) {
        context->memory_fault = {
            true,
            R5900IrMemoryAccessKind::Store,
            context->current_memory_guest_pc,
            address,
            16u,
        };
        return false;
    }
    return true;
}

void emit_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
}

void patch_u32(std::vector<std::uint8_t>& bytes,
               std::size_t offset,
               std::uint32_t value) {
    bytes[offset + 0u] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

void emit_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64u; shift += 8u) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void emit_xor_eax_eax(std::vector<std::uint8_t>& bytes) {
    bytes.push_back(0x31u);
    bytes.push_back(0xc0u);
}

void emit_store_rax_to_state(std::vector<std::uint8_t>& bytes,
                             std::uint32_t displacement) {
    bytes.push_back(0x48u);
    bytes.push_back(0x89u);
    bytes.push_back(0x81u);
    emit_u32(bytes, displacement);
}

void emit_store_eax_to_state(std::vector<std::uint8_t>& bytes,
                             std::uint32_t displacement) {
    bytes.push_back(0x89u);
    bytes.push_back(0x81u);
    emit_u32(bytes, displacement);
}

void emit_load_eax_from_state(std::vector<std::uint8_t>& bytes,
                              std::uint32_t displacement) {
    bytes.push_back(0x8bu);
    bytes.push_back(0x81u);
    emit_u32(bytes, displacement);
}

void emit_load_edx_from_state(std::vector<std::uint8_t>& bytes,
                              std::uint32_t displacement) {
    bytes.push_back(0x8bu);
    bytes.push_back(0x91u);
    emit_u32(bytes, displacement);
}

void emit_load_rax_from_state(std::vector<std::uint8_t>& bytes,
                              std::uint32_t displacement) {
    bytes.push_back(0x48u);
    bytes.push_back(0x8bu);
    bytes.push_back(0x81u);
    emit_u32(bytes, displacement);
}

void emit_load_rdx_from_state(std::vector<std::uint8_t>& bytes,
                              std::uint32_t displacement) {
    bytes.push_back(0x48u);
    bytes.push_back(0x8bu);
    bytes.push_back(0x91u);
    emit_u32(bytes, displacement);
}

void emit_mov_eax_imm32(std::vector<std::uint8_t>& bytes,
                        std::uint32_t immediate) {
    bytes.push_back(0xb8u);
    emit_u32(bytes, immediate);
}

void emit_mov_edx_imm32(std::vector<std::uint8_t>& bytes,
                        std::uint32_t immediate) {
    bytes.push_back(0xbau);
    emit_u32(bytes, immediate);
}

void emit_mov_rax_imm64(std::vector<std::uint8_t>& bytes,
                        std::uint64_t immediate) {
    bytes.push_back(0x48u);
    bytes.push_back(0xb8u);
    emit_u64(bytes, immediate);
}

void emit_mov_rdx_imm64(std::vector<std::uint8_t>& bytes,
                        std::uint64_t immediate) {
    bytes.push_back(0x48u);
    bytes.push_back(0xbau);
    emit_u64(bytes, immediate);
}

void emit_operand32_to_eax(std::vector<std::uint8_t>& bytes,
                           const R5900IrOperand& operand) {
    if (operand.kind == R5900IrOperandKind::Gpr) {
        emit_load_eax_from_state(bytes, gpr_low64_offset(operand.gpr_index));
        return;
    }
    emit_mov_eax_imm32(bytes, static_cast<std::uint32_t>(operand.immediate));
}

void emit_operand32_to_edx(std::vector<std::uint8_t>& bytes,
                           const R5900IrOperand& operand) {
    if (operand.kind == R5900IrOperandKind::Gpr) {
        emit_load_edx_from_state(bytes, gpr_low64_offset(operand.gpr_index));
        return;
    }
    emit_mov_edx_imm32(bytes, static_cast<std::uint32_t>(operand.immediate));
}

void emit_operand64_to_rax(std::vector<std::uint8_t>& bytes,
                           const R5900IrOperand& operand) {
    if (operand.kind == R5900IrOperandKind::Gpr) {
        emit_load_rax_from_state(bytes, gpr_low64_offset(operand.gpr_index));
        return;
    }
    emit_mov_rax_imm64(bytes, static_cast<std::uint64_t>(operand.immediate));
}

void emit_operand64_to_rdx(std::vector<std::uint8_t>& bytes,
                           const R5900IrOperand& operand) {
    if (operand.kind == R5900IrOperandKind::Gpr) {
        emit_load_rdx_from_state(bytes, gpr_low64_offset(operand.gpr_index));
        return;
    }
    emit_mov_rdx_imm64(bytes, static_cast<std::uint64_t>(operand.immediate));
}

void emit_zero_gpr0(std::vector<std::uint8_t>& bytes) {
    emit_xor_eax_eax(bytes);
    emit_store_rax_to_state(bytes, gpr_offset(0u));
    emit_store_rax_to_state(bytes, gpr_offset(0u) + 8u);
}

void emit_helper_frame_prologue(std::vector<std::uint8_t>& bytes) {
    // On Win64 entry RSP % 16 == 8. Subtracting 0x38 aligns RSP for nested
    // calls, reserves 32 bytes of shadow space, and leaves 24 bytes for locals.
    bytes.insert(bytes.end(), {0x48u, 0x83u, 0xecu, 0x38u});
    // mov [rsp+0x20], rcx  ; saved state
    bytes.insert(bytes.end(), {0x48u, 0x89u, 0x4cu, 0x24u, 0x20u});
    // mov [rsp+0x28], rdx  ; saved execution context
    bytes.insert(bytes.end(), {0x48u, 0x89u, 0x54u, 0x24u, 0x28u});
}

void emit_helper_frame_epilogue(std::vector<std::uint8_t>& bytes) {
    bytes.insert(bytes.end(), {0x48u, 0x83u, 0xc4u, 0x38u});
}

void emit_reload_state_and_context(std::vector<std::uint8_t>& bytes) {
    // mov rcx, [rsp+0x20]
    bytes.insert(bytes.end(), {0x48u, 0x8bu, 0x4cu, 0x24u, 0x20u});
    // mov rdx, [rsp+0x28]
    bytes.insert(bytes.end(), {0x48u, 0x8bu, 0x54u, 0x24u, 0x28u});
}

void emit_add_word_sign_extend(std::vector<std::uint8_t>& bytes,
                               const R5900IrInstruction& instruction) {
    emit_operand32_to_eax(bytes, instruction.inputs[0]);
    emit_operand32_to_edx(bytes, instruction.inputs[1]);
    bytes.push_back(0x01u);
    bytes.push_back(0xd0u);
    bytes.push_back(0x48u);
    bytes.push_back(0x98u);
    if (instruction.destination->index != 0u) {
        emit_store_rax_to_state(bytes,
                                gpr_low64_offset(instruction.destination->index));
    }
}

void emit_or64(std::vector<std::uint8_t>& bytes,
               const R5900IrInstruction& instruction) {
    emit_operand64_to_rax(bytes, instruction.inputs[0]);
    emit_operand64_to_rdx(bytes, instruction.inputs[1]);
    bytes.push_back(0x48u);
    bytes.push_back(0x09u);
    bytes.push_back(0xd0u);
    if (instruction.destination->index != 0u) {
        emit_store_rax_to_state(bytes,
                                gpr_low64_offset(instruction.destination->index));
    }
}

void emit_and64(std::vector<std::uint8_t>& bytes,
                const R5900IrInstruction& instruction) {
    emit_operand64_to_rax(bytes, instruction.inputs[0]);
    emit_operand64_to_rdx(bytes, instruction.inputs[1]);
    bytes.push_back(0x48u);
    bytes.push_back(0x21u);
    bytes.push_back(0xd0u);
    if (instruction.destination->index != 0u) {
        emit_store_rax_to_state(bytes,
                                gpr_low64_offset(instruction.destination->index));
    }
}

void emit_lui(std::vector<std::uint8_t>& bytes,
              const R5900IrInstruction& instruction) {
    const auto immediate16 =
        static_cast<std::uint16_t>(instruction.inputs[0].immediate);
    const auto word = static_cast<std::uint32_t>(immediate16) << 16u;
    emit_mov_eax_imm32(bytes, word);
    bytes.push_back(0x48u);
    bytes.push_back(0x98u);
    if (instruction.destination->index != 0u) {
        emit_store_rax_to_state(bytes,
                                gpr_low64_offset(instruction.destination->index));
    }
}

std::uint32_t hilo_destination_offset(R5900IrDestinationKind kind) {
    switch (kind) {
    case R5900IrDestinationKind::Hi:
        return hi_offset();
    case R5900IrDestinationKind::Lo:
        return lo_offset();
    case R5900IrDestinationKind::Hi1:
        return hi1_offset();
    case R5900IrDestinationKind::Lo1:
        return lo1_offset();
    default:
        return hi_offset();
    }
}

void emit_move_gpr_low64(std::vector<std::uint8_t>& bytes,
                         const R5900IrInstruction& instruction) {
    emit_load_rax_from_state(
        bytes,
        gpr_low64_offset(instruction.inputs[0].gpr_index));
    emit_store_rax_to_state(
        bytes,
        hilo_destination_offset(instruction.destination->kind));
}

void emit_mtsah(std::vector<std::uint8_t>& bytes,
                const R5900IrInstruction& instruction) {
    emit_load_eax_from_state(
        bytes,
        gpr_low64_offset(instruction.inputs[0].gpr_index));
    bytes.push_back(0x83u);
    bytes.push_back(0xe0u);
    bytes.push_back(0x07u);
    bytes.push_back(0x83u);
    bytes.push_back(0xf0u);
    bytes.push_back(
        static_cast<std::uint8_t>(instruction.inputs[1].immediate) & 0x07u);
    bytes.push_back(0xd1u);
    bytes.push_back(0xe0u);
    emit_store_eax_to_state(bytes, sa_offset());
}

void emit_load_xmm0_gpr(std::vector<std::uint8_t>& bytes,
                        std::uint8_t index) {
    bytes.push_back(0xf3u);
    bytes.push_back(0x0fu);
    bytes.push_back(0x6fu);
    bytes.push_back(0x81u);
    emit_u32(bytes, gpr_offset(index));
}

void emit_load_xmm1_gpr(std::vector<std::uint8_t>& bytes,
                        std::uint8_t index) {
    bytes.push_back(0xf3u);
    bytes.push_back(0x0fu);
    bytes.push_back(0x6fu);
    bytes.push_back(0x89u);
    emit_u32(bytes, gpr_offset(index));
}

void emit_movd_eax_xmm0(std::vector<std::uint8_t>& bytes) {
    bytes.insert(bytes.end(), {0x66u, 0x0fu, 0x7eu, 0xc0u});
}

void emit_movd_edx_xmm1(std::vector<std::uint8_t>& bytes) {
    bytes.insert(bytes.end(), {0x66u, 0x0fu, 0x7eu, 0xcau});
}

void emit_shift_xmm_sources_one_lane(std::vector<std::uint8_t>& bytes) {
    bytes.insert(bytes.end(), {0x66u, 0x0fu, 0x73u, 0xd8u, 0x04u});
    bytes.insert(bytes.end(), {0x66u, 0x0fu, 0x73u, 0xd9u, 0x04u});
}

void emit_padduw(std::vector<std::uint8_t>& bytes,
                 const R5900IrInstruction& instruction) {
    emit_load_xmm0_gpr(bytes, instruction.inputs[0].gpr_index);
    emit_load_xmm1_gpr(bytes, instruction.inputs[1].gpr_index);
    for (std::uint32_t lane = 0u; lane < 4u; ++lane) {
        emit_movd_eax_xmm0(bytes);
        emit_movd_edx_xmm1(bytes);
        bytes.push_back(0x01u);
        bytes.push_back(0xd0u);
        bytes.push_back(0x19u);
        bytes.push_back(0xd2u);
        bytes.push_back(0x09u);
        bytes.push_back(0xd0u);
        if (instruction.destination->index != 0u) {
            emit_store_eax_to_state(
                bytes,
                gpr_offset(instruction.destination->index) +
                    lane * sizeof(std::uint32_t));
        }
        if (lane != 3u) {
            emit_shift_xmm_sources_one_lane(bytes);
        }
    }
}

void emit_move_bits32(std::vector<std::uint8_t>& bytes,
                      const R5900IrInstruction& instruction) {
    emit_load_eax_from_state(
        bytes,
        gpr_low64_offset(instruction.inputs[0].gpr_index));
    const auto destination_offset =
        instruction.destination->kind == R5900IrDestinationKind::Fpr
            ? fpr_offset(instruction.destination->index)
            : fcr31_offset();
    emit_store_eax_to_state(bytes, destination_offset);
}

void emit_load_xmm0_f32(std::vector<std::uint8_t>& bytes,
                        std::uint32_t displacement) {
    bytes.insert(bytes.end(), {0xf3u, 0x0fu, 0x10u, 0x81u});
    emit_u32(bytes, displacement);
}

void emit_add_xmm0_f32(std::vector<std::uint8_t>& bytes,
                       std::uint32_t displacement) {
    bytes.insert(bytes.end(), {0xf3u, 0x0fu, 0x58u, 0x81u});
    emit_u32(bytes, displacement);
}

void emit_store_xmm0_f32(std::vector<std::uint8_t>& bytes,
                         std::uint32_t displacement) {
    bytes.insert(bytes.end(), {0xf3u, 0x0fu, 0x11u, 0x81u});
    emit_u32(bytes, displacement);
}

void emit_add_f32_to_accumulator(std::vector<std::uint8_t>& bytes,
                                 const R5900IrInstruction& instruction) {
    emit_load_xmm0_f32(bytes,
                       fpr_offset(instruction.inputs[0].gpr_index));
    emit_add_xmm0_f32(bytes,
                      fpr_offset(instruction.inputs[1].gpr_index));
    emit_store_xmm0_f32(bytes, fp_acc_offset());
}

void emit_store128(std::vector<std::uint8_t>& bytes,
                   const R5900IrInstruction& instruction) {
    // Restore state into RCX in case an earlier helper call clobbered volatile
    // registers. The saved pointers live outside the Win64 shadow area.
    emit_reload_state_and_context(bytes);

    // mov rax, [rsp+0x28] ; context
    bytes.insert(bytes.end(), {0x48u, 0x8bu, 0x44u, 0x24u, 0x28u});
    // mov dword ptr [rax+current_memory_guest_pc], guest_pc
    bytes.push_back(0xc7u);
    bytes.push_back(0x80u);
    emit_u32(bytes, current_memory_guest_pc_offset());
    emit_u32(bytes, instruction.guest_pc);

    // EDX = (low32(base) + signed16(offset)) & ~0xf.
    emit_load_eax_from_state(bytes,
                             gpr_low64_offset(instruction.inputs[0].gpr_index));
    bytes.push_back(0x05u); // add eax, imm32
    emit_u32(bytes, static_cast<std::uint32_t>(
                        static_cast<std::int32_t>(instruction.inputs[2].immediate)));
    bytes.push_back(0x25u); // and eax, imm32
    emit_u32(bytes, 0xfffffff0u);
    bytes.insert(bytes.end(), {0x89u, 0xc2u}); // mov edx, eax

    // R8/R9 = complete 128-bit source GPR.
    bytes.insert(bytes.end(), {0x4cu, 0x8bu, 0x81u});
    emit_u32(bytes, gpr_low64_offset(instruction.inputs[1].gpr_index));
    bytes.insert(bytes.end(), {0x4cu, 0x8bu, 0x89u});
    emit_u32(bytes, gpr_high64_offset(instruction.inputs[1].gpr_index));

    // RCX = context. EDX/R8/R9 already carry the remaining Win64 arguments.
    bytes.insert(bytes.end(), {0x48u, 0x8bu, 0x4cu, 0x24u, 0x28u});
    emit_mov_rax_imm64(
        bytes,
        static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(&r5900_native_store128)));
    bytes.insert(bytes.end(), {0xffu, 0xd0u}); // call rax
    bytes.insert(bytes.end(), {0x84u, 0xc0u}); // test al, al

    // jnz success. Helper failure is terminal for the current native block.
    bytes.insert(bytes.end(), {0x0fu, 0x85u});
    const auto success_rel32_offset = bytes.size();
    emit_u32(bytes, 0u);

    emit_xor_eax_eax(bytes);
    emit_helper_frame_epilogue(bytes);
    bytes.push_back(0xc3u);

    const auto success_offset = bytes.size();
    const auto branch_end = success_rel32_offset + sizeof(std::uint32_t);
    const auto displacement =
        static_cast<std::int64_t>(success_offset) -
        static_cast<std::int64_t>(branch_end);
    patch_u32(bytes,
              success_rel32_offset,
              static_cast<std::uint32_t>(
                  static_cast<std::int32_t>(displacement)));

    emit_reload_state_and_context(bytes);
}

R5900X64CompileResult failure(R5900X64CompileError error,
                              std::string message) {
    R5900X64CompileResult result{};
    result.error = error;
    result.message = std::move(message);
    return result;
}

struct EmitResult {
    R5900X64CompileError error{R5900X64CompileError::None};
    std::string message{};

    [[nodiscard]] bool ok() const noexcept {
        return error == R5900X64CompileError::None;
    }
};

EmitResult emit_failure(R5900X64CompileError error,
                        std::size_t index,
                        const R5900IrInstruction& instruction) {
    std::ostringstream message;
    message << "IR instruction " << index << " at guest PC 0x" << std::hex
            << instruction.guest_pc
            << ": opcode not implemented by x64 backend";
    return {error, message.str()};
}

bool sequence_needs_helper(
    const std::vector<R5900IrInstruction>& instructions) noexcept {
    for (const auto& instruction : instructions) {
        if (instruction.opcode == R5900IrOpcode::Store128) {
            return true;
        }
    }
    return false;
}

EmitResult emit_ir_instruction(std::vector<std::uint8_t>& bytes,
                               const R5900IrInstruction& instruction,
                               std::size_t index,
                               bool helper_frame) {
    switch (instruction.opcode) {
    case R5900IrOpcode::Nop:
        return {};
    case R5900IrOpcode::AddWordSignExtend:
        emit_add_word_sign_extend(bytes, instruction);
        return {};
    case R5900IrOpcode::Or64:
        emit_or64(bytes, instruction);
        return {};
    case R5900IrOpcode::And64:
        emit_and64(bytes, instruction);
        return {};
    case R5900IrOpcode::LoadUpperImmediateSignExtend:
        emit_lui(bytes, instruction);
        return {};
    case R5900IrOpcode::MoveGprLow64:
        emit_move_gpr_low64(bytes, instruction);
        return {};
    case R5900IrOpcode::ComputeMtsah:
        emit_mtsah(bytes, instruction);
        return {};
    case R5900IrOpcode::AddPackedU32Saturate128:
        emit_padduw(bytes, instruction);
        return {};
    case R5900IrOpcode::MoveBits32:
        emit_move_bits32(bytes, instruction);
        return {};
    case R5900IrOpcode::AddF32ToAccumulator:
        emit_add_f32_to_accumulator(bytes, instruction);
        return {};
    case R5900IrOpcode::Store128:
        if (!helper_frame) {
            return emit_failure(R5900X64CompileError::UnsupportedOpcode,
                                index,
                                instruction);
        }
        emit_store128(bytes, instruction);
        return {};
    default:
        return emit_failure(R5900X64CompileError::UnsupportedOpcode,
                            index,
                            instruction);
    }
}

EmitResult emit_ir_sequence(std::vector<std::uint8_t>& bytes,
                            const std::vector<R5900IrInstruction>& instructions,
                            std::size_t base_index,
                            bool helper_frame) {
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        const auto emitted = emit_ir_instruction(
            bytes,
            instructions[index],
            base_index + index,
            helper_frame);
        if (!emitted.ok()) {
            return emitted;
        }
    }
    return {};
}

struct PendingX64Code {
    R5900X64CompileError error{R5900X64CompileError::None};
    std::string message{};
    void* code{};
    std::size_t size{};

    [[nodiscard]] bool ok() const noexcept {
        return error == R5900X64CompileError::None &&
               code != nullptr && size != 0u;
    }
};

PendingX64Code pending_failure(R5900X64CompileError error,
                               std::string message) {
    PendingX64Code result{};
    result.error = error;
    result.message = std::move(message);
    return result;
}

PendingX64Code publish_code(const std::vector<std::uint8_t>& bytes) {
    void* code = VirtualAlloc(nullptr,
                              bytes.size(),
                              MEM_RESERVE | MEM_COMMIT,
                              PAGE_READWRITE);
    if (code == nullptr) {
        return pending_failure(R5900X64CompileError::AllocationFailed,
                               "VirtualAlloc failed for R5900 x64 block");
    }

    std::memcpy(code, bytes.data(), bytes.size());

    DWORD previous_protection{};
    if (!VirtualProtect(code,
                        bytes.size(),
                        PAGE_EXECUTE_READ,
                        &previous_protection)) {
        VirtualFree(code, 0u, MEM_RELEASE);
        return pending_failure(R5900X64CompileError::ProtectionFailed,
                               "VirtualProtect failed for R5900 x64 block");
    }

    if (!FlushInstructionCache(GetCurrentProcess(), code, bytes.size())) {
        VirtualFree(code, 0u, MEM_RELEASE);
        return pending_failure(R5900X64CompileError::CacheFlushFailed,
                               "FlushInstructionCache failed for R5900 x64 block");
    }

    PendingX64Code result{};
    result.code = code;
    result.size = bytes.size();
    return result;
}

PendingX64Code compile_linear_code(
    const std::vector<R5900IrInstruction>& instructions,
    std::uint32_t fallthrough_pc) {
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        const auto validation =
            validate_r5900_ir_instruction(instructions[index], index);
        if (!validation.ok()) {
            return pending_failure(map_validation_error(validation.error),
                                   validation.message);
        }
    }

    const bool helper_frame = sequence_needs_helper(instructions);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(96u + instructions.size() * 128u);
    if (helper_frame) {
        emit_helper_frame_prologue(bytes);
    }
    emit_zero_gpr0(bytes);

    const auto emitted = emit_ir_sequence(bytes,
                                          instructions,
                                          0u,
                                          helper_frame);
    if (!emitted.ok()) {
        return pending_failure(emitted.error, emitted.message);
    }

    emit_zero_gpr0(bytes);
    emit_mov_eax_imm32(bytes, fallthrough_pc);
    if (helper_frame) {
        emit_helper_frame_epilogue(bytes);
    }
    bytes.push_back(0xc3u);
    return publish_code(bytes);
}

PendingX64Code compile_branch_equal_code(const R5900IrBlock& block) {
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
    bytes.insert(bytes.end(), {0x0fu, 0x85u});          // jne rel32
    const auto not_taken_rel32_offset = bytes.size();
    emit_u32(bytes, 0u);

    emit_zero_gpr0(bytes);
    const auto taken_delay = emit_ir_sequence(
        bytes,
        block.terminator.delay_slot,
        block.body.size() + 1u,
        helper_frame);
    if (!taken_delay.ok()) {
        return pending_failure(taken_delay.error, taken_delay.message);
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
                               "R5900 x64 BEQ native path exceeds rel32 range");
    }
    patch_u32(bytes,
              not_taken_rel32_offset,
              static_cast<std::uint32_t>(
                  static_cast<std::int32_t>(displacement)));

    emit_zero_gpr0(bytes);
    const auto not_taken_delay = emit_ir_sequence(
        bytes,
        block.terminator.delay_slot,
        block.body.size() + 1u,
        helper_frame);
    if (!not_taken_delay.ok()) {
        return pending_failure(not_taken_delay.error, not_taken_delay.message);
    }
    emit_zero_gpr0(bytes);
    emit_mov_eax_imm32(bytes, block.terminator.fallthrough_pc);
    if (helper_frame) {
        emit_helper_frame_epilogue(bytes);
    }
    bytes.push_back(0xc3u);

    return publish_code(bytes);
}

PendingX64Code compile_direct_transfer_code(const R5900IrBlock& block) {
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
    std::vector<std::uint8_t> bytes;
    bytes.reserve(192u + block.body.size() * 128u);
    // Reuse the aligned Win64 helper frame. The target local at +0x30 is
    // outside shadow space and the saved state/context pointers.
    emit_helper_frame_prologue(bytes);
    emit_zero_gpr0(bytes);
    const auto body = emit_ir_sequence(bytes, block.body, 0u, true);
    if (!body.ok()) return pending_failure(body.error, body.message);

    emit_load_eax_from_state(bytes,
        gpr_low64_offset(block.terminator.inputs.front().gpr_index));
    bytes.insert(bytes.end(), {0x89u, 0x44u, 0x24u, 0x30u}); // mov [rsp+0x30], eax
    if (block.terminator.kind == R5900IrTerminatorKind::IndirectCall &&
        *block.terminator.link_gpr != 0u) {
        emit_mov_eax_imm32(bytes, block.terminator.link_pc);
        emit_store_rax_to_state(bytes, gpr_low64_offset(*block.terminator.link_gpr));
    }
    emit_zero_gpr0(bytes);
    const auto delay = emit_ir_sequence(bytes, block.terminator.delay_slot,
                                        block.body.size() + 1u, true);
    if (!delay.ok()) return pending_failure(delay.error, delay.message);
    emit_zero_gpr0(bytes);
    bytes.insert(bytes.end(), {0x8bu, 0x44u, 0x24u, 0x30u}); // mov eax, [rsp+0x30]
    emit_helper_frame_epilogue(bytes);
    bytes.push_back(0xc3u);
    return publish_code(bytes);
}

} // namespace

R5900X64CompiledBlock::R5900X64CompiledBlock(void* code,
                                             std::size_t size) noexcept
    : code_(code), size_(size) {}

R5900X64CompiledBlock::~R5900X64CompiledBlock() {
    release();
}

R5900X64CompiledBlock::R5900X64CompiledBlock(
    R5900X64CompiledBlock&& other) noexcept
    : code_(std::exchange(other.code_, nullptr)),
      size_(std::exchange(other.size_, 0u)) {}

R5900X64CompiledBlock& R5900X64CompiledBlock::operator=(
    R5900X64CompiledBlock&& other) noexcept {
    if (this != &other) {
        release();
        code_ = std::exchange(other.code_, nullptr);
        size_ = std::exchange(other.size_, 0u);
    }
    return *this;
}

bool R5900X64CompiledBlock::valid() const noexcept {
    return code_ != nullptr && size_ != 0u;
}

R5900X64ExecutionResult R5900X64CompiledBlock::execute(
    R5900IrExecutionContext& context) const noexcept {
    if (!valid()) {
        return {R5900IrExecutionError::MalformedInstruction,
                "R5900 x64 compiled block is not valid",
                0u};
    }
    if (context.state == nullptr) {
        return {R5900IrExecutionError::MalformedInstruction,
                "R5900 x64 execution context has no CPU state",
                0u};
    }

    context.memory_fault = {};
    context.current_memory_guest_pc = 0u;

    using GeneratedFunction = std::uint32_t (*)(R5900IrExecutionState*,
                                                R5900IrExecutionContext*);
    const auto function = reinterpret_cast<GeneratedFunction>(code_);
    const auto next_pc = function(context.state, &context);

    if (context.memory_fault.active) {
        return {R5900IrExecutionError::MemoryAccessFailure,
                "R5900 x64 guest-memory access failed",
                0u};
    }
    return {R5900IrExecutionError::None, {}, next_pc};
}

std::uint32_t R5900X64CompiledBlock::execute(
    R5900IrExecutionState& state) const noexcept {
    R5900IrExecutionContext context{};
    context.state = &state;
    return execute(context).next_pc;
}

void R5900X64CompiledBlock::release() noexcept {
    if (code_ != nullptr) {
        VirtualFree(code_, 0u, MEM_RELEASE);
        code_ = nullptr;
        size_ = 0u;
    }
}

R5900X64CompileResult compile_r5900_ir_x64(
    const std::vector<R5900IrInstruction>& instructions) {
    const auto fallthrough_pc = instructions.empty()
        ? 0u
        : instructions.back().guest_pc + 4u;
    auto pending = compile_linear_code(instructions, fallthrough_pc);
    if (!pending.ok()) {
        return failure(pending.error, std::move(pending.message));
    }

    R5900X64CompiledBlock block(pending.code, pending.size);
    R5900X64CompileResult result{};
    result.block.emplace(std::move(block));
    return result;
}

R5900X64CompileResult compile_r5900_ir_x64(const R5900IrBlock& block) {
    const auto validation = validate_r5900_ir_block(block);
    if (!validation.ok()) {
        return failure(map_validation_error(validation.error),
                       validation.message);
    }

    PendingX64Code pending{};
    switch (block.terminator.kind) {
    case R5900IrTerminatorKind::Fallthrough:
        pending = compile_linear_code(block.body,
                                      block.terminator.fallthrough_pc);
        break;
    case R5900IrTerminatorKind::BranchEqual64:
        pending = compile_branch_equal_code(block);
        break;
    case R5900IrTerminatorKind::DirectJump:
    case R5900IrTerminatorKind::DirectCall:
        pending = compile_direct_transfer_code(block);
        break;
    case R5900IrTerminatorKind::IndirectJump:
    case R5900IrTerminatorKind::IndirectCall:
        pending = compile_indirect_transfer_code(block);
        break;
    default:
        return failure(R5900X64CompileError::UnsupportedOpcode,
                       "R5900 x64 backend does not support this block terminator");
    }

    if (!pending.ok()) {
        return failure(pending.error, std::move(pending.message));
    }

    R5900X64CompiledBlock native_block(pending.code, pending.size);
    R5900X64CompileResult result{};
    result.block.emplace(std::move(native_block));
    return result;
}

} // namespace b3r::recompiler
