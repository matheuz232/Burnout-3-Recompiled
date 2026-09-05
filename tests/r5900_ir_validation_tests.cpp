#include "recompiler/r5900_ir_executor.h"
#include "recompiler/r5900_ir_validation.h"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string>

namespace {

using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_validation_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
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
                           std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = opcode;
    ir.destination = destination;
    ir.write_mode = mode;
    ir.inputs.assign(inputs.begin(), inputs.end());
    return ir;
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    R5900IrInstruction model_probe{};
    model_probe.destination = R5900IrDestination{R5900IrDestinationKind::Fpr, 3u};
    model_probe.write_mode = R5900IrGprWriteMode::Full128;
    R5900IrOperand fpr_probe{};
    fpr_probe.kind = R5900IrOperandKind::Fpr;
    fpr_probe.gpr_index = 4u;

    R5900IrExecutionState state_probe{};
    state_probe.hi = 1u;
    state_probe.lo = 2u;
    state_probe.hi1 = 3u;
    state_probe.lo1 = 4u;
    state_probe.sa = 5u;
    state_probe.fpr[0] = 6u;
    state_probe.fcr31 = 7u;
    state_probe.fp_acc = 8u;
    expect(state_probe.gpr[0].low64 == 0u,
           "appended architectural state must not disturb GPR zero initialization");
    (void)model_probe;
    (void)fpr_probe;

    R5900IrInstruction nop{};
    nop.guest_pc = 0x00102000u;
    nop.opcode = R5900IrOpcode::Nop;
    expect(validate_r5900_ir_instruction(nop, 0).ok(), "valid Nop must validate");

    const auto valid_or64 = write_ir(
        R5900IrOpcode::Or64, 5, gpr(4), immediate(0xff00), 0x00102004u);
    expect(validate_r5900_ir_instruction(valid_or64, 1).ok(), "valid Or64 must validate");

    const auto valid_add = write_ir(
        R5900IrOpcode::AddWordSignExtend, 8, gpr(9), gpr(10), 0x00102008u);
    expect(validate_r5900_ir_instruction(valid_add, 2).ok(), "valid AddWordSignExtend must validate");

    const auto valid_andi = make_ir(
        R5900IrOpcode::And64,
        {R5900IrDestinationKind::Gpr, 2u},
        R5900IrGprWriteMode::Low64PreserveUpper64,
        {gpr(1), immediate(0xff)},
        0x00102100u);
    expect(validate_r5900_ir_instruction(valid_andi, 20).ok(),
           "valid And64 startup IR must validate");

    const auto valid_lui = make_ir(
        R5900IrOpcode::LoadUpperImmediateSignExtend,
        {R5900IrDestinationKind::Gpr, 3u},
        R5900IrGprWriteMode::Low64PreserveUpper64,
        {immediate(0x8040)},
        0x00102104u);
    expect(validate_r5900_ir_instruction(valid_lui, 21).ok(),
           "valid LUI semantic IR must validate");

    const auto valid_packed = make_ir(
        R5900IrOpcode::AddPackedU32Saturate128,
        {R5900IrDestinationKind::Gpr, 4u},
        R5900IrGprWriteMode::Full128,
        {gpr(5), gpr(6)},
        0x00102108u);
    expect(validate_r5900_ir_instruction(valid_packed, 22).ok(),
           "valid PADDUW semantic IR must validate");

    const auto valid_hi = make_ir(
        R5900IrOpcode::MoveGprLow64,
        {R5900IrDestinationKind::Hi, 0u},
        R5900IrGprWriteMode::None,
        {gpr(7)},
        0x0010210cu);
    expect(validate_r5900_ir_instruction(valid_hi, 23).ok(),
           "valid MTHI semantic IR must validate");

    const auto valid_lo = make_ir(
        R5900IrOpcode::MoveGprLow64,
        {R5900IrDestinationKind::Lo, 0u},
        R5900IrGprWriteMode::None,
        {gpr(8)},
        0x00102110u);
    expect(validate_r5900_ir_instruction(valid_lo, 24).ok(),
           "valid MTLO semantic IR must validate");

