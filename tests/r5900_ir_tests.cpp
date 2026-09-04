#include "recompiler/r5900_decoder.h"
#include "recompiler/r5900_ir.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_tests: FAIL: " << message << '\n';
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

constexpr std::uint32_t i_type(std::uint8_t op,
                               std::uint8_t rs,
                               std::uint8_t rt,
                               std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           imm;
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    {
        const auto result = lower_r5900_instruction(decode_r5900(0u), 0x00100000u);
        expect(result.ok(), "NOP must lower successfully");
        expect(result.instructions.size() == 1u, "NOP lowering must emit one provenance-carrying IR instruction");
        expect(result.instructions[0].opcode == R5900IrOpcode::Nop, "NOP must lower to IR Nop");
        expect(result.instructions[0].write_mode == R5900IrGprWriteMode::None,
               "NOP must not claim a GPR write");
        expect(result.instructions[0].guest_pc == 0x00100000u, "IR must retain the guest PC");
        expect(result.instructions[0].guest_raw == 0u, "IR must retain the original guest word");
    }

    {
        const auto decoded = decode_r5900(r_type(9, 10, 8, 0, 0x21)); // addu t0,t1,t2
        const auto result = lower_r5900_instruction(decoded, 0x00100010u);
        expect(result.ok(), "ADDU must lower successfully");
        expect(result.instructions.size() == 1u, "ADDU must lower to one semantic IR operation");

        const auto& ir = result.instructions[0];
        expect(ir.opcode == R5900IrOpcode::AddWordSignExtend, "ADDU must preserve 32-bit add/sign-extend semantics");
        expect(ir.destination.has_value() && ir.destination->index == 8u, "ADDU destination must be rd");
        expect(ir.write_mode == R5900IrGprWriteMode::Low64PreserveUpper64,
               "ADDU must explicitly preserve the upper 64 bits of the 128-bit EE GPR");
        expect(ir.inputs.size() == 2u, "ADDU must have two inputs");
        expect(ir.inputs[0].kind == R5900IrOperandKind::Gpr && ir.inputs[0].gpr_index == 9u,
               "ADDU first input must be rs");
        expect(ir.inputs[1].kind == R5900IrOperandKind::Gpr && ir.inputs[1].gpr_index == 10u,
               "ADDU second input must be rt");
    }

    {
        const auto decoded = decode_r5900(i_type(0x09, 29, 29, 0xFFF0)); // addiu sp,sp,-16
        const auto result = lower_r5900_instruction(decoded, 0x00100020u);
        expect(result.ok(), "ADDIU must lower successfully");

        const auto& ir = result.instructions[0];
        expect(ir.opcode == R5900IrOpcode::AddWordSignExtend, "ADDIU must share non-trapping word-add semantics");
        expect(ir.destination.has_value() && ir.destination->index == 29u, "ADDIU destination must be rt");
        expect(ir.write_mode == R5900IrGprWriteMode::Low64PreserveUpper64,
               "ADDIU must explicitly preserve the upper 64 bits of the 128-bit EE GPR");
        expect(ir.inputs.size() == 2u, "ADDIU must have register plus immediate inputs");
        expect(ir.inputs[0].kind == R5900IrOperandKind::Gpr && ir.inputs[0].gpr_index == 29u,
               "ADDIU register input must be rs");
        expect(ir.inputs[1].kind == R5900IrOperandKind::Immediate && ir.inputs[1].immediate == -16,
               "ADDIU immediate must be sign-extended before entering IR");
    }

    {
        const auto decoded = decode_r5900(i_type(0x0D, 4, 5, 0xFF00)); // ori a1,a0,0xff00
        const auto result = lower_r5900_instruction(decoded, 0x00100030u);
        expect(result.ok(), "ORI must lower successfully");

        const auto& ir = result.instructions[0];
        expect(ir.opcode == R5900IrOpcode::Or64, "ORI must lower to 64-bit OR semantics");
        expect(ir.destination.has_value() && ir.destination->index == 5u, "ORI destination must be rt");
        expect(ir.write_mode == R5900IrGprWriteMode::Low64PreserveUpper64,
               "ORI must explicitly preserve the upper 64 bits of the 128-bit EE GPR");
        expect(ir.inputs.size() == 2u, "ORI must have register plus immediate inputs");
        expect(ir.inputs[1].kind == R5900IrOperandKind::Immediate && ir.inputs[1].immediate == 0xFF00,
               "ORI immediate must be zero-extended before entering IR");
    }

    {
        const auto decoded = decode_r5900(r_type(9, 10, 0, 0, 0x21)); // addu zero,t1,t2
        const auto result = lower_r5900_instruction(decoded, 0x00100034u);
        expect(result.ok(), "ADDU targeting GPR zero must still lower successfully");
        expect(result.instructions.size() == 1u, "discarded GPR-zero write must retain one provenance IR site");
        expect(result.instructions[0].opcode == R5900IrOpcode::Nop,
               "side-effect-free ADDU targeting GPR zero must lower to Nop");
        expect(!result.instructions[0].destination.has_value(), "discarded GPR-zero write must have no destination");
        expect(result.instructions[0].write_mode == R5900IrGprWriteMode::None,
               "discarded GPR-zero write must not claim a write mode");
    }

    {
        const auto decoded = decode_r5900(i_type(0x0D, 4, 0, 0xFF00)); // ori zero,a0,0xff00
        const auto result = lower_r5900_instruction(decoded, 0x00100038u);
        expect(result.ok(), "ORI targeting GPR zero must still lower successfully");
        expect(result.instructions.size() == 1u && result.instructions[0].opcode == R5900IrOpcode::Nop,
               "side-effect-free ORI targeting GPR zero must lower to Nop");
    }

    {
        const auto decoded = decode_r5900(0x70000000u); // unsupported MMI primary opcode
        const auto result = lower_r5900_instruction(decoded, 0x00100040u);
        expect(!result.ok(), "unsupported guest instruction must not lower successfully");
        expect(result.error == R5900IrLoweringError::UnsupportedInstruction,
               "unsupported lowering must use an explicit error code");
        expect(result.instructions.empty(), "unsupported lowering must not emit partial IR");
    }

    std::cout << "r5900_ir_tests: PASS\n";
    return EXIT_SUCCESS;
}
