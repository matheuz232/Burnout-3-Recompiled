#include "recompiler/r5900_decoder.h"
#include "recompiler/r5900_ir.h"
#include "recompiler/r5900_ir_validation.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_store128_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

b3r::recompiler::R5900IrOperand gpr(std::uint8_t index) {
    b3r::recompiler::R5900IrOperand operand{};
    operand.kind = b3r::recompiler::R5900IrOperandKind::Gpr;
    operand.gpr_index = index;
    return operand;
}

b3r::recompiler::R5900IrOperand fpr(std::uint8_t index) {
    b3r::recompiler::R5900IrOperand operand{};
    operand.kind = b3r::recompiler::R5900IrOperandKind::Fpr;
    operand.gpr_index = index;
    return operand;
}

b3r::recompiler::R5900IrOperand immediate(std::int64_t value) {
    b3r::recompiler::R5900IrOperand operand{};
    operand.kind = b3r::recompiler::R5900IrOperandKind::Immediate;
    operand.immediate = value;
    return operand;
}

b3r::recompiler::R5900IrInstruction valid_store128() {
    using namespace b3r::recompiler;
    R5900IrInstruction ir{};
    ir.guest_pc = 0x00100160u;
    ir.guest_raw = 0x7c400000u;
    ir.opcode = R5900IrOpcode::Store128;
    ir.inputs = {gpr(2u), gpr(7u), immediate(-16)};
    return ir;
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    constexpr std::uint32_t sq_word =
        (0x1fu << 26u) | (2u << 21u) | (7u << 16u) | 0xfff0u;
    const auto decoded = decode_r5900(sq_word);
    expect(decoded.instruction == R5900Instruction::Sq,
           "fixture must decode as SQ");

    const auto lowered = lower_r5900_instruction(decoded, 0x00100160u);
    expect(lowered.ok(), "SQ must lower");
    expect(lowered.instructions.size() == 1u,
           "SQ must lower to one IR instruction");
    const auto& ir = lowered.instructions.front();
    expect(ir.opcode == R5900IrOpcode::Store128,
           "SQ must lower to Store128");
    expect(!ir.destination.has_value(),
           "Store128 must have no destination");
    expect(ir.write_mode == R5900IrGprWriteMode::None,
           "Store128 write mode mismatch");
    expect(ir.inputs.size() == 3u,
           "Store128 operand count mismatch");
    expect(ir.inputs[0].kind == R5900IrOperandKind::Gpr &&
               ir.inputs[0].gpr_index == 2u,
           "Store128 base operand mismatch");
    expect(ir.inputs[1].kind == R5900IrOperandKind::Gpr &&
               ir.inputs[1].gpr_index == 7u,
           "Store128 value operand mismatch");
    expect(ir.inputs[2].kind == R5900IrOperandKind::Immediate &&
               ir.inputs[2].immediate == -16,
           "Store128 signed offset mismatch");
    expect(validate_r5900_ir_instruction(ir, 0u).ok(),
           "lowered Store128 must validate");

    constexpr std::uint32_t sq_zero_word =
        (0x1fu << 26u) | (2u << 21u) | (0u << 16u);
    const auto zero_lowered = lower_r5900_instruction(
        decode_r5900(sq_zero_word), 0x00100164u);
    expect(zero_lowered.ok() && zero_lowered.instructions.size() == 1u,
           "SQ with rt=0 must still lower to an observable store");
    expect(zero_lowered.instructions.front().opcode == R5900IrOpcode::Store128,
           "SQ with rt=0 must not lower to Nop");
    expect(zero_lowered.instructions.front().inputs[1].gpr_index == 0u,
           "SQ with rt=0 must preserve GPR0 source operand");

    {
        auto malformed = valid_store128();
        malformed.destination = R5900IrDestination{3u};
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store128 destination must reject");
    }
    {
        auto malformed = valid_store128();
        malformed.inputs.pop_back();
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store128 wrong operand count must reject");
    }
    {
        auto malformed = valid_store128();
        malformed.inputs[0] = fpr(2u);
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store128 FPR base must reject");
    }
    {
        auto malformed = valid_store128();
        malformed.inputs[1] = fpr(7u);
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store128 FPR value must reject");
    }
    {
        auto malformed = valid_store128();
        malformed.inputs[0] = gpr(32u);
        expect(validate_r5900_ir_instruction(malformed, 0u).error ==
                   R5900IrValidationError::InvalidRegister,
               "Store128 base GPR index 32 must reject as invalid register");
    }
    {
        auto malformed = valid_store128();
        malformed.inputs[1] = gpr(32u);
        expect(validate_r5900_ir_instruction(malformed, 0u).error ==
                   R5900IrValidationError::InvalidRegister,
               "Store128 value GPR index 32 must reject as invalid register");
    }
    {
        auto malformed = valid_store128();
        malformed.inputs[2] = immediate(32768);
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store128 immediate 32768 must reject");
    }
    {
        auto malformed = valid_store128();
        malformed.inputs[2] = immediate(-32769);
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store128 immediate -32769 must reject");
    }

    std::cout << "r5900_ir_store128_tests: PASS\n";
    return EXIT_SUCCESS;
}