    const auto valid_hi1 = make_ir(
        R5900IrOpcode::MoveGprLow64,
        {R5900IrDestinationKind::Hi1, 0u},
        R5900IrGprWriteMode::None,
        {gpr(9)},
        0x00102114u);
    expect(validate_r5900_ir_instruction(valid_hi1, 25).ok(),
           "valid MTHI1 semantic IR must validate");

    const auto valid_lo1 = make_ir(
        R5900IrOpcode::MoveGprLow64,
        {R5900IrDestinationKind::Lo1, 0u},
        R5900IrGprWriteMode::None,
        {gpr(10)},
        0x00102118u);
    expect(validate_r5900_ir_instruction(valid_lo1, 26).ok(),
           "valid MTLO1 semantic IR must validate");

    const auto valid_mtsah = make_ir(
        R5900IrOpcode::ComputeMtsah,
        {R5900IrDestinationKind::Sa, 0u},
        R5900IrGprWriteMode::None,
        {gpr(11), immediate(5)},
        0x0010211cu);
    expect(validate_r5900_ir_instruction(valid_mtsah, 27).ok(),
           "valid MTSAH semantic IR must validate");

    // RED: exact COP1 validation matrix.
    const auto valid_mtc1 = make_ir(
        R5900IrOpcode::MoveBits32,
        {R5900IrDestinationKind::Fpr, 5u},
        R5900IrGprWriteMode::None,
        {gpr(3)},
        0x00102200u);
    expect(validate_r5900_ir_instruction(valid_mtc1, 40).ok(),
           "valid MTC1 semantic IR must validate");

    const auto valid_ctc1 = make_ir(
        R5900IrOpcode::MoveBits32,
        {R5900IrDestinationKind::Fcr31, 0u},
        R5900IrGprWriteMode::None,
        {gpr(4)},
        0x00102204u);
    expect(validate_r5900_ir_instruction(valid_ctc1, 41).ok(),
           "valid CTC1/FCR31 semantic IR must validate");

    const auto valid_addas = make_ir(
        R5900IrOpcode::AddF32ToAccumulator,
        {R5900IrDestinationKind::FpAccumulator, 0u},
        R5900IrGprWriteMode::None,
        {fpr(1), fpr(2)},
        0x00102208u);
    expect(validate_r5900_ir_instruction(valid_addas, 42).ok(),
           "valid ADDA.S semantic IR must validate");

    {
        auto ir = valid_mtc1;
        ir.destination = R5900IrDestination{R5900IrDestinationKind::Fpr, 32u};
        expect(validate_r5900_ir_instruction(ir, 43).error == R5900IrValidationError::InvalidRegister,
               "MTC1 FPR32 destination must be rejected");
    }

    {
        auto ir = valid_mtc1;
        ir.destination = R5900IrDestination{R5900IrDestinationKind::Gpr, 5u};
        expect(validate_r5900_ir_instruction(ir, 44).error == R5900IrValidationError::MalformedInstruction,
               "MoveBits32 must reject GPR destination");
    }

    {
        auto ir = valid_mtc1;
        ir.inputs[0] = immediate(1);
        expect(validate_r5900_ir_instruction(ir, 45).error == R5900IrValidationError::MalformedInstruction,
               "MoveBits32 must require a GPR source");
    }

    {
        auto ir = valid_mtc1;
        ir.inputs.push_back(gpr(4));
        expect(validate_r5900_ir_instruction(ir, 46).error == R5900IrValidationError::MalformedInstruction,
               "MoveBits32 must have exactly one source");
    }

    {
        auto ir = valid_mtc1;
        ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
        expect(validate_r5900_ir_instruction(ir, 47).error == R5900IrValidationError::MalformedInstruction,
               "MoveBits32 must require write mode None");
    }

