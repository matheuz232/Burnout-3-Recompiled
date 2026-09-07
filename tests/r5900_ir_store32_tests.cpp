#include "recompiler/r5900_decoder.h"
#include "recompiler/r5900_ir.h"
#include "recompiler/r5900_ir_validation.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_store32_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) fail(message);
}

R5900IrOperand gpr(std::uint8_t index) {
    R5900IrOperand operand{};
    operand.kind = R5900IrOperandKind::Gpr;
    operand.gpr_index = index;
    return operand;
}

R5900IrOperand fpr(std::uint8_t index) {
    R5900IrOperand operand{};
    operand.kind = R5900IrOperandKind::Fpr;
    operand.gpr_index = index;
    return operand;
}

R5900IrOperand immediate(std::int64_t value) {
    R5900IrOperand operand{};
    operand.kind = R5900IrOperandKind::Immediate;
    operand.immediate = value;
    return operand;
}

constexpr std::uint32_t i_type(std::uint8_t op,
                               std::uint8_t rs,
                               std::uint8_t rt,
                               std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           imm;
}

R5900IrInstruction valid_store32() {
    R5900IrInstruction ir{};
    ir.guest_pc = 0x00114ee0u;
    ir.guest_raw = i_type(0x2bu, 29u, 2u, 0x0028u);
    ir.opcode = R5900IrOpcode::Store32;
    ir.inputs = {gpr(29u), gpr(2u), immediate(0x28)};
    return ir;
}

} // namespace

int main() {
    constexpr std::uint32_t sw_word = i_type(0x2bu, 29u, 2u, 0x0028u);
    const auto decoded = decode_r5900(sw_word);
    expect(decoded.instruction == R5900Instruction::Sw &&
               decoded.instruction_class == R5900InstructionClass::Store &&
               decoded.memory_width == R5900MemoryWidth::Word,
           "fixture must decode as word SW");

    const auto lowered = lower_r5900_instruction(decoded, 0x00114ee0u);
    expect(lowered.ok() && lowered.instructions.size() == 1u,
           "SW must lower to one IR instruction");
    const auto& ir = lowered.instructions.front();
    expect(ir.opcode == R5900IrOpcode::Store32 &&
               ir.guest_pc == 0x00114ee0u && ir.guest_raw == sw_word,
           "SW must lower to provenance-preserving Store32");
    expect(!ir.destination.has_value() &&
               ir.write_mode == R5900IrGprWriteMode::None &&
               ir.inputs.size() == 3u,
           "Store32 destination/write-mode/operand count mismatch");
    expect(ir.inputs[0].kind == R5900IrOperandKind::Gpr &&
               ir.inputs[0].gpr_index == 29u &&
               ir.inputs[1].kind == R5900IrOperandKind::Gpr &&
               ir.inputs[1].gpr_index == 2u &&
               ir.inputs[2].kind == R5900IrOperandKind::Immediate &&
               ir.inputs[2].immediate == 0x28,
           "Store32 operand lowering mismatch");
    expect(validate_r5900_ir_instruction(ir, 0u).ok(),
           "lowered Store32 must validate");

    constexpr std::uint32_t sw_zero_word = i_type(0x2bu, 0u, 0u, 0xfffcu);
    const auto zero = lower_r5900_instruction(
        decode_r5900(sw_zero_word), 0x00114ee4u);
    expect(zero.ok() && zero.instructions.size() == 1u &&
               zero.instructions.front().opcode == R5900IrOpcode::Store32 &&
               zero.instructions.front().inputs[0].gpr_index == 0u &&
               zero.instructions.front().inputs[1].gpr_index == 0u &&
               zero.instructions.front().inputs[2].immediate == -4,
           "SW with GPR0 base/value must remain an observable store");

    {
        auto malformed = valid_store32();
        malformed.destination = R5900IrDestination{3u};
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store32 destination must reject");
    }
    {
        auto malformed = valid_store32();
        malformed.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store32 write mode must reject");
    }
    {
        auto malformed = valid_store32();
        malformed.inputs[0] = fpr(1u);
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store32 FPR base must reject");
    }
    {
        auto malformed = valid_store32();
        malformed.inputs[1] = fpr(2u);
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store32 FPR value must reject");
    }
    for (const auto index : {0u, 1u}) {
        auto malformed = valid_store32();
        malformed.inputs[index] = gpr(32u);
        expect(validate_r5900_ir_instruction(malformed, 0u).error ==
                   R5900IrValidationError::InvalidRegister,
               "Store32 invalid GPR must reject");
    }
    for (const auto count : {0u, 1u, 2u, 4u}) {
        auto malformed = valid_store32();
        malformed.inputs.resize(count);
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store32 must require exactly three operands");
    }
    for (const auto offset : {-32768, 32767}) {
        auto valid = valid_store32();
        valid.inputs[2] = immediate(offset);
        expect(validate_r5900_ir_instruction(valid, 0u).ok(),
               "Store32 must accept signed16 endpoints");
    }
    for (const auto offset : {-32769, 32768}) {
        auto malformed = valid_store32();
        malformed.inputs[2] = immediate(offset);
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store32 must reject offsets outside signed16");
    }

    std::cout << "r5900_ir_store32_tests: PASS\n";
    return EXIT_SUCCESS;
}
