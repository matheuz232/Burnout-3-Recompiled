#pragma once

#include <cstdint>
#include <optional>

namespace b3r::recompiler {

enum class R5900Instruction {
    Unknown = 0,
    Nop,

    Sll,
    Srl,
    Sra,
    Sllv,
    Srlv,
    Srav,
    Jr,
    Jalr,
    Syscall,
    Break,
    Sync,
    Mfhi,
    Mthi,
    Mflo,
    Mtlo,
    Mthi1,
    Mtlo1,
    Mtsah,
    Mult,
    Multu,
    Div,
    Divu,
    Add,
    Addu,
    Sub,
    Subu,
    And,
    Or,
    Xor,
    Nor,
    Slt,
    Sltu,
    Padduw,
    Mtc1,
    Ctc1,
    AddaS,

    Bltz,
    Bgez,
    Bltzl,
    Bgezl,
    Bltzal,
    Bgezal,
    Bltzall,
    Bgezall,

    J,
    Jal,
    Beq,
    Bne,
    Blez,
    Bgtz,
    Addi,
    Addiu,
    Slti,
    Sltiu,
    Andi,
    Ori,
    Xori,
    Lui,
    Beql,
    Bnel,
    Blezl,
    Bgtzl,
    Daddi,
    Daddiu,

    Ldl,
    Ldr,
    Lq,
    Sq,
    Lb,
    Lh,
    Lwl,
    Lw,
    Lbu,
    Lhu,
    Lwr,
    Lwu,
    Sb,
    Sh,
    Swl,
    Sw,
    Sdl,
    Sdr,
    Swr,
    Lwc1,
    Lqc2,
    Ld,
    Swc1,
    Sqc2,
    Sd,
};

enum class R5900InstructionClass {
    Unknown = 0,
    Alu,
    Branch,
    Jump,
    Load,
    Store,
    System,
};

enum class R5900MemoryWidth {
    None = 0,
    Byte,
    Halfword,
    Word,
    Doubleword64,
    Quadword128,
};

struct R5900DecodedInstruction {
    std::uint32_t raw{};
    R5900Instruction instruction{R5900Instruction::Unknown};
    R5900InstructionClass instruction_class{R5900InstructionClass::Unknown};
    R5900MemoryWidth memory_width{R5900MemoryWidth::None};

    std::uint8_t primary_opcode{};
    std::uint8_t rs{};
    std::uint8_t rt{};
    std::uint8_t rd{};
    std::uint8_t sa{};
    std::uint8_t funct{};
    std::uint16_t immediate{};
    std::uint32_t target{};

    bool has_delay_slot{};
    bool link{};
    bool likely{};

    [[nodiscard]] std::int32_t signed_immediate() const noexcept;
    [[nodiscard]] bool is_branch() const noexcept;
    [[nodiscard]] bool is_jump() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> direct_target(std::uint32_t pc) const noexcept;
};

[[nodiscard]] R5900DecodedInstruction decode_r5900(std::uint32_t word) noexcept;
[[nodiscard]] const char* r5900_instruction_name(R5900Instruction instruction) noexcept;

} // namespace b3r::recompiler