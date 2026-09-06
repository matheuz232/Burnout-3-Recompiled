#include "recompiler/r5900_decoder.h"
#include "recompiler/r5900_ir.h"
#include "recompiler/r5900_ir_validation.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_store64_tests: FAIL: " << message << '\n';
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

b3r::recompiler::R5900IrInstruction valid_store64() {
    using namespace b3r::recompiler;
    R5900IrInstruction ir{};
    ir.guest_pc = 0x0011510cu;
    ir.guest_raw = (0x3fu << 26u) | (29u << 21u) | (31u << 16u) | 0xfff8u;
    ir.opcode = R5900IrOpcode::Store64;
    ir.inputs = {gpr(29u), gpr(31u), immediate(-8)};
    return ir;
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    constexpr std::uint32_t sd_word =
        (0x3fu << 26u) | (29u << 21u) | (31u << 16u) | 0xfff8u;
    const auto decoded = decode_r5900(sd_word);
    expect(decoded.instruction == R5900Instruction::Sd,
           "fixture must decode as SD");

    expect(decoded.instruction_class == R5900InstructionClass::Store &&
               decoded.memory_width == R5900MemoryWidth::Doubleword64,
           "SD must decode as a 64-bit store");

    const auto lowered = lower_r5900_instruction(decoded, 0x0011510cu);
    expect(lowered.ok(), "SD must lower");
    expect(lowered.instructions.size() == 1u,
           "SD must lower to one IR instruction");
    const auto& ir = lowered.instructions.front();
    expect(ir.opcode == R5900IrOpcode::Store64,
           "SD must lower to Store64");
    expect(!ir.destination.has_value(),
           "Store64 must have no destination");
    expect(ir.write_mode == R5900IrGprWriteMode::None,
           "Store64 write mode mismatch");
    expect(ir.inputs.size() == 3u,
           "Store64 operand count mismatch");
    expect(ir.inputs[0].kind == R5900IrOperandKind::Gpr &&
               ir.inputs[0].gpr_index == 29u,
           "Store64 base operand mismatch");
    expect(ir.inputs[1].kind == R5900IrOperandKind::Gpr &&
               ir.inputs[1].gpr_index == 31u,
           "Store64 value operand mismatch");
    expect(ir.inputs[2].kind == R5900IrOperandKind::Immediate &&
               ir.inputs[2].immediate == -8,
           "Store64 signed offset mismatch");
    expect(validate_r5900_ir_instruction(ir, 0u).ok(),
           "lowered Store64 must validate");

    constexpr std::uint32_t sd_zero_word =
        (0x3fu << 26u) | (29u << 21u) | (0u << 16u);
    const auto zero_lowered = lower_r5900_instruction(
        decode_r5900(sd_zero_word), 0x00115110u);
    expect(zero_lowered.ok() && zero_lowered.instructions.size() == 1u,
           "SD with rt=0 must still lower to an observable store");
    expect(zero_lowered.instructions.front().opcode == R5900IrOpcode::Store64,
           "SD with rt=0 must not lower to Nop");
    expect(zero_lowered.instructions.front().inputs[1].gpr_index == 0u,
           "SD with rt=0 must preserve GPR0 source operand");

    {
        auto malformed = valid_store64();
        malformed.destination = R5900IrDestination{3u};
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store64 destination must reject");
    }
    {
        auto malformed = valid_store64();
        malformed.inputs.pop_back();
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store64 wrong operand count must reject");
    }
    {
        auto malformed = valid_store64();
        malformed.inputs[0] = fpr(2u);
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store64 FPR base must reject");
    }
    {
        auto malformed = valid_store64();
        malformed.inputs[1] = fpr(7u);
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store64 FPR value must reject");
    }
    {
        auto malformed = valid_store64();
        malformed.inputs[0] = gpr(32u);
        expect(validate_r5900_ir_instruction(malformed, 0u).error ==
                   R5900IrValidationError::InvalidRegister,
               "Store64 base GPR index 32 must reject as invalid register");
    }
    {
        auto malformed = valid_store64();
        malformed.inputs[1] = gpr(32u);
        expect(validate_r5900_ir_instruction(malformed, 0u).error ==
                   R5900IrValidationError::InvalidRegister,
               "Store64 value GPR index 32 must reject as invalid register");
    }
    {
        auto malformed = valid_store64();
        malformed.inputs[2] = immediate(32768);
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store64 immediate 32768 must reject");
    }
    {
        auto malformed = valid_store64();
        malformed.inputs[2] = immediate(-32769);
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store64 immediate -32769 must reject");
    }

    {
        auto malformed = valid_store64();
        malformed.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store64 GPR write mode must reject");
    }
    {
        auto malformed = valid_store64();
        malformed.inputs[2] = gpr(3u);
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store64 register offset must reject");
    }
    for (const auto count : {0u, 1u, 4u}) {
        auto malformed = valid_store64();
        malformed.inputs.resize(count);
        expect(!validate_r5900_ir_instruction(malformed, 0u).ok(),
               "Store64 must require exactly three operands");
    }
    for (const auto offset : {-32768, 32767}) {
        auto valid = valid_store64();
        valid.inputs[2] = immediate(offset);
        expect(validate_r5900_ir_instruction(valid, 0u).ok(),
               "Store64 must accept signed16 endpoint offsets");
    }

    std::cout << "r5900_ir_store64_tests: PASS\n";
    return EXIT_SUCCESS;
}
