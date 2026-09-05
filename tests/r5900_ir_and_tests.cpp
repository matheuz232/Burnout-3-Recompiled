#include "recompiler/r5900_decoder.h"
#include "recompiler/r5900_ir.h"
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

    const auto zero_word = r_type(3u, 4u, 0u, 0u, 0x24u); // AND r0,r3,r4
    const auto zero_lowered = lower_r5900_instruction(decode_r5900(zero_word), 0x00100158u);
    expect(zero_lowered.ok() && zero_lowered.instructions.size() == 1u,
           "AND writing r0 must lower deterministically");
    expect(zero_lowered.instructions.front().opcode == R5900IrOpcode::Nop,
           "AND writing r0 must become provenance-preserving Nop");
    expect(zero_lowered.instructions.front().guest_raw == zero_word,
           "discarded AND write must retain guest word provenance");

    std::cout << "r5900_ir_and_tests: PASS\n";
    return EXIT_SUCCESS;
}
