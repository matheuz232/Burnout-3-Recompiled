#pragma once

#include "recompiler/r5900_ir_executor.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace b3r::recompiler {

enum class R5900X64CompileError {
    None = 0,
    MalformedInstruction,
    InvalidRegister,
    UnsupportedOpcode,
    AllocationFailed,
    ProtectionFailed,
    CacheFlushFailed,
};

class R5900X64CompiledBlock;
struct R5900X64CompileResult;

[[nodiscard]] R5900X64CompileResult compile_r5900_ir_x64(
    const std::vector<R5900IrInstruction>& instructions);

[[nodiscard]] R5900X64CompileResult compile_r5900_ir_x64(
    const R5900IrBlock& block);

class R5900X64CompiledBlock {
public:
    R5900X64CompiledBlock() noexcept = default;
    ~R5900X64CompiledBlock();

    R5900X64CompiledBlock(const R5900X64CompiledBlock&) = delete;
    R5900X64CompiledBlock& operator=(const R5900X64CompiledBlock&) = delete;
    R5900X64CompiledBlock(R5900X64CompiledBlock&& other) noexcept;
    R5900X64CompiledBlock& operator=(R5900X64CompiledBlock&& other) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint32_t execute(
        R5900IrExecutionState& state) const noexcept;

private:
    friend R5900X64CompileResult compile_r5900_ir_x64(
        const std::vector<R5900IrInstruction>& instructions);
    friend R5900X64CompileResult compile_r5900_ir_x64(
        const R5900IrBlock& block);

    R5900X64CompiledBlock(void* code, std::size_t size) noexcept;
    void release() noexcept;

    void* code_{};
    std::size_t size_{};
};

struct R5900X64CompileResult {
    R5900X64CompileError error{R5900X64CompileError::None};
    std::string message{};
    std::optional<R5900X64CompiledBlock> block{};

    [[nodiscard]] bool ok() const noexcept {
        return error == R5900X64CompileError::None && block.has_value();
    }
};

[[nodiscard]] inline R5900X64CompileResult compile_r5900_ir_x64(
    std::initializer_list<R5900IrInstruction> instructions) {
    return compile_r5900_ir_x64(
        std::vector<R5900IrInstruction>(instructions.begin(), instructions.end()));
}

} // namespace b3r::recompiler