    {
        auto ir = valid_ctc1;
        ir.destination = R5900IrDestination{R5900IrDestinationKind::Fcr31, 1u};
        expect(validate_r5900_ir_instruction(ir, 48).error == R5900IrValidationError::MalformedInstruction,
               "FCR31 destination must be unindexed");
    }

    {
        auto ir = valid_addas;
        ir.destination = R5900IrDestination{R5900IrDestinationKind::FpAccumulator, 1u};
        expect(validate_r5900_ir_instruction(ir, 49).error == R5900IrValidationError::MalformedInstruction,
               "FP accumulator destination must be unindexed");
    }

    {
        auto ir = valid_addas;
        ir.inputs[0] = fpr(32);
        expect(validate_r5900_ir_instruction(ir, 50).error == R5900IrValidationError::InvalidRegister,
               "ADDA.S FPR32 source must be rejected");
    }

    {
        auto ir = valid_addas;
        ir.inputs[1] = immediate(0);
        expect(validate_r5900_ir_instruction(ir, 51).error == R5900IrValidationError::MalformedInstruction,
               "ADDA.S must require two FPR sources");
    }

    {
        auto ir = valid_addas;
        ir.inputs.pop_back();
        expect(validate_r5900_ir_instruction(ir, 52).error == R5900IrValidationError::MalformedInstruction,
               "ADDA.S must have exactly two sources");
    }

    {
        auto ir = valid_addas;
        ir.write_mode = R5900IrGprWriteMode::Full128;
        expect(validate_r5900_ir_instruction(ir, 53).error == R5900IrValidationError::MalformedInstruction,
               "ADDA.S must require write mode None");
    }

    {
        auto ir = valid_andi;
        ir.destination = R5900IrDestination{R5900IrDestinationKind::Fpr, 2u};
        expect(validate_r5900_ir_instruction(ir, 28).error == R5900IrValidationError::MalformedInstruction,
               "And64 with non-GPR destination must be rejected");
    }

    {
        auto ir = valid_andi;
        ir.write_mode = R5900IrGprWriteMode::Full128;
        expect(validate_r5900_ir_instruction(ir, 29).error == R5900IrValidationError::MalformedInstruction,
               "And64 with Full128 write must be rejected");
    }

    {
        auto ir = valid_andi;
        ir.inputs[0] = immediate(1);
        expect(validate_r5900_ir_instruction(ir, 30).error == R5900IrValidationError::MalformedInstruction,
               "And64 first operand must be GPR");
    }

    {
        auto ir = valid_andi;
        ir.inputs[1] = gpr(2);
        expect(validate_r5900_ir_instruction(ir, 31).error == R5900IrValidationError::MalformedInstruction,
               "And64 second operand must be immediate");
    }

    {
        auto ir = valid_lui;
        ir.inputs.push_back(immediate(1));
        expect(validate_r5900_ir_instruction(ir, 32).error == R5900IrValidationError::MalformedInstruction,
               "LUI semantic IR must have exactly one operand");
    }

    {
        auto ir = valid_packed;
        ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
        expect(validate_r5900_ir_instruction(ir, 33).error == R5900IrValidationError::MalformedInstruction,
               "packed add must require Full128 write mode");
    }

    {
        auto ir = valid_packed;
        ir.inputs[1] = immediate(1);
        expect(validate_r5900_ir_instruction(ir, 34).error == R5900IrValidationError::MalformedInstruction,
               "packed add must require two GPR sources");
    }

    {
        auto ir = valid_hi;
        ir.destination = R5900IrDestination{R5900IrDestinationKind::Hi, 1u};
        expect(validate_r5900_ir_instruction(ir, 35).error == R5900IrValidationError::MalformedInstruction,
               "unindexed HI destination must reject nonzero index");
    }

