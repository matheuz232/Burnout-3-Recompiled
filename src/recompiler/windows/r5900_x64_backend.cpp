#include "recompiler/windows/r5900_x64_backend.h"

#include "recompiler/r5900_ir_validation.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
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

void emit_zero_gpr0(std::vector<std::uint8_t>& bytes) {
    emit_xor_eax_eax(bytes);
    emit_store_rax_to_state(bytes, 0u);
    emit_store_rax_to_state(bytes, 8u);
}

R5900X64CompileResult failure(R5900X64CompileError error, std::string message) {
    R5900X64CompileResult result{};
    result.error = error;
    result.message = std::move(message);
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

    for (const auto& instruction : instructions) {
        if (instruction.opcode != R5900IrOpcode::Nop) {
            return failure(R5900X64CompileError::UnsupportedOpcode,
                           "R5900 x64 backend v0 foundation only emits Nop");
        }
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(32u);

    emit_zero_gpr0(bytes);
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

    R5900X64CompileResult result{};
    result.block.emplace(code, bytes.size());
    return result;
}

} // namespace b3r::recompiler
