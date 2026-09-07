#include "recompiler/r5900_decoder.h"
#include "recompiler/r5900_ir.h"
#include "recompiler/r5900_ir_executor.h"
#include "recompiler/r5900_ir_validation.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_and_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

constexpr std::uint32_t r_type(std::uint8_t rs,
                               std::uint8_t rt,
                               std::uint8_t rd,
                               std::uint8_t sa,
                               std::uint8_t funct) {
    return (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) |
           funct;
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    const auto word = r_type(3u, 4u, 5u, 0u, 0x24u); // AND r5,r3,r4
    const auto lowered = lower_r5900_instruction(decode_r5900(word), 0x00100154u);
    expect(lowered.ok(), "AND must lower for BEQ startup continuation");
    expect(lowered.instructions.size() == 1u, "AND must lower to one IR instruction");

    const auto& ir = lowered.instructions.front();
    expect(ir.opcode == R5900IrOpcode::And64, "AND must lower to And64");
    expect(ir.destination.has_value() &&
               ir.destination->kind == R5900IrDestinationKind::Gpr &&
               ir.destination->index == 5u,
           "AND destination must be rd");
    expect(ir.write_mode == R5900IrGprWriteMode::Low64PreserveUpper64,
           "AND must preserve GPR high64");
    expect(ir.inputs.size() == 2u &&
               ir.inputs[0].kind == R5900IrOperandKind::Gpr &&
               ir.inputs[0].gpr_index == 3u &&
               ir.inputs[1].kind == R5900IrOperandKind::Gpr &&
               ir.inputs[1].gpr_index == 4u,
           "AND sources must be rs and rt GPRs");
    expect(ir.guest_pc == 0x00100154u && ir.guest_raw == word,
           "AND must retain guest provenance");

    expect(validate_r5900_ir_instruction(ir, 0u).ok(),
           "And64 GPR+GPR must validate");

    R5900IrExecutionState state{};
    state.gpr[3] = {0x00ff00ff00ff00ffull, 0x1111111111111111ull};
    state.gpr[4] = {0x0f0f0f0f0f0f0f0full, 0x2222222222222222ull};
    state.gpr[5] = {0u, 0xaaaaaaaaaaaaaaaaull};
    expect(execute_r5900_ir(lowered.instructions, state).ok(),
           "reference executor must accept register AND IR");
    expect(state.gpr[5].low64 == 0x000f000f000f000full,
           "register AND must combine both GPR low64 operands");
    expect(state.gpr[5].high64 == 0xaaaaaaaaaaaaaaaaull,
           "register AND must preserve destination high64");

    const auto zero_word = r_type(3u, 4u, 0u, 0u, 0x24u); // AND r0,r3,r4
    const auto zero_lowered = lower_r5900_instruction(decode_r5900(zero_word), 0x00100158u);
    expect(zero_lowered.ok() && zero_lowered.instructions.size() == 1u,
           "AND writing r0 must lower deterministically");
    expect(zero_lowered.instructions.front().opcode == R5900IrOpcode::Nop,
           "AND writing r0 must become provenance-preserving Nop");
    expect(zero_lowered.instructions.front().guest_raw == zero_word,
           "discarded AND write must retain guest word provenance");

    const auto or_word = r_type(6u, 7u, 8u, 0u, 0x25u); // OR r8,r6,r7
    constexpr std::uint32_t or_test_pc = 0x00001000u;
    const auto or_lowered = lower_r5900_instruction(decode_r5900(or_word), or_test_pc);
    expect(or_lowered.ok(), "register OR must lower");
    expect(or_lowered.instructions.size() == 1u, "OR must lower to one IR instruction");

    const auto& or_ir = or_lowered.instructions.front();
    expect(or_ir.opcode == R5900IrOpcode::Or64, "OR must lower to Or64");
    expect(or_ir.destination.has_value() &&
               or_ir.destination->kind == R5900IrDestinationKind::Gpr &&
               or_ir.destination->index == 8u,
           "OR destination must be rd");
    expect(or_ir.write_mode == R5900IrGprWriteMode::Low64PreserveUpper64,
           "OR must preserve GPR high64");
    expect(or_ir.inputs.size() == 2u &&
               or_ir.inputs[0].kind == R5900IrOperandKind::Gpr &&
               or_ir.inputs[0].gpr_index == 6u &&
               or_ir.inputs[1].kind == R5900IrOperandKind::Gpr &&
               or_ir.inputs[1].gpr_index == 7u,
           "OR sources must be rs and rt GPRs");
    expect(or_ir.guest_pc == or_test_pc && or_ir.guest_raw == or_word,
           "OR must retain guest provenance");
    expect(validate_r5900_ir_instruction(or_ir, 0u).ok(),
           "Or64 GPR+GPR must validate");

    R5900IrExecutionState or_state{};
    or_state.gpr[6] = {0x00ff00000000ff00ull, 0x1111111111111111ull};
    or_state.gpr[7] = {0x0f000f000f00000full, 0x2222222222222222ull};
    or_state.gpr[8] = {0u, 0xbbbbbbbbbbbbbbbbull};
    expect(execute_r5900_ir(or_lowered.instructions, or_state).ok(),
           "reference executor must accept register OR IR");
    expect(or_state.gpr[8].low64 == 0x0fff0f000f00ff0full,
           "register OR must combine both GPR low64 operands");
    expect(or_state.gpr[8].high64 == 0xbbbbbbbbbbbbbbbbull,
           "register OR must preserve destination high64");

    const auto or_zero_word = r_type(6u, 7u, 0u, 0u, 0x25u); // OR r0,r6,r7
    const auto or_zero_lowered =
        lower_r5900_instruction(decode_r5900(or_zero_word), or_test_pc + 4u);
    expect(or_zero_lowered.ok() && or_zero_lowered.instructions.size() == 1u,
           "OR writing r0 must lower deterministically");
    expect(or_zero_lowered.instructions.front().opcode == R5900IrOpcode::Nop,
           "OR writing r0 must become provenance-preserving Nop");
    expect(or_zero_lowered.instructions.front().guest_raw == or_zero_word,
           "discarded OR write must retain guest word provenance");

    std::cout << "r5900_ir_and_tests: PASS\n";
    return EXIT_SUCCESS;
}
