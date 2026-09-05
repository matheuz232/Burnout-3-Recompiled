#include "recompiler/r5900_decoder.h"

namespace b3r::recompiler {
namespace {

void set_instruction(R5900DecodedInstruction& decoded,
                     R5900Instruction instruction,
                     R5900InstructionClass instruction_class,
                     R5900MemoryWidth memory_width = R5900MemoryWidth::None) noexcept {
    decoded.instruction = instruction;
    decoded.instruction_class = instruction_class;
    decoded.memory_width = memory_width;
}

void mark_branch(R5900DecodedInstruction& decoded,
                 R5900Instruction instruction,
                 bool likely = false,
                 bool link = false) noexcept {
    set_instruction(decoded, instruction, R5900InstructionClass::Branch);
    decoded.has_delay_slot = true;
    decoded.likely = likely;
    decoded.link = link;
}

void mark_jump(R5900DecodedInstruction& decoded,
               R5900Instruction instruction,
               bool link = false) noexcept {
    set_instruction(decoded, instruction, R5900InstructionClass::Jump);
    decoded.has_delay_slot = true;
    decoded.link = link;
}

void decode_special(R5900DecodedInstruction& decoded) noexcept {
    if (decoded.raw == 0u) {
        set_instruction(decoded, R5900Instruction::Nop, R5900InstructionClass::Alu);
        return;
    }

    switch (decoded.funct) {
    case 0x00: set_instruction(decoded, R5900Instruction::Sll, R5900InstructionClass::Alu); break;
    case 0x02: set_instruction(decoded, R5900Instruction::Srl, R5900InstructionClass::Alu); break;
    case 0x03: set_instruction(decoded, R5900Instruction::Sra, R5900InstructionClass::Alu); break;
    case 0x04: set_instruction(decoded, R5900Instruction::Sllv, R5900InstructionClass::Alu); break;
    case 0x06: set_instruction(decoded, R5900Instruction::Srlv, R5900InstructionClass::Alu); break;
    case 0x07: set_instruction(decoded, R5900Instruction::Srav, R5900InstructionClass::Alu); break;
    case 0x08:
        if (decoded.rt == 0u && decoded.rd == 0u && decoded.sa == 0u)
            mark_jump(decoded, R5900Instruction::Jr);
        break;
    case 0x09:
        if (decoded.rt == 0u && decoded.sa == 0u)
            mark_jump(decoded, R5900Instruction::Jalr, true);
        break;
    case 0x0C: set_instruction(decoded, R5900Instruction::Syscall, R5900InstructionClass::System); break;
    case 0x0D: set_instruction(decoded, R5900Instruction::Break, R5900InstructionClass::System); break;
    case 0x0F: set_instruction(decoded, R5900Instruction::Sync, R5900InstructionClass::Alu); break;
    case 0x10: set_instruction(decoded, R5900Instruction::Mfhi, R5900InstructionClass::Alu); break;
    case 0x11: set_instruction(decoded, R5900Instruction::Mthi, R5900InstructionClass::Alu); break;
    case 0x12: set_instruction(decoded, R5900Instruction::Mflo, R5900InstructionClass::Alu); break;
    case 0x13: set_instruction(decoded, R5900Instruction::Mtlo, R5900InstructionClass::Alu); break;
    case 0x18: set_instruction(decoded, R5900Instruction::Mult, R5900InstructionClass::Alu); break;
    case 0x19: set_instruction(decoded, R5900Instruction::Multu, R5900InstructionClass::Alu); break;
    case 0x1A: set_instruction(decoded, R5900Instruction::Div, R5900InstructionClass::Alu); break;
    case 0x1B: set_instruction(decoded, R5900Instruction::Divu, R5900InstructionClass::Alu); break;
    case 0x20: set_instruction(decoded, R5900Instruction::Add, R5900InstructionClass::Alu); break;
    case 0x21: set_instruction(decoded, R5900Instruction::Addu, R5900InstructionClass::Alu); break;
    case 0x22: set_instruction(decoded, R5900Instruction::Sub, R5900InstructionClass::Alu); break;
    case 0x23: set_instruction(decoded, R5900Instruction::Subu, R5900InstructionClass::Alu); break;
    case 0x24: set_instruction(decoded, R5900Instruction::And, R5900InstructionClass::Alu); break;
    case 0x25: set_instruction(decoded, R5900Instruction::Or, R5900InstructionClass::Alu); break;
    case 0x26: set_instruction(decoded, R5900Instruction::Xor, R5900InstructionClass::Alu); break;
    case 0x27: set_instruction(decoded, R5900Instruction::Nor, R5900InstructionClass::Alu); break;
    case 0x2A: set_instruction(decoded, R5900Instruction::Slt, R5900InstructionClass::Alu); break;
    case 0x2B: set_instruction(decoded, R5900Instruction::Sltu, R5900InstructionClass::Alu); break;
    default: break;
    }
}

void decode_regimm(R5900DecodedInstruction& decoded) noexcept {
    switch (decoded.rt) {
    case 0x00: mark_branch(decoded, R5900Instruction::Bltz); break;
    case 0x01: mark_branch(decoded, R5900Instruction::Bgez); break;
    case 0x02: mark_branch(decoded, R5900Instruction::Bltzl, true); break;
    case 0x03: mark_branch(decoded, R5900Instruction::Bgezl, true); break;
    case 0x10: mark_branch(decoded, R5900Instruction::Bltzal, false, true); break;
    case 0x11: mark_branch(decoded, R5900Instruction::Bgezal, false, true); break;
    case 0x12: mark_branch(decoded, R5900Instruction::Bltzall, true, true); break;
    case 0x13: mark_branch(decoded, R5900Instruction::Bgezall, true, true); break;
    case 0x19: set_instruction(decoded, R5900Instruction::Mtsah, R5900InstructionClass::Alu); break;
    default: break;
    }
}

void decode_cop1(R5900DecodedInstruction& decoded) noexcept {
    switch (decoded.rs) {
    case 0x04:
        set_instruction(decoded, R5900Instruction::Mtc1, R5900InstructionClass::Alu);
        break;
    case 0x06:
        set_instruction(decoded, R5900Instruction::Ctc1, R5900InstructionClass::Alu);
        break;
    case 0x10:
        if (decoded.funct == 0x18u) {
            set_instruction(decoded, R5900Instruction::AddaS, R5900InstructionClass::Alu);
        }
        break;
    default:
        break;
    }
}

void decode_mmi(R5900DecodedInstruction& decoded) noexcept {
    switch (decoded.funct) {
    case 0x11:
        set_instruction(decoded, R5900Instruction::Mthi1, R5900InstructionClass::Alu);
        break;
    case 0x13:
        set_instruction(decoded, R5900Instruction::Mtlo1, R5900InstructionClass::Alu);
        break;
    case 0x28:
        if (decoded.sa == 0x10u) {
            set_instruction(decoded, R5900Instruction::Padduw, R5900InstructionClass::Alu);
        }
        break;
    default:
        break;
    }
}

} // namespace

std::int32_t R5900DecodedInstruction::signed_immediate() const noexcept {
    return static_cast<std::int16_t>(immediate);
}

bool R5900DecodedInstruction::is_branch() const noexcept {
    return instruction_class == R5900InstructionClass::Branch;
}

bool R5900DecodedInstruction::is_jump() const noexcept {
    return instruction_class == R5900InstructionClass::Jump;
}

std::optional<std::uint32_t>
R5900DecodedInstruction::direct_target(std::uint32_t pc) const noexcept {
    if (is_branch()) {
        const std::uint32_t base = pc + 4u;
        const std::int32_t displacement = signed_immediate() * 4;
        return base + static_cast<std::uint32_t>(displacement);
    }

    if (instruction == R5900Instruction::J || instruction == R5900Instruction::Jal) {
        const std::uint32_t base = pc + 4u;
        return (base & 0xF0000000u) | ((target & 0x03FFFFFFu) << 2u);
    }

    return std::nullopt;
}

R5900DecodedInstruction decode_r5900(std::uint32_t word) noexcept {
    R5900DecodedInstruction decoded{};
    decoded.raw = word;
    decoded.primary_opcode = static_cast<std::uint8_t>((word >> 26u) & 0x3Fu);
    decoded.rs = static_cast<std::uint8_t>((word >> 21u) & 0x1Fu);
    decoded.rt = static_cast<std::uint8_t>((word >> 16u) & 0x1Fu);
    decoded.rd = static_cast<std::uint8_t>((word >> 11u) & 0x1Fu);
    decoded.sa = static_cast<std::uint8_t>((word >> 6u) & 0x1Fu);
    decoded.funct = static_cast<std::uint8_t>(word & 0x3Fu);
    decoded.immediate = static_cast<std::uint16_t>(word & 0xFFFFu);
    decoded.target = word & 0x03FFFFFFu;

    switch (decoded.primary_opcode) {
    case 0x00: decode_special(decoded); break;
    case 0x01: decode_regimm(decoded); break;
    case 0x02: mark_jump(decoded, R5900Instruction::J); break;
    case 0x03: mark_jump(decoded, R5900Instruction::Jal, true); break;
    case 0x04: mark_branch(decoded, R5900Instruction::Beq); break;
    case 0x05: mark_branch(decoded, R5900Instruction::Bne); break;
    case 0x06: mark_branch(decoded, R5900Instruction::Blez); break;
    case 0x07: mark_branch(decoded, R5900Instruction::Bgtz); break;
    case 0x08: set_instruction(decoded, R5900Instruction::Addi, R5900InstructionClass::Alu); break;
    case 0x09: set_instruction(decoded, R5900Instruction::Addiu, R5900InstructionClass::Alu); break;
    case 0x0A: set_instruction(decoded, R5900Instruction::Slti, R5900InstructionClass::Alu); break;
    case 0x0B: set_instruction(decoded, R5900Instruction::Sltiu, R5900InstructionClass::Alu); break;
    case 0x0C: set_instruction(decoded, R5900Instruction::Andi, R5900InstructionClass::Alu); break;
    case 0x0D: set_instruction(decoded, R5900Instruction::Ori, R5900InstructionClass::Alu); break;
    case 0x0E: set_instruction(decoded, R5900Instruction::Xori, R5900InstructionClass::Alu); break;
    case 0x0F: set_instruction(decoded, R5900Instruction::Lui, R5900InstructionClass::Alu); break;
    case 0x11: decode_cop1(decoded); break;
    case 0x14: mark_branch(decoded, R5900Instruction::Beql, true); break;
    case 0x15: mark_branch(decoded, R5900Instruction::Bnel, true); break;
    case 0x16: mark_branch(decoded, R5900Instruction::Blezl, true); break;
    case 0x17: mark_branch(decoded, R5900Instruction::Bgtzl, true); break;
    case 0x18: set_instruction(decoded, R5900Instruction::Daddi, R5900InstructionClass::Alu); break;
    case 0x19: set_instruction(decoded, R5900Instruction::Daddiu, R5900InstructionClass::Alu); break;
    case 0x1A: set_instruction(decoded, R5900Instruction::Ldl, R5900InstructionClass::Load, R5900MemoryWidth::Doubleword64); break;
    case 0x1B: set_instruction(decoded, R5900Instruction::Ldr, R5900InstructionClass::Load, R5900MemoryWidth::Doubleword64); break;
    case 0x1C: decode_mmi(decoded); break;
    case 0x1E: set_instruction(decoded, R5900Instruction::Lq, R5900InstructionClass::Load, R5900MemoryWidth::Quadword128); break;
    case 0x1F: set_instruction(decoded, R5900Instruction::Sq, R5900InstructionClass::Store, R5900MemoryWidth::Quadword128); break;
    case 0x20: set_instruction(decoded, R5900Instruction::Lb, R5900InstructionClass::Load, R5900MemoryWidth::Byte); break;
    case 0x21: set_instruction(decoded, R5900Instruction::Lh, R5900InstructionClass::Load, R5900MemoryWidth::Halfword); break;
    case 0x22: set_instruction(decoded, R5900Instruction::Lwl, R5900InstructionClass::Load, R5900MemoryWidth::Word); break;
    case 0x23: set_instruction(decoded, R5900Instruction::Lw, R5900InstructionClass::Load, R5900MemoryWidth::Word); break;
    case 0x24: set_instruction(decoded, R5900Instruction::Lbu, R5900InstructionClass::Load, R5900MemoryWidth::Byte); break;
    case 0x25: set_instruction(decoded, R5900Instruction::Lhu, R5900InstructionClass::Load, R5900MemoryWidth::Halfword); break;
    case 0x26: set_instruction(decoded, R5900Instruction::Lwr, R5900InstructionClass::Load, R5900MemoryWidth::Word); break;
    case 0x27: set_instruction(decoded, R5900Instruction::Lwu, R5900InstructionClass::Load, R5900MemoryWidth::Word); break;
    case 0x28: set_instruction(decoded, R5900Instruction::Sb, R5900InstructionClass::Store, R5900MemoryWidth::Byte); break;
    case 0x29: set_instruction(decoded, R5900Instruction::Sh, R5900InstructionClass::Store, R5900MemoryWidth::Halfword); break;
    case 0x2A: set_instruction(decoded, R5900Instruction::Swl, R5900InstructionClass::Store, R5900MemoryWidth::Word); break;
    case 0x2B: set_instruction(decoded, R5900Instruction::Sw, R5900InstructionClass::Store, R5900MemoryWidth::Word); break;
    case 0x2C: set_instruction(decoded, R5900Instruction::Sdl, R5900InstructionClass::Store, R5900MemoryWidth::Doubleword64); break;
    case 0x2D: set_instruction(decoded, R5900Instruction::Sdr, R5900InstructionClass::Store, R5900MemoryWidth::Doubleword64); break;
    case 0x2E: set_instruction(decoded, R5900Instruction::Swr, R5900InstructionClass::Store, R5900MemoryWidth::Word); break;
    case 0x31: set_instruction(decoded, R5900Instruction::Lwc1, R5900InstructionClass::Load, R5900MemoryWidth::Word); break;
    case 0x36: set_instruction(decoded, R5900Instruction::Lqc2, R5900InstructionClass::Load, R5900MemoryWidth::Quadword128); break;
    case 0x37: set_instruction(decoded, R5900Instruction::Ld, R5900InstructionClass::Load, R5900MemoryWidth::Doubleword64); break;
    case 0x39: set_instruction(decoded, R5900Instruction::Swc1, R5900InstructionClass::Store, R5900MemoryWidth::Word); break;
    case 0x3E: set_instruction(decoded, R5900Instruction::Sqc2, R5900InstructionClass::Store, R5900MemoryWidth::Quadword128); break;
    case 0x3F: set_instruction(decoded, R5900Instruction::Sd, R5900InstructionClass::Store, R5900MemoryWidth::Doubleword64); break;
    default: break;
    }

    return decoded;
}

const char* r5900_instruction_name(R5900Instruction instruction) noexcept {
    switch (instruction) {
    case R5900Instruction::Unknown: return "UNKNOWN";
    case R5900Instruction::Nop: return "NOP";
    case R5900Instruction::Sll: return "SLL";
    case R5900Instruction::Srl: return "SRL";
    case R5900Instruction::Sra: return "SRA";
    case R5900Instruction::Sllv: return "SLLV";
    case R5900Instruction::Srlv: return "SRLV";
    case R5900Instruction::Srav: return "SRAV";
    case R5900Instruction::Jr: return "JR";
    case R5900Instruction::Jalr: return "JALR";
    case R5900Instruction::Syscall: return "SYSCALL";
    case R5900Instruction::Break: return "BREAK";
    case R5900Instruction::Sync: return "SYNC";
    case R5900Instruction::Mfhi: return "MFHI";
    case R5900Instruction::Mthi: return "MTHI";
    case R5900Instruction::Mflo: return "MFLO";
    case R5900Instruction::Mtlo: return "MTLO";
    case R5900Instruction::Mthi1: return "MTHI1";
    case R5900Instruction::Mtlo1: return "MTLO1";
    case R5900Instruction::Mtsah: return "MTSAH";
    case R5900Instruction::Mult: return "MULT";
    case R5900Instruction::Multu: return "MULTU";
    case R5900Instruction::Div: return "DIV";
    case R5900Instruction::Divu: return "DIVU";
    case R5900Instruction::Add: return "ADD";
    case R5900Instruction::Addu: return "ADDU";
    case R5900Instruction::Sub: return "SUB";
    case R5900Instruction::Subu: return "SUBU";
    case R5900Instruction::And: return "AND";
    case R5900Instruction::Or: return "OR";
    case R5900Instruction::Xor: return "XOR";
    case R5900Instruction::Nor: return "NOR";
    case R5900Instruction::Slt: return "SLT";
    case R5900Instruction::Sltu: return "SLTU";
    case R5900Instruction::Padduw: return "PADDUW";
    case R5900Instruction::Mtc1: return "MTC1";
    case R5900Instruction::Ctc1: return "CTC1";
    case R5900Instruction::AddaS: return "ADDA.S";
    case R5900Instruction::Bltz: return "BLTZ";
    case R5900Instruction::Bgez: return "BGEZ";
    case R5900Instruction::Bltzl: return "BLTZL";
    case R5900Instruction::Bgezl: return "BGEZL";
    case R5900Instruction::Bltzal: return "BLTZAL";
    case R5900Instruction::Bgezal: return "BGEZAL";
    case R5900Instruction::Bltzall: return "BLTZALL";
    case R5900Instruction::Bgezall: return "BGEZALL";
    case R5900Instruction::J: return "J";
    case R5900Instruction::Jal: return "JAL";
    case R5900Instruction::Beq: return "BEQ";
    case R5900Instruction::Bne: return "BNE";
    case R5900Instruction::Blez: return "BLEZ";
    case R5900Instruction::Bgtz: return "BGTZ";
    case R5900Instruction::Addi: return "ADDI";
    case R5900Instruction::Addiu: return "ADDIU";
    case R5900Instruction::Slti: return "SLTI";
    case R5900Instruction::Sltiu: return "SLTIU";
    case R5900Instruction::Andi: return "ANDI";
    case R5900Instruction::Ori: return "ORI";
    case R5900Instruction::Xori: return "XORI";
    case R5900Instruction::Lui: return "LUI";
    case R5900Instruction::Beql: return "BEQL";
    case R5900Instruction::Bnel: return "BNEL";
    case R5900Instruction::Blezl: return "BLEZL";
    case R5900Instruction::Bgtzl: return "BGTZL";
    case R5900Instruction::Daddi: return "DADDI";
    case R5900Instruction::Daddiu: return "DADDIU";
    case R5900Instruction::Ldl: return "LDL";
    case R5900Instruction::Ldr: return "LDR";
    case R5900Instruction::Lq: return "LQ";
    case R5900Instruction::Sq: return "SQ";
    case R5900Instruction::Lb: return "LB";
    case R5900Instruction::Lh: return "LH";
    case R5900Instruction::Lwl: return "LWL";
    case R5900Instruction::Lw: return "LW";
    case R5900Instruction::Lbu: return "LBU";
    case R5900Instruction::Lhu: return "LHU";
    case R5900Instruction::Lwr: return "LWR";
    case R5900Instruction::Lwu: return "LWU";
    case R5900Instruction::Sb: return "SB";
    case R5900Instruction::Sh: return "SH";
    case R5900Instruction::Swl: return "SWL";
    case R5900Instruction::Sw: return "SW";
    case R5900Instruction::Sdl: return "SDL";
    case R5900Instruction::Sdr: return "SDR";
    case R5900Instruction::Swr: return "SWR";
    case R5900Instruction::Lwc1: return "LWC1";
    case R5900Instruction::Lqc2: return "LQC2";
    case R5900Instruction::Ld: return "LD";
    case R5900Instruction::Swc1: return "SWC1";
    case R5900Instruction::Sqc2: return "SQC2";
    case R5900Instruction::Sd: return "SD";
    }
    return "UNKNOWN";
}

} // namespace b3r::recompiler
