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

constexpr std::uint32_t mmi_type(std::uint8_t rs,
                                 std::uint8_t rt,
                                 std::uint8_t rd,
                                 std::uint8_t sa,
                                 std::uint8_t funct) {
    return (0x1cu << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) |
           funct;
}

constexpr std::uint32_t cop1_type(std::uint8_t rs,
                                  std::uint8_t rt,
                                  std::uint8_t rd,
                                  std::uint8_t sa,
                                  std::uint8_t funct) {
    return (0x11u << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) |
           funct;
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
        expect(ir.destination.has_value() && ir.destination->kind == R5900IrDestinationKind::Gpr &&
                   ir.destination->index == 8u,
               "ADDU destination must be rd GPR");
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
        expect(ir.destination.has_value() && ir.destination->kind == R5900IrDestinationKind::Gpr &&
                   ir.destination->index == 29u,
               "ADDIU destination must be rt GPR");
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
        expect(ir.destination.has_value() && ir.destination->kind == R5900IrDestinationKind::Gpr &&
                   ir.destination->index == 5u,
               "ORI destination must be rt GPR");
        expect(ir.write_mode == R5900IrGprWriteMode::Low64PreserveUpper64,
               "ORI must explicitly preserve the upper 64 bits of the 128-bit EE GPR");
        expect(ir.inputs.size() == 2u, "ORI must have register plus immediate inputs");
        expect(ir.inputs[1].kind == R5900IrOperandKind::Immediate && ir.inputs[1].immediate == 0xFF00,
               "ORI immediate must be zero-extended before entering IR");
    }

    {
        const auto word = i_type(0x0c, 1, 2, 0x00ff); // andi r2,r1,0xff
        const auto result = lower_r5900_instruction(decode_r5900(word), 0x00101000u);
        expect(result.ok(), "ANDI must lower for startup execution");
        const auto& ir = result.instructions.at(0);
        expect(ir.opcode == R5900IrOpcode::And64, "ANDI must lower to And64");
        expect(ir.destination.has_value() && ir.destination->kind == R5900IrDestinationKind::Gpr &&
                   ir.destination->index == 2u,
               "ANDI destination mismatch");
        expect(ir.write_mode == R5900IrGprWriteMode::Low64PreserveUpper64,
               "ANDI must preserve upper64");
        expect(ir.inputs.size() == 2u && ir.inputs[0].kind == R5900IrOperandKind::Gpr &&
                   ir.inputs[0].gpr_index == 1u && ir.inputs[1].kind == R5900IrOperandKind::Immediate &&
                   ir.inputs[1].immediate == 0x00ff,
               "ANDI operands/immediate mismatch");
        expect(ir.guest_pc == 0x00101000u && ir.guest_raw == word,
               "ANDI must preserve provenance");
    }

    {
        const auto word = i_type(0x0f, 0, 2, 0x8040); // lui r2,0x8040
        const auto result = lower_r5900_instruction(decode_r5900(word), 0x00101004u);
        expect(result.ok(), "LUI must lower for startup execution");
        const auto& ir = result.instructions.at(0);
        expect(ir.opcode == R5900IrOpcode::LoadUpperImmediateSignExtend,
               "LUI must lower to explicit sign-extending upper-immediate op");
        expect(ir.destination.has_value() && ir.destination->kind == R5900IrDestinationKind::Gpr &&
                   ir.destination->index == 2u,
               "LUI destination mismatch");
        expect(ir.inputs.size() == 1u && ir.inputs[0].kind == R5900IrOperandKind::Immediate &&
                   ir.inputs[0].immediate == 0x8040,
               "LUI must retain raw 16-bit immediate in IR");
    }

    {
        const auto result = lower_r5900_instruction(decode_r5900(r_type(7, 0, 0, 0, 0x11)), 0x00101008u);
        expect(result.ok(), "MTHI must lower");
        const auto& ir = result.instructions.at(0);
        expect(ir.opcode == R5900IrOpcode::MoveGprLow64 && ir.destination.has_value() &&
                   ir.destination->kind == R5900IrDestinationKind::Hi && ir.destination->index == 0u &&
                   ir.inputs.size() == 1u && ir.inputs[0].kind == R5900IrOperandKind::Gpr &&
                   ir.inputs[0].gpr_index == 7u,
               "MTHI lowering mismatch");
    }

    {
        const auto result = lower_r5900_instruction(decode_r5900(r_type(8, 0, 0, 0, 0x13)), 0x0010100cu);
        expect(result.ok(), "MTLO must lower");
        expect(result.instructions.at(0).destination->kind == R5900IrDestinationKind::Lo,
               "MTLO destination mismatch");
    }

    {
        const auto result = lower_r5900_instruction(decode_r5900(mmi_type(9, 0, 0, 0, 0x11)), 0x00101010u);
        expect(result.ok(), "MTHI1 must lower");
        expect(result.instructions.at(0).destination->kind == R5900IrDestinationKind::Hi1,
               "MTHI1 destination mismatch");
    }

    {
        const auto result = lower_r5900_instruction(decode_r5900(mmi_type(10, 0, 0, 0, 0x13)), 0x00101014u);
        expect(result.ok(), "MTLO1 must lower");
        expect(result.instructions.at(0).destination->kind == R5900IrDestinationKind::Lo1,
               "MTLO1 destination mismatch");
    }

    {
        const auto result = lower_r5900_instruction(decode_r5900(i_type(0x01, 11, 0x19, 5)), 0x00101018u);
        expect(result.ok(), "MTSAH must lower");
        const auto& ir = result.instructions.at(0);
        expect(ir.opcode == R5900IrOpcode::ComputeMtsah && ir.destination.has_value() &&
                   ir.destination->kind == R5900IrDestinationKind::Sa,
               "MTSAH destination/opcode mismatch");
        expect(ir.inputs.size() == 2u && ir.inputs[0].kind == R5900IrOperandKind::Gpr &&
                   ir.inputs[0].gpr_index == 11u && ir.inputs[1].kind == R5900IrOperandKind::Immediate &&
                   ir.inputs[1].immediate == 5,
               "MTSAH inputs mismatch");
    }

    {
        const auto result = lower_r5900_instruction(
            decode_r5900(mmi_type(12, 13, 14, 0x10, 0x28)), 0x0010101cu);
        expect(result.ok(), "PADDUW must lower");
        const auto& ir = result.instructions.at(0);
        expect(ir.opcode == R5900IrOpcode::AddPackedU32Saturate128 && ir.destination.has_value() &&
                   ir.destination->kind == R5900IrDestinationKind::Gpr && ir.destination->index == 14u,
               "PADDUW destination/opcode mismatch");
        expect(ir.write_mode == R5900IrGprWriteMode::Full128,
               "PADDUW must explicitly replace full 128-bit GPR");
        expect(ir.inputs.size() == 2u && ir.inputs[0].gpr_index == 12u && ir.inputs[1].gpr_index == 13u,
               "PADDUW source mismatch");
    }

    {
        const auto result = lower_r5900_instruction(
            decode_r5900(r_type(0, 0, 0, 16, 0x0f)), 0x00101020u);
        expect(result.ok() && result.instructions.size() == 1u &&
                   result.instructions[0].opcode == R5900IrOpcode::Nop,
               "SYNC must lower to semantic Nop");
    }

    {
        const std::uint32_t words[] = {
            i_type(0x0c, 1, 0, 0x00ff),
            i_type(0x0f, 0, 0, 0x8040),
            mmi_type(1, 2, 0, 0x10, 0x28),
        };
        for (const auto word : words) {
            const auto result = lower_r5900_instruction(decode_r5900(word), 0x00101024u);
            expect(result.ok() && result.instructions.size() == 1u &&
                       result.instructions[0].opcode == R5900IrOpcode::Nop,
                   "side-effect-free startup write to GPR0 must lower to provenance Nop");
        }
    }

    // RED: narrow COP1 startup lowering.
    {
        const auto word = cop1_type(0x04, 3, 5, 0, 0); // mtc1 r3,f5
        const auto result = lower_r5900_instruction(decode_r5900(word), 0x00101100u);
        expect(result.ok(), "MTC1 must lower for startup execution");
        const auto& ir = result.instructions.at(0);
        expect(ir.opcode == R5900IrOpcode::MoveBits32,
               "MTC1 must lower to raw 32-bit move");
        expect(ir.destination.has_value() && ir.destination->kind == R5900IrDestinationKind::Fpr &&
                   ir.destination->index == 5u,
               "MTC1 FPR destination mismatch");
        expect(ir.write_mode == R5900IrGprWriteMode::None,
               "MTC1 must not claim a GPR write mode");
        expect(ir.inputs.size() == 1u && ir.inputs[0].kind == R5900IrOperandKind::Gpr &&
                   ir.inputs[0].gpr_index == 3u,
               "MTC1 GPR source mismatch");
        expect(ir.guest_pc == 0x00101100u && ir.guest_raw == word,
               "MTC1 provenance mismatch");
    }

    {
        const auto word = cop1_type(0x06, 4, 31, 0, 0); // ctc1 r4,fcr31
        const auto result = lower_r5900_instruction(decode_r5900(word), 0x00101104u);
        expect(result.ok(), "CTC1 FCR31 must lower for startup execution");
        const auto& ir = result.instructions.at(0);
        expect(ir.opcode == R5900IrOpcode::MoveBits32,
               "CTC1 must lower to raw 32-bit move");
        expect(ir.destination.has_value() && ir.destination->kind == R5900IrDestinationKind::Fcr31 &&
                   ir.destination->index == 0u,
               "CTC1 destination must be unindexed FCR31");
        expect(ir.inputs.size() == 1u && ir.inputs[0].kind == R5900IrOperandKind::Gpr &&
                   ir.inputs[0].gpr_index == 4u,
               "CTC1 GPR source mismatch");
    }

    {
        const auto word = cop1_type(0x10, 2, 1, 0, 0x18); // adda.s f1,f2
        const auto result = lower_r5900_instruction(decode_r5900(word), 0x00101108u);
        expect(result.ok(), "ADDA.S must lower for startup execution");
        const auto& ir = result.instructions.at(0);
        expect(ir.opcode == R5900IrOpcode::AddF32ToAccumulator,
               "ADDA.S must lower to FP accumulator add");
        expect(ir.destination.has_value() &&
                   ir.destination->kind == R5900IrDestinationKind::FpAccumulator &&
                   ir.destination->index == 0u,
               "ADDA.S destination must be FP accumulator");
        expect(ir.write_mode == R5900IrGprWriteMode::None,
               "ADDA.S must not claim a GPR write mode");
        expect(ir.inputs.size() == 2u &&
                   ir.inputs[0].kind == R5900IrOperandKind::Fpr && ir.inputs[0].gpr_index == 1u &&
                   ir.inputs[1].kind == R5900IrOperandKind::Fpr && ir.inputs[1].gpr_index == 2u,
               "ADDA.S must use fs then ft FPR operands");
    }

    {
        const auto result = lower_r5900_instruction(
            decode_r5900(cop1_type(0x06, 4, 30, 0, 0)), 0x0010110cu);
        expect(!result.ok(), "CTC1 to control register other than 31 must remain unsupported");
        expect(result.error == R5900IrLoweringError::UnsupportedInstruction,
               "unsupported CTC1 must use explicit lowering error");
        expect(result.instructions.empty(),
               "unsupported CTC1 must not emit partial IR");
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
