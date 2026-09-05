#include "recompiler/windows/r5900_x64_backend.h"

#include "recompiler/r5900_ir_validation.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

namespace b3r::recompiler {
namespace {

static_assert(std::is_standard_layout_v<R5900IrGprValue>);
static_assert(std::is_standard_layout_v<R5900IrExecutionState>);
static_assert(sizeof(R5900IrGprValue) == 16u);
static_assert(offsetof(R5900IrGprValue, low64) == 0u);
static_assert(offsetof(R5900IrGprValue, high64) == 8u);
static_assert(offsetof(R5900IrExecutionState, gpr) == 0u);

constexpr std::uint32_t state_offset(std::size_t value) {
    return static_cast<std::uint32_t>(value);
}

constexpr std::uint32_t gpr_offset(std::uint8_t index) {
    return state_offset(offsetof(R5900IrExecutionState, gpr) +
                        static_cast<std::size_t>(index) * sizeof(R5900IrGprValue));
}

constexpr std::uint32_t gpr_low64_offset(std::uint8_t index) {
    return gpr_offset(index) + state_offset(offsetof(R5900IrGprValue, low64));
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

void emit_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
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

void emit_store_rax_to_state(std::vector<std::uint8_t>& bytes, std::uint32_t displacement) {
    bytes.push_back(0x48u);
    bytes.push_back(0x89u);
    bytes.push_back(0x81u);
    emit_u32(bytes, displacement);
}

void emit_store_eax_to_state(std::vector<std::uint8_t>& bytes, std::uint32_t displacement) {
    bytes.push_back(0x89u);
    bytes.push_back(0x81u);
    emit_u32(bytes, displacement);
}

void emit_load_eax_from_state(std::vector<std::uint8_t>& bytes, std::uint32_t displacement) {
    bytes.push_back(0x8bu);
    bytes.push_back(0x81u);
    emit_u32(bytes, displacement);
}

void emit_load_edx_from_state(std::vector<std::uint8_t>& bytes, std::uint32_t displacement) {
    bytes.push_back(0x8bu);
    bytes.push_back(0x91u);
    emit_u32(bytes, displacement);
}

void emit_load_rax_from_state(std::vector<std::uint8_t>& bytes, std::uint32_t displacement) {
    bytes.push_back(0x48u);
    bytes.push_back(0x8bu);
    bytes.push_back(0x81u);
    emit_u32(bytes, displacement);
}

void emit_load_rdx_from_state(std::vector<std::uint8_t>& bytes, std::uint32_t displacement) {
    bytes.push_back(0x48u);
    bytes.push_back(0x8bu);
    bytes.push_back(0x91u);
    emit_u32(bytes, displacement);
}

void emit_mov_eax_imm32(std::vector<std::uint8_t>& bytes, std::uint32_t immediate) {
    bytes.push_back(0xb8u);
    emit_u32(bytes, immediate);
}

void emit_mov_edx_imm32(std::vector<std::uint8_t>& bytes, std::uint32_t immediate) {
    bytes.push_back(0xbau);
    emit_u32(bytes, immediate);
}

void emit_mov_rax_imm64(std::vector<std::uint8_t>& bytes, std::uint64_t immediate) {
    bytes.push_back(0x48u);
    bytes.push_back(0xb8u);
    emit_u64(bytes, immediate);
}

void emit_mov_rdx_imm64(std::vector<std::uint8_t>& bytes, std::uint64_t immediate) {
    bytes.push_back(0x48u);
    bytes.push_back(0xbau);
    emit_u64(bytes, immediate);
}

void emit_operand32_to_eax(std::vector<std::uint8_t>& bytes, const R5900IrOperand& operand) {
    if (operand.kind == R5900IrOperandKind::Gpr) {
        emit_load_eax_from_state(bytes, gpr_low64_offset(operand.gpr_index));
        return;
    }
    emit_mov_eax_imm32(bytes, static_cast<std::uint32_t>(operand.immediate));
}

void emit_operand32_to_edx(std::vector<std::uint8_t>& bytes, const R5900IrOperand& operand) {
    if (operand.kind == R5900IrOperandKind::Gpr) {
        emit_load_edx_from_state(bytes, gpr_low64_offset(operand.gpr_index));
        return;
    }
    emit_mov_edx_imm32(bytes, static_cast<std::uint32_t>(operand.immediate));
}

void emit_operand64_to_rax(std::vector<std::uint8_t>& bytes, const R5900IrOperand& operand) {
    if (operand.kind == R5900IrOperandKind::Gpr) {
        emit_load_rax_from_state(bytes, gpr_low64_offset(operand.gpr_index));
        return;
    }
    emit_mov_rax_imm64(bytes, static_cast<std::uint64_t>(operand.immediate));
}

void emit_operand64_to_rdx(std::vector<std::uint8_t>& bytes, const R5900IrOperand& operand) {
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

void emit_add_word_sign_extend(std::vector<std::uint8_t>& bytes,
                               const R5900IrInstruction& instruction) {
    emit_operand32_to_eax(bytes, instruction.inputs[0]);
    emit_operand32_to_edx(bytes, instruction.inputs[1]);

    bytes.push_back(0x01u);
    bytes.push_back(0xd0u);
    bytes.push_back(0x48u);
    bytes.push_back(0x98u);

    if (instruction.destination->index != 0u) {
        emit_store_rax_to_state(bytes, gpr_low64_offset(instruction.destination->index));
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
        emit_store_rax_to_state(bytes, gpr_low64_offset(instruction.destination->index));
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
        emit_store_rax_to_state(bytes, gpr_low64_offset(instruction.destination->index));
    }
}

void emit_lui(std::vector<std::uint8_t>& bytes,
              const R5900IrInstruction& instruction) {
    const auto immediate16 = static_cast<std::uint16_t>(instruction.inputs[0].immediate);
    const auto word = static_cast<std::uint32_t>(immediate16) << 16u;
    emit_mov_eax_imm32(bytes, word);
    bytes.push_back(0x48u);
    bytes.push_back(0x98u);

    if (instruction.destination->index != 0u) {
        emit_store_rax_to_state(bytes, gpr_low64_offset(instruction.destination->index));
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
    emit_load_rax_from_state(bytes, gpr_low64_offset(instruction.inputs[0].gpr_index));
    emit_store_rax_to_state(bytes, hilo_destination_offset(instruction.destination->kind));
}

void emit_mtsah(std::vector<std::uint8_t>& bytes,
                const R5900IrInstruction& instruction) {
    emit_load_eax_from_state(bytes, gpr_low64_offset(instruction.inputs[0].gpr_index));

    bytes.push_back(0x83u);
    bytes.push_back(0xe0u);
    bytes.push_back(0x07u);

    bytes.push_back(0x83u);
    bytes.push_back(0xf0u);
    bytes.push_back(static_cast<std::uint8_t>(instruction.inputs[1].immediate) & 0x07u);

    bytes.push_back(0xd1u);
    bytes.push_back(0xe0u);

    emit_store_eax_to_state(bytes, sa_offset());
}

void emit_load_xmm0_gpr(std::vector<std::uint8_t>& bytes, std::uint8_t index) {
    bytes.push_back(0xf3u);
    bytes.push_back(0x0fu);
    bytes.push_back(0x6fu);
    bytes.push_back(0x81u);
    emit_u32(bytes, gpr_offset(index));
}

void emit_load_xmm1_gpr(std::vector<std::uint8_t>& bytes, std::uint8_t index) {
    bytes.push_back(0xf3u);
    bytes.push_back(0x0fu);
    bytes.push_back(0x6fu);
    bytes.push_back(0x89u);
    emit_u32(bytes, gpr_offset(index));
}

void emit_movd_eax_xmm0(std::vector<std::uint8_t>& bytes) {
    bytes.push_back(0x66u);
    bytes.push_back(0x0fu);
    bytes.push_back(0x7eu);
    bytes.push_back(0xc0u);
}

void emit_movd_edx_xmm1(std::vector<std::uint8_t>& bytes) {
    bytes.push_back(0x66u);
    bytes.push_back(0x0fu);
    bytes.push_back(0x7eu);
    bytes.push_back(0xcau);
}

void emit_shift_xmm_sources_one_lane(std::vector<std::uint8_t>& bytes) {
    bytes.push_back(0x66u);
    bytes.push_back(0x0fu);
    bytes.push_back(0x73u);
    bytes.push_back(0xd8u);
    bytes.push_back(0x04u);

    bytes.push_back(0x66u);
    bytes.push_back(0x0fu);
    bytes.push_back(0x73u);
    bytes.push_back(0xd9u);
    bytes.push_back(0x04u);
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
                gpr_offset(instruction.destination->index) + lane * sizeof(std::uint32_t));
        }

        if (lane != 3u) {
            emit_shift_xmm_sources_one_lane(bytes);
        }
    }
}

void emit_move_bits32(std::vector<std::uint8_t>& bytes,
                      const R5900IrInstruction& instruction) {
    emit_load_eax_from_state(bytes, gpr_low64_offset(instruction.inputs[0].gpr_index));
    const auto destination_offset =
        instruction.destination->kind == R5900IrDestinationKind::Fpr
            ? fpr_offset(instruction.destination->index)
            : fcr31_offset();
    emit_store_eax_to_state(bytes, destination_offset);
}

void emit_load_xmm0_f32(std::vector<std::uint8_t>& bytes, std::uint32_t displacement) {
    bytes.push_back(0xf3u);
    bytes.push_back(0x0fu);
    bytes.push_back(0x10u);
    bytes.push_back(0x81u);
    emit_u32(bytes, displacement);
}

void emit_add_xmm0_f32(std::vector<std::uint8_t>& bytes, std::uint32_t displacement) {
    bytes.push_back(0xf3u);
    bytes.push_back(0x0fu);
    bytes.push_back(0x58u);
    bytes.push_back(0x81u);
    emit_u32(bytes, displacement);
}

void emit_store_xmm0_f32(std::vector<std::uint8_t>& bytes, std::uint32_t displacement) {
    bytes.push_back(0xf3u);
    bytes.push_back(0x0fu);
    bytes.push_back(0x11u);
    bytes.push_back(0x81u);
    emit_u32(bytes, displacement);
}

void emit_add_f32_to_accumulator(std::vector<std::uint8_t>& bytes,
                                 const R5900IrInstruction& instruction) {
    emit_load_xmm0_f32(bytes, fpr_offset(instruction.inputs[0].gpr_index));
    emit_add_xmm0_f32(bytes, fpr_offset(instruction.inputs[1].gpr_index));
    emit_store_xmm0_f32(bytes, fp_acc_offset());
}

R5900X64CompileResult failure(R5900X64CompileError error, std::string message) {
    R5900X64CompileResult result{};
    result.error = error;
    result.message = std::move(message);
    return result;
}

R5900X64CompileResult unsupported_backend_opcode(std::size_t index,
                                                  const R5900IrInstruction& instruction) {
    std::ostringstream message;
    message << "IR instruction " << index << " at guest PC 0x" << std::hex
            << instruction.guest_pc << ": opcode not implemented by x64 backend";
    return failure(R5900X64CompileError::UnsupportedOpcode, message.str());
}

R5900X64CompileResult compile_linear_block(
    const std::vector<R5900IrInstruction>& instructions,
    std::uint32_t fallthrough_pc) {
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        const auto validation = validate_r5900_ir_instruction(instructions[index], index);
        if (!validation.ok()) {
            return failure(map_validation_error(validation.error), validation.message);
        }
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(80u + instructions.size() * 64u);
    emit_zero_gpr0(bytes);

    for (std::size_t index = 0; index < instructions.size(); ++index) {
        const auto& instruction = instructions[index];
        switch (instruction.opcode) {
        case R5900IrOpcode::Nop:
            break;
        case R5900IrOpcode::AddWordSignExtend:
            emit_add_word_sign_extend(bytes, instruction);
            break;
        case R5900IrOpcode::Or64:
            emit_or64(bytes, instruction);
            break;
        case R5900IrOpcode::And64:
            emit_and64(bytes, instruction);
            break;
        case R5900IrOpcode::LoadUpperImmediateSignExtend:
            emit_lui(bytes, instruction);
            break;
        case R5900IrOpcode::MoveGprLow64:
            emit_move_gpr_low64(bytes, instruction);
            break;
        case R5900IrOpcode::ComputeMtsah:
            emit_mtsah(bytes, instruction);
            break;
        case R5900IrOpcode::AddPackedU32Saturate128:
            emit_padduw(bytes, instruction);
            break;
        case R5900IrOpcode::MoveBits32:
            emit_move_bits32(bytes, instruction);
            break;
        case R5900IrOpcode::AddF32ToAccumulator:
            emit_add_f32_to_accumulator(bytes, instruction);
            break;
        default:
            return unsupported_backend_opcode(index, instruction);
        }
    }

    emit_zero_gpr0(bytes);
    emit_mov_eax_imm32(bytes, fallthrough_pc);
    bytes.push_back(0xc3u);

    void* code = VirtualAlloc(nullptr,
                              bytes.size(),
                              MEM_RESERVE | MEM_COMMIT,
                              PAGE_READWRITE);
    if (code == nullptr) {
        return failure(R5900X64CompileError::AllocationFailed,
                       "VirtualAlloc failed for R5900 x64 block");
    }

    std::memcpy(code, bytes.data(), bytes.size());

    DWORD previous_protection{};
    if (!VirtualProtect(code,
                        bytes.size(),
                        PAGE_EXECUTE_READ,
                        &previous_protection)) {
        VirtualFree(code, 0u, MEM_RELEASE);
        return failure(R5900X64CompileError::ProtectionFailed,
                       "VirtualProtect failed for R5900 x64 block");
    }

    if (!FlushInstructionCache(GetCurrentProcess(), code, bytes.size())) {
        VirtualFree(code, 0u, MEM_RELEASE);
        return failure(R5900X64CompileError::CacheFlushFailed,
                       "FlushInstructionCache failed for R5900 x64 block");
    }

    R5900X64CompiledBlock block(code, bytes.size());
    R5900X64CompileResult result{};
    result.block.emplace(std::move(block));
    return result;
}

} // namespace

R5900X64CompiledBlock::R5900X64CompiledBlock(void* code, std::size_t size) noexcept
    : code_(code), size_(size) {}

R5900X64CompiledBlock::~R5900X64CompiledBlock() {
    release();
}

R5900X64CompiledBlock::R5900X64CompiledBlock(R5900X64CompiledBlock&& other) noexcept
    : code_(std::exchange(other.code_, nullptr)),
      size_(std::exchange(other.size_, 0u)) {}

R5900X64CompiledBlock& R5900X64CompiledBlock::operator=(R5900X64CompiledBlock&& other) noexcept {
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

std::uint32_t R5900X64CompiledBlock::execute(
    R5900IrExecutionState& state) const noexcept {
    using GeneratedFunction = std::uint32_t (*)(R5900IrExecutionState*);
    const auto function = reinterpret_cast<GeneratedFunction>(code_);
    return function(&state);
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
    return compile_linear_block(instructions, fallthrough_pc);
}

R5900X64CompileResult compile_r5900_ir_x64(const R5900IrBlock& block) {
    const auto validation = validate_r5900_ir_block(block);
    if (!validation.ok()) {
        return failure(map_validation_error(validation.error), validation.message);
    }
    if (block.terminator.kind != R5900IrTerminatorKind::Fallthrough) {
        return failure(R5900X64CompileError::UnsupportedOpcode,
                       "R5900 x64 backend does not yet emit branch terminators");
    }
    return compile_linear_block(block.body, block.terminator.fallthrough_pc);
}

} // namespace b3r::recompiler
