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

constexpr std::uint32_t gpr_low64_offset(std::uint8_t index) {
    return static_cast<std::uint32_t>(index) * 16u;
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
    emit_store_rax_to_state(bytes, 0u);
    emit_store_rax_to_state(bytes, 8u);
}

void emit_add_word_sign_extend(std::vector<std::uint8_t>& bytes,
                               const R5900IrInstruction& instruction) {
    emit_operand32_to_eax(bytes, instruction.inputs[0]);
    emit_operand32_to_edx(bytes, instruction.inputs[1]);

    bytes.push_back(0x01u);
    bytes.push_back(0xd0u); // add eax, edx
    bytes.push_back(0x48u);
    bytes.push_back(0x98u); // cdqe

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
    bytes.push_back(0xd0u); // or rax, rdx

    if (instruction.destination->index != 0u) {
        emit_store_rax_to_state(bytes, gpr_low64_offset(instruction.destination->index));
    }
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

void R5900X64CompiledBlock::execute(R5900IrExecutionState& state) const noexcept {
    using GeneratedFunction = void (*)(R5900IrExecutionState*);
    const auto function = reinterpret_cast<GeneratedFunction>(code_);
    function(&state);
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
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        const auto validation = validate_r5900_ir_instruction(instructions[index], index);
        if (!validation.ok()) {
            return failure(map_validation_error(validation.error), validation.message);
        }
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(32u + instructions.size() * 32u);
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
        default:
            return unsupported_backend_opcode(index, instruction);
        }
    }

    emit_zero_gpr0(bytes);
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

} // namespace b3r::recompiler
