#include "recompiler/r5900_decoder.h"
#include "recompiler/r5900_ir_executor.h"

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_executor_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

R5900IrOperand gpr(std::uint8_t index) {
    R5900IrOperand value{};
    value.kind = R5900IrOperandKind::Gpr;
    value.gpr_index = index;
    return value;
}

R5900IrOperand immediate(std::int64_t value) {
    R5900IrOperand operand{};
    operand.kind = R5900IrOperandKind::Immediate;
    operand.immediate = value;
    return operand;
}

R5900IrInstruction write_ir(R5900IrOpcode opcode,
                            std::uint8_t destination,
                            R5900IrOperand lhs,
                            R5900IrOperand rhs,
                            std::uint32_t guest_pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = guest_pc;
    ir.opcode = opcode;
    ir.destination = R5900IrRegister{destination};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
    ir.inputs = {lhs, rhs};
    return ir;
}

R5900IrInstruction make_ir(R5900IrOpcode opcode,
                           R5900IrDestination destination,
                           R5900IrGprWriteMode mode,
                           std::initializer_list<R5900IrOperand> inputs,
                           std::uint32_t guest_pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = guest_pc;
    ir.opcode = opcode;
    ir.destination = destination;
    ir.write_mode = mode;
    ir.inputs.assign(inputs.begin(), inputs.end());
    return ir;
}

R5900IrGprValue packed_u32(std::uint32_t lane0,
                           std::uint32_t lane1,
                           std::uint32_t lane2,
                           std::uint32_t lane3) {
    return {
        static_cast<std::uint64_t>(lane0) |
            (static_cast<std::uint64_t>(lane1) << 32u),
        static_cast<std::uint64_t>(lane2) |
            (static_cast<std::uint64_t>(lane3) << 32u),
    };
}

