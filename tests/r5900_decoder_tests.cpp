#include "recompiler/r5900_decoder.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_decoder_tests: FAIL: " << message << '\n';
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

constexpr std::uint32_t i_type(std::uint8_t op, std::uint8_t rs, std::uint8_t rt, std::uint16_t imm) {
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
        const auto decoded = decode_r5900(0u);
        expect(decoded.instruction == R5900Instruction::Nop, "zero word must be recognized as NOP");
        expect(decoded.instruction_class == R5900InstructionClass::Alu, "NOP must stay in the ALU class");
    }

    {
        expect(std::string_view(r5900_instruction_name(R5900Instruction::Addu)) == "ADDU",
               "instruction names must be stable for analysis output");
        expect(std::string_view(r5900_instruction_name(R5900Instruction::Lq)) == "LQ",
               "R5900-specific instruction names must be exposed");
        expect(std::string_view(r5900_instruction_name(R5900Instruction::Unknown)) == "UNKNOWN",
               "unknown instructions need an explicit analysis label");
    }

    {
        const auto decoded = decode_r5900(r_type(9, 10, 8, 0, 0x21)); // addu t0,t1,t2
        expect(decoded.instruction == R5900Instruction::Addu, "SPECIAL/ADDU must decode");
        expect(decoded.rs == 9u && decoded.rt == 10u && decoded.rd == 8u, "R-type register fields must decode");
    }

    {
        const auto decoded = decode_r5900(i_type(0x09, 29, 29, 0xFFF0)); // addiu sp,sp,-16
        expect(decoded.instruction == R5900Instruction::Addiu, "ADDIU must decode");
        expect(decoded.signed_immediate() == -16, "signed immediate must sign extend");
    }

    {
        const auto decoded = decode_r5900(i_type(0x04, 4, 0, 0xFFFC)); // beq a0,zero,-4
        expect(decoded.instruction == R5900Instruction::Beq, "BEQ must decode");
        expect(decoded.is_branch(), "BEQ must be a branch");
        expect(decoded.has_delay_slot, "EE branches must expose their delay slot");
        expect(decoded.direct_target(0x00100020u).value_or(0) == 0x00100014u,
               "branch target must use PC+4 plus signed immediate<<2");
    }

    {
        const std::uint32_t j = (0x02u << 26u) | ((0x00123450u >> 2u) & 0x03FFFFFFu);
        const auto decoded = decode_r5900(j);
        expect(decoded.instruction == R5900Instruction::J, "J must decode");
        expect(decoded.is_jump(), "J must be a jump");
        expect(decoded.direct_target(0x10000000u).value_or(0) == 0x10123450u,
               "jump target must combine PC+4 high nibble with target field");
    }

    {
        const auto decoded = decode_r5900(i_type(0x01, 16, 0x11, 2)); // bgezal s0,+2
        expect(decoded.instruction == R5900Instruction::Bgezal, "REGIMM/BGEZAL must decode");
        expect(decoded.is_branch(), "BGEZAL must be a branch");
        expect(decoded.link, "BGEZAL must expose link semantics");
        expect(decoded.direct_target(0x2000u).value_or(0) == 0x200Cu, "REGIMM branch target must decode");
    }

    {
        const auto decoded = decode_r5900(i_type(0x14, 8, 9, 1)); // beql t0,t1,+1
        expect(decoded.instruction == R5900Instruction::Beql, "BEQL must decode");
        expect(decoded.likely, "branch-likely semantics must be explicit");
        expect(decoded.has_delay_slot, "branch-likely still has an architectural delay slot");
    }

    {
        const auto jr = decode_r5900(r_type(31, 0, 0, 0, 0x08));
        expect(jr.instruction == R5900Instruction::Jr, "JR must decode");
        expect(jr.is_jump() && jr.has_delay_slot, "JR must classify as delayed indirect jump");
        expect(!jr.direct_target(0x3000u).has_value(), "JR target is register-indirect and cannot be resolved statically here");

        const auto jalr = decode_r5900(r_type(25, 0, 31, 0, 0x09));
        expect(jalr.instruction == R5900Instruction::Jalr, "JALR must decode");
        expect(jalr.link, "JALR must expose link semantics");
        expect(!jalr.direct_target(0x3000u).has_value(), "JALR target is register-indirect");
    }

    {
        const auto lq = decode_r5900(i_type(0x1E, 5, 6, 0x0040));
        expect(lq.instruction == R5900Instruction::Lq, "R5900 LQ must decode");
        expect(lq.instruction_class == R5900InstructionClass::Load, "LQ must classify as load");
        expect(lq.memory_width == R5900MemoryWidth::Quadword128, "LQ must expose 128-bit width");

        const auto sq = decode_r5900(i_type(0x1F, 5, 6, 0x0040));
        expect(sq.instruction == R5900Instruction::Sq, "R5900 SQ must decode");
        expect(sq.instruction_class == R5900InstructionClass::Store, "SQ must classify as store");
        expect(sq.memory_width == R5900MemoryWidth::Quadword128, "SQ must expose 128-bit width");
    }

    {
        const auto sync = decode_r5900(r_type(0, 0, 0, 16, 0x0f));
        expect(sync.instruction == R5900Instruction::Sync, "SPECIAL/SYNC must decode");
        expect(sync.instruction_class == R5900InstructionClass::Alu,
               "SYNC must remain a non-terminating analysis instruction");
        expect(std::string_view(r5900_instruction_name(sync.instruction)) == "SYNC",
               "SYNC name must be stable");
    }

    {
        const auto mtsah = decode_r5900(i_type(0x01, 0, 0x19, 0));
        expect(mtsah.instruction == R5900Instruction::Mtsah, "REGIMM/MTSAH must decode");
        expect(mtsah.instruction_class == R5900InstructionClass::Alu,
               "MTSAH must not terminate static control flow");
    }

    {
        const auto mthi1 = decode_r5900(mmi_type(0, 0, 0, 0, 0x11));
        const auto mtlo1 = decode_r5900(mmi_type(0, 0, 0, 0, 0x13));
        const auto padduw = decode_r5900(mmi_type(0, 0, 7, 0x10, 0x28));
        expect(mthi1.instruction == R5900Instruction::Mthi1, "MMI/MTHI1 must decode");
        expect(mtlo1.instruction == R5900Instruction::Mtlo1, "MMI/MTLO1 must decode");
        expect(padduw.instruction == R5900Instruction::Padduw, "MMI1/PADDUW must decode");
        expect(padduw.rd == 7u && padduw.sa == 0x10u,
               "MMI1 register/sub-op fields must remain available");
        expect(std::string_view(r5900_instruction_name(padduw.instruction)) == "PADDUW",
               "PADDUW name must be stable");
    }

    {
        const auto mtc1 = decode_r5900(cop1_type(0x04, 3, 5, 0, 0));
        const auto ctc1 = decode_r5900(cop1_type(0x06, 4, 31, 0, 0));
        const auto adda_s = decode_r5900(cop1_type(0x10, 1, 2, 0, 0x18));
        expect(mtc1.instruction == R5900Instruction::Mtc1, "COP1/MTC1 must decode");
        expect(ctc1.instruction == R5900Instruction::Ctc1, "COP1/CTC1 must decode");
        expect(adda_s.instruction == R5900Instruction::AddaS, "COP1.S/ADDA.S must decode");
        expect(mtc1.rt == 3u && mtc1.rd == 5u,
               "COP1 transfer register fields must remain available");
        expect(std::string_view(r5900_instruction_name(adda_s.instruction)) == "ADDA.S",
               "ADDA.S name must be stable");
    }

    {
        const auto decoded = decode_r5900(0x70000000u); // unsupported MMI sub-op stays explicit
        expect(decoded.instruction == R5900Instruction::Unknown, "unsupported MMI sub-op must remain Unknown");
        expect(decoded.raw == 0x70000000u, "unknown instruction must retain raw word");
    }

    std::cout << "r5900_decoder_tests: PASS\n";
    return EXIT_SUCCESS;
}