    {
        auto ir = valid_hi;
        ir.destination = R5900IrDestination{R5900IrDestinationKind::Sa, 0u};
        expect(validate_r5900_ir_instruction(ir, 36).error == R5900IrValidationError::MalformedInstruction,
               "MoveGprLow64 must reject SA destination");
    }

    {
        auto ir = valid_mtsah;
        ir.destination = R5900IrDestination{R5900IrDestinationKind::Sa, 1u};
        expect(validate_r5900_ir_instruction(ir, 37).error == R5900IrValidationError::MalformedInstruction,
               "unindexed SA destination must reject nonzero index");
    }

    {
        auto ir = valid_mtsah;
        ir.inputs[0] = fpr(0);
        expect(validate_r5900_ir_instruction(ir, 38).error == R5900IrValidationError::MalformedInstruction,
               "MTSAH semantic IR must reject FPR source");
    }

    {
        auto ir = valid_packed;
        ir.destination = R5900IrDestination{R5900IrDestinationKind::Gpr, 32u};
        expect(validate_r5900_ir_instruction(ir, 39).error == R5900IrValidationError::InvalidRegister,
               "packed destination GPR32 must be rejected");
    }

    {
        auto ir = valid_or64;
        ir.inputs[0] = gpr(32);
        expect(validate_r5900_ir_instruction(ir, 3).error == R5900IrValidationError::InvalidRegister,
               "GPR 32 source must be rejected");
    }

    {
        auto ir = valid_or64;
        ir.destination = R5900IrRegister{32};
        expect(validate_r5900_ir_instruction(ir, 4).error == R5900IrValidationError::InvalidRegister,
               "GPR 32 destination must be rejected");
    }

    {
        auto ir = valid_or64;
        ir.destination.reset();
        expect(validate_r5900_ir_instruction(ir, 5).error == R5900IrValidationError::MalformedInstruction,
               "write instruction without destination must be rejected");
    }

    {
        auto ir = valid_or64;
        ir.write_mode = R5900IrGprWriteMode::None;
        expect(validate_r5900_ir_instruction(ir, 6).error == R5900IrValidationError::MalformedInstruction,
               "write instruction with wrong write mode must be rejected");
    }

    {
        auto ir = valid_or64;
        ir.inputs.pop_back();
        expect(validate_r5900_ir_instruction(ir, 7).error == R5900IrValidationError::MalformedInstruction,
               "write instruction with wrong operand count must be rejected");
    }

    {
        auto ir = valid_or64;
        ir.inputs[0].kind = static_cast<R5900IrOperandKind>(0xff);
        expect(validate_r5900_ir_instruction(ir, 8).error == R5900IrValidationError::MalformedInstruction,
               "unknown operand kind must be rejected");
    }

    {
        auto ir = nop;
        ir.destination = R5900IrRegister{1};
        expect(validate_r5900_ir_instruction(ir, 9).error == R5900IrValidationError::MalformedInstruction,
               "Nop with destination must be rejected");
    }

    {
        auto ir = nop;
        ir.inputs = {immediate(1)};
        expect(validate_r5900_ir_instruction(ir, 10).error == R5900IrValidationError::MalformedInstruction,
               "Nop with inputs must be rejected");
    }

    {
        auto ir = nop;
        ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
        expect(validate_r5900_ir_instruction(ir, 11).error == R5900IrValidationError::MalformedInstruction,
               "Nop with write mode must be rejected");
    }

    {
        auto ir = valid_or64;
        ir.opcode = static_cast<R5900IrOpcode>(0xff);
        const auto result = validate_r5900_ir_instruction(ir, 12);
        expect(result.error == R5900IrValidationError::UnsupportedOpcode,
               "unknown opcode must be rejected");
        expect(result.message.find("IR instruction 12") != std::string::npos,
               "diagnostic must include instruction index");
        expect(result.message.find("102004") != std::string::npos,
               "diagnostic must include guest PC");
    }

    std::cout << "r5900_ir_validation_tests: PASS\n";
    return EXIT_SUCCESS;
}