std::uint32_t packed_lane(const R5900IrGprValue& value, std::size_t lane) {
    if (lane < 2u) {
        return static_cast<std::uint32_t>(value.low64 >> (lane * 32u));
    }
    return static_cast<std::uint32_t>(value.high64 >> ((lane - 2u) * 32u));
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
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x1122334455667788ull;
        state.gpr[1].high64 = 0x8877665544332211ull;

        R5900IrInstruction nop{};
        nop.guest_pc = 0x00100000u;
        nop.opcode = R5900IrOpcode::Nop;

        const auto result = execute_r5900_ir(std::vector<R5900IrInstruction>{nop}, state);
        expect(result.ok(), "valid Nop must execute successfully");
        expect(state.gpr[1].low64 == 0x1122334455667788ull, "Nop must preserve low64");
        expect(state.gpr[1].high64 == 0x8877665544332211ull, "Nop must preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[9].low64 = 0x000000007fffffffull;
        state.gpr[10].low64 = 1u;
        state.gpr[8].high64 = 0x0123456789abcdefull;

        const auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 8, gpr(9), gpr(10), 0x00100010u);
        const auto result = execute_r5900_ir({ir}, state);
        expect(result.ok(), "word add must execute");
        expect(state.gpr[8].low64 == 0xffffffff80000000ull, "negative word result must sign-extend to 64 bits");
        expect(state.gpr[8].high64 == 0x0123456789abcdefull, "word add must preserve destination high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 0x00000000ffffffffull;
        const auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 2, gpr(1), immediate(1), 0x00100014u);
        const auto result = execute_r5900_ir({ir}, state);
        expect(result.ok(), "wrapping word add must execute");
        expect(state.gpr[2].low64 == 0u, "word add must wrap modulo 2^32");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[29].low64 = 0x0000000000001000ull;
        state.gpr[29].high64 = 0xfeedfacecafebeefull;
        const auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 29, gpr(29), immediate(-16), 0x00100018u);
        const auto result = execute_r5900_ir({ir}, state);
        expect(result.ok(), "ADDIU-style immediate add must execute");
        expect(state.gpr[29].low64 == 0x0000000000000ff0ull, "signed immediate must contribute its low 32 bits to word addition");
        expect(state.gpr[29].high64 == 0xfeedfacecafebeefull, "ADDIU-style write must preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 0x1234567800000000ull;
        state.gpr[5].high64 = 0xaabbccddeeff0011ull;
        const auto ir = write_ir(R5900IrOpcode::Or64, 5, gpr(4), immediate(0xff00), 0x0010001cu);
        const auto result = execute_r5900_ir({ir}, state);
        expect(result.ok(), "ORI-style OR must execute");
        expect(state.gpr[5].low64 == 0x123456780000ff00ull, "Or64 must operate across the full low 64-bit register value");
        expect(state.gpr[5].high64 == 0xaabbccddeeff0011ull, "Or64 must preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[0].low64 = 0xffffffffffffffffull;
        state.gpr[0].high64 = 0xffffffffffffffffull;
        const auto ir = write_ir(R5900IrOpcode::Or64, 0, immediate(1), immediate(2), 0x00100020u);
        const auto result = execute_r5900_ir({ir}, state);
        expect(result.ok(), "write to GPR zero must be accepted as a discarded architectural write");
        expect(state.gpr[0].low64 == 0u && state.gpr[0].high64 == 0u, "GPR zero must remain immutable zero");
    }

    {
        R5900IrExecutionState state{};
        auto ir = write_ir(R5900IrOpcode::Or64, 1, gpr(32), immediate(1), 0x00100100u);
        const auto result = execute_r5900_ir({ir}, state);
        expect(result.error == R5900IrExecutionError::InvalidRegister, "source GPR >31 must fail explicitly");
    }

    {
        R5900IrExecutionState state{};
        auto ir = write_ir(R5900IrOpcode::Or64, 32, immediate(1), immediate(2), 0x00100104u);
        const auto result = execute_r5900_ir({ir}, state);
        expect(result.error == R5900IrExecutionError::InvalidRegister, "destination GPR >31 must fail explicitly");
    }

    {
        R5900IrExecutionState state{};
        R5900IrInstruction ir{};
        ir.guest_pc = 0x00100108u;
        ir.opcode = R5900IrOpcode::Or64;
        ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
        ir.inputs = {immediate(1), immediate(2)};
        const auto result = execute_r5900_ir({ir}, state);
        expect(result.error == R5900IrExecutionError::MalformedInstruction, "write opcode without destination must fail");
    }

    {
        R5900IrExecutionState state{};
        auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 1, immediate(1), immediate(2), 0x0010010cu);
        ir.inputs.pop_back();
        const auto result = execute_r5900_ir({ir}, state);
        expect(result.error == R5900IrExecutionError::MalformedInstruction, "write opcode with wrong operand count must fail");
    }

    {
        R5900IrExecutionState state{};
        auto ir = write_ir(R5900IrOpcode::Or64, 1, immediate(1), immediate(2), 0x00100110u);
        ir.write_mode = R5900IrGprWriteMode::None;
        const auto result = execute_r5900_ir({ir}, state);
        expect(result.error == R5900IrExecutionError::MalformedInstruction, "write opcode with wrong write mode must fail");
    }

    {
        R5900IrExecutionState state{};
        R5900IrInstruction nop{};
        nop.guest_pc = 0x00100114u;
        nop.opcode = R5900IrOpcode::Nop;
        nop.destination = R5900IrRegister{1};
        const auto result = execute_r5900_ir({nop}, state);
        expect(result.error == R5900IrExecutionError::MalformedInstruction, "Nop with destination must fail");
    }

    {
        R5900IrExecutionState state{};
        R5900IrInstruction nop{};
        nop.guest_pc = 0x00100118u;
        nop.opcode = R5900IrOpcode::Nop;
        nop.inputs = {immediate(1)};
        const auto result = execute_r5900_ir({nop}, state);
        expect(result.error == R5900IrExecutionError::MalformedInstruction, "Nop with inputs must fail");
    }

    {
        R5900IrExecutionState state{};
        R5900IrInstruction nop{};
        nop.guest_pc = 0x0010011cu;
        nop.opcode = R5900IrOpcode::Nop;
        nop.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
        const auto result = execute_r5900_ir({nop}, state);
        expect(result.error == R5900IrExecutionError::MalformedInstruction, "Nop with a write mode must fail");
    }

    {
        R5900IrExecutionState state{};
        auto ir = write_ir(R5900IrOpcode::Or64, 1, immediate(1), immediate(2), 0x00100120u);
        ir.inputs[0].kind = static_cast<R5900IrOperandKind>(0xff);
        const auto result = execute_r5900_ir({ir}, state);
        expect(result.error == R5900IrExecutionError::MalformedInstruction, "unknown operand kind must fail explicitly");
    }

    {
        R5900IrExecutionState state{};
        R5900IrInstruction ir{};
        ir.guest_pc = 0x00100124u;
        ir.opcode = static_cast<R5900IrOpcode>(0xff);
        const auto result = execute_r5900_ir({ir}, state);
        expect(result.error == R5900IrExecutionError::UnsupportedOpcode, "unknown opcode must fail explicitly");
        expect(result.message.find("IR instruction 0") != std::string::npos, "error must include instruction index");
        expect(result.message.find("100124") != std::string::npos, "error must include guest PC");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[1].low64 = 1u;
        state.gpr[3].low64 = 0xaaaaaaaaaaaaaaaaull;

        const auto first = write_ir(R5900IrOpcode::Or64, 2, gpr(1), immediate(2), 0x00100130u);
        const auto bad = write_ir(R5900IrOpcode::Or64, 3, gpr(32), immediate(4), 0x00100134u);
        const auto later = write_ir(R5900IrOpcode::Or64, 3, immediate(8), immediate(16), 0x00100138u);

        const auto result = execute_r5900_ir({first, bad, later}, state);
        expect(result.error == R5900IrExecutionError::InvalidRegister, "malformed middle instruction must stop execution");
        expect(state.gpr[2].low64 == 3u, "earlier valid instruction must remain committed");
        expect(state.gpr[3].low64 == 0xaaaaaaaaaaaaaaaaull, "failing and later instructions must not mutate state");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[9].low64 = 5u;
        state.gpr[10].low64 = 7u;
        state.gpr[8].high64 = 0x1111222233334444ull;
        state.gpr[29].low64 = 0x1000u;
        state.gpr[29].high64 = 0xaaaabbbbccccddddull;
        state.gpr[4].low64 = 0x1234567800000000ull;
        state.gpr[5].high64 = 0x5555666677778888ull;

        const std::uint32_t words[] = {
            r_type(9, 10, 8, 0, 0x21),
            i_type(0x09, 29, 29, 0xfff0),
            i_type(0x0d, 4, 5, 0xff00),
        };

        std::vector<R5900IrInstruction> program;
        std::uint32_t pc = 0x00101000u;
        for (const auto word : words) {
            const auto lowered = lower_r5900_instruction(decode_r5900(word), pc);
            expect(lowered.ok(), "synthetic guest instruction must lower for executor integration test");
            expect(lowered.instructions.size() == 1u, "each current synthetic instruction must lower to one IR op");
            program.push_back(lowered.instructions.front());
            pc += 4u;
        }

        const auto executed = execute_r5900_ir(program, state);
        expect(executed.ok(), "decoded/lowered synthetic sequence must execute");
        expect(state.gpr[8].low64 == 12u, "ADDU result must flow through decoder, IR and executor");
        expect(state.gpr[8].high64 == 0x1111222233334444ull, "ADDU pipeline must preserve high64");
        expect(state.gpr[29].low64 == 0x0ff0u, "ADDIU result must flow through decoder, IR and executor");
        expect(state.gpr[29].high64 == 0xaaaabbbbccccddddull, "ADDIU pipeline must preserve high64");
        expect(state.gpr[5].low64 == 0x123456780000ff00ull, "ORI result must flow through decoder, IR and executor");
        expect(state.gpr[5].high64 == 0x5555666677778888ull, "ORI pipeline must preserve high64");
    }

    // RED: startup execution v0 reference semantics.
    {
        R5900IrExecutionState state{};
        state.gpr[1] = {0x12345678abcdef12ull, 0xaabbccddeeff0011ull};
        state.gpr[2].high64 = 0x1122334455667788ull;
        const auto ir = make_ir(
            R5900IrOpcode::And64,
            {R5900IrDestinationKind::Gpr, 2u},
            R5900IrGprWriteMode::Low64PreserveUpper64,
            {gpr(1), immediate(0xff00)},
            0x00102000u);
        expect(execute_r5900_ir({ir}, state).ok(), "And64 reference IR must execute");
        expect(state.gpr[2].low64 == 0xef00u,
               "ANDI semantics must AND low64 with zero-extended immediate");
        expect(state.gpr[2].high64 == 0x1122334455667788ull,
               "ANDI semantics must preserve destination high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[2].high64 = 0x123456789abcdef0ull;
        const auto positive = make_ir(
            R5900IrOpcode::LoadUpperImmediateSignExtend,
            {R5900IrDestinationKind::Gpr, 2u},
            R5900IrGprWriteMode::Low64PreserveUpper64,
            {immediate(0x004e)},
            0x00102004u);
        expect(execute_r5900_ir({positive}, state).ok(), "positive LUI reference IR must execute");
        expect(state.gpr[2].low64 == 0x00000000004e0000ull,
               "positive LUI must place immediate in bits 31:16");
        expect(state.gpr[2].high64 == 0x123456789abcdef0ull,
               "LUI must preserve upper 64 bits");

        const auto negative = make_ir(
            R5900IrOpcode::LoadUpperImmediateSignExtend,
            {R5900IrDestinationKind::Gpr, 2u},
            R5900IrGprWriteMode::Low64PreserveUpper64,
            {immediate(0x8040)},
            0x00102008u);
        expect(execute_r5900_ir({negative}, state).ok(), "negative LUI reference IR must execute");
        expect(state.gpr[2].low64 == 0xffffffff80400000ull,
               "negative LUI word must sign-extend to low64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[7].low64 = 0x1111222233334444ull;
        state.gpr[8].low64 = 0x5555666677778888ull;
        state.gpr[9].low64 = 0x9999aaaabbbbccccull;
        state.gpr[10].low64 = 0xddddeeeeffff0001ull;
        state.gpr[11].low64 = 5u;

        const std::vector<R5900IrInstruction> program = {
            make_ir(R5900IrOpcode::MoveGprLow64,
                    {R5900IrDestinationKind::Hi, 0u}, R5900IrGprWriteMode::None,
                    {gpr(7)}, 0x00102010u),
            make_ir(R5900IrOpcode::MoveGprLow64,
                    {R5900IrDestinationKind::Lo, 0u}, R5900IrGprWriteMode::None,
                    {gpr(8)}, 0x00102014u),
            make_ir(R5900IrOpcode::MoveGprLow64,
                    {R5900IrDestinationKind::Hi1, 0u}, R5900IrGprWriteMode::None,
                    {gpr(9)}, 0x00102018u),
            make_ir(R5900IrOpcode::MoveGprLow64,
                    {R5900IrDestinationKind::Lo1, 0u}, R5900IrGprWriteMode::None,
                    {gpr(10)}, 0x0010201cu),
            make_ir(R5900IrOpcode::ComputeMtsah,
                    {R5900IrDestinationKind::Sa, 0u}, R5900IrGprWriteMode::None,
                    {gpr(11), immediate(3)}, 0x00102020u),
        };

        expect(execute_r5900_ir(program, state).ok(), "special-register reference program must execute");
        expect(state.hi == 0x1111222233334444ull, "MTHI reference semantics mismatch");
        expect(state.lo == 0x5555666677778888ull, "MTLO reference semantics mismatch");
        expect(state.hi1 == 0x9999aaaabbbbccccull, "MTHI1 reference semantics mismatch");
        expect(state.lo1 == 0xddddeeeeffff0001ull, "MTLO1 reference semantics mismatch");
        expect(state.sa == 12u, "MTSAH must compute ((5&7)^(3&7))<<1 == 12");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[12] = packed_u32(0xffffffffu, 1u, 0xfffffffeu, 0x80000000u);
        state.gpr[13] = packed_u32(1u, 2u, 1u, 0x80000000u);
        state.gpr[14] = packed_u32(0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u);

        const auto ir = make_ir(
            R5900IrOpcode::AddPackedU32Saturate128,
            {R5900IrDestinationKind::Gpr, 14u},
            R5900IrGprWriteMode::Full128,
            {gpr(12), gpr(13)},
            0x00102030u);
        expect(execute_r5900_ir({ir}, state).ok(), "PADDUW reference IR must execute");
        expect(packed_lane(state.gpr[14], 0) == 0xffffffffu,
               "PADDUW lane0 must saturate on overflow");
        expect(packed_lane(state.gpr[14], 1) == 3u,
               "PADDUW lane1 must add without saturation");
        expect(packed_lane(state.gpr[14], 2) == 0xffffffffu,
               "PADDUW lane2 exact maximum must remain maximum");
        expect(packed_lane(state.gpr[14], 3) == 0xffffffffu,
               "PADDUW lane3 must saturate on overflow");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[1] = packed_u32(1u, 2u, 3u, 4u);
        state.gpr[2] = packed_u32(10u, 20u, 30u, 40u);
        const auto rd_eq_rs = make_ir(
            R5900IrOpcode::AddPackedU32Saturate128,
            {R5900IrDestinationKind::Gpr, 1u}, R5900IrGprWriteMode::Full128,
            {gpr(1), gpr(2)}, 0x00102034u);
        expect(execute_r5900_ir({rd_eq_rs}, state).ok(), "PADDUW rd==rs alias must execute");
        expect(packed_lane(state.gpr[1], 0) == 11u &&
                   packed_lane(state.gpr[1], 1) == 22u &&
                   packed_lane(state.gpr[1], 2) == 33u &&
                   packed_lane(state.gpr[1], 3) == 44u,
               "PADDUW rd==rs must snapshot sources before write");

        state.gpr[1] = packed_u32(1u, 2u, 3u, 4u);
        state.gpr[2] = packed_u32(10u, 20u, 30u, 40u);
        const auto rd_eq_rt = make_ir(
            R5900IrOpcode::AddPackedU32Saturate128,
            {R5900IrDestinationKind::Gpr, 2u}, R5900IrGprWriteMode::Full128,
            {gpr(1), gpr(2)}, 0x00102038u);
        expect(execute_r5900_ir({rd_eq_rt}, state).ok(), "PADDUW rd==rt alias must execute");
        expect(packed_lane(state.gpr[2], 0) == 11u &&
                   packed_lane(state.gpr[2], 1) == 22u &&
                   packed_lane(state.gpr[2], 2) == 33u &&
                   packed_lane(state.gpr[2], 3) == 44u,
               "PADDUW rd==rt must snapshot sources before write");
    }

    std::cout << "r5900_ir_executor_tests: PASS\n";
    return EXIT_SUCCESS;
}
