#include "recompiler/windows/r5900_x64_backend.h"

#include <cstdlib>
#include <iostream>
#include <type_traits>
#include <vector>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_x64_backend_windows_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    static_assert(std::is_move_constructible_v<R5900X64CompiledBlock>);
    static_assert(std::is_move_assignable_v<R5900X64CompiledBlock>);
    static_assert(!std::is_copy_constructible_v<R5900X64CompiledBlock>);
    static_assert(!std::is_copy_assignable_v<R5900X64CompiledBlock>);

    {
        R5900IrExecutionState state{};
        state.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
        state.gpr[1] = {0x1122334455667788ull, 0x8877665544332211ull};

        auto compiled = compile_r5900_ir_x64({});
        expect(compiled.ok() && compiled.block.has_value(),
               "empty program must compile to a callable block");
        expect(compiled.block->valid(), "compiled block must report valid ownership");
        compiled.block->execute(state);

        expect(state.gpr[0].low64 == 0 && state.gpr[0].high64 == 0,
               "native block must normalize GPR0");
        expect(state.gpr[1].low64 == 0x1122334455667788ull &&
                   state.gpr[1].high64 == 0x8877665544332211ull,
               "empty native block must preserve nonzero GPRs");
    }

    {
        R5900IrInstruction nop{};
        nop.guest_pc = 0x00103000u;
        nop.opcode = R5900IrOpcode::Nop;

        R5900IrExecutionState state{};
        state.gpr[7] = {0x0123456789abcdefull, 0xfedcba9876543210ull};

        auto compiled = compile_r5900_ir_x64(std::vector<R5900IrInstruction>{nop});
        expect(compiled.ok() && compiled.block.has_value(), "valid Nop must compile");
        compiled.block->execute(state);
        expect(state.gpr[7].low64 == 0x0123456789abcdefull &&
                   state.gpr[7].high64 == 0xfedcba9876543210ull,
               "native Nop must preserve GPR state");
    }

    std::cout << "r5900_x64_backend_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
