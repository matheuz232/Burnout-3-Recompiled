#include "recompiler/r5900_ir_executor.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_executor_tests: FAIL: " << message << '\n';
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

    R5900IrExecutionState state{};
    state.gpr[1].low64 = 0x1122334455667788ull;
    state.gpr[1].high64 = 0x8877665544332211ull;

    R5900IrInstruction nop{};
    nop.guest_pc = 0x00100000u;
    nop.opcode = R5900IrOpcode::Nop;

    const auto result = execute_r5900_ir(std::vector<R5900IrInstruction>{nop}, state);
    expect(result.ok(), "valid Nop must execute successfully");
    expect(state.gpr[1].low64 == 0x1122334455667788ull, "Nop must preserve low64");
    expect(state.gpr[1].high64 == 0x8877665544332211ull, "Nop must preserve high64");

    std::cout << "r5900_ir_executor_tests: PASS\n";
    return EXIT_SUCCESS;
}
