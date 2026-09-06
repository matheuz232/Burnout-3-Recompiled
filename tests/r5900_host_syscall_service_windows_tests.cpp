#include "recompiler/windows/r5900_host_syscall_service.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_host_syscall_service_windows_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void expect_states_equal(
    const b3r::recompiler::R5900IrExecutionState& before,
    const b3r::recompiler::R5900IrExecutionState& after,
    const char* message) {
    for (std::size_t index = 0; index < 32u; ++index) {
        if (after.gpr[index].low64 != before.gpr[index].low64 ||
            after.gpr[index].high64 != before.gpr[index].high64) {
            fail(message);
        }
    }
    if (after.hi != before.hi || after.lo != before.lo ||
        after.hi1 != before.hi1 || after.lo1 != before.lo1 ||
        after.sa != before.sa || after.fcr31 != before.fcr31 ||
        after.fp_acc != before.fp_acc || after.fpr != before.fpr) {
        fail(message);
    }
}

void expect_only_v0_low_changed(
    const b3r::recompiler::R5900IrExecutionState& before,
    const b3r::recompiler::R5900IrExecutionState& after,
    std::uint64_t expected_v0_low) {
    for (std::size_t index = 0; index < 32u; ++index) {
        const auto expected_low = index == 2u ? expected_v0_low : before.gpr[index].low64;
        expect(after.gpr[index].low64 == expected_low,
               "SetupThread changed an unexpected GPR low64 value");
        expect(after.gpr[index].high64 == before.gpr[index].high64,
               "SetupThread changed an unexpected GPR high64 value");
    }
    expect(after.hi == before.hi && after.lo == before.lo &&
               after.hi1 == before.hi1 && after.lo1 == before.lo1 &&
               after.sa == before.sa && after.fcr31 == before.fcr31 &&
               after.fp_acc == before.fp_acc,
           "SetupThread changed unexpected special-register state");
    for (std::size_t index = 0; index < before.fpr.size(); ++index) {
        expect(after.fpr[index] == before.fpr[index],
               "SetupThread changed unexpected FPR state");
    }
}

bool same_context(
    const b3r::recompiler::R5900SetupThreadContext& lhs,
    const b3r::recompiler::R5900SetupThreadContext& rhs) {
    return lhs.gp == rhs.gp &&
           lhs.stack_base == rhs.stack_base &&
           lhs.stack_size == rhs.stack_size &&
           lhs.stack_top == rhs.stack_top &&
           lhs.args == rhs.args &&
           lhs.root_func == rhs.root_func;
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    b3r::runtime::Ps2MemoryMap memory{};
    R5900HostSyscallService service{};

    R5900IrExecutionState state{};
    state.gpr[0] = {};
    state.gpr[1] = {0x1122334455667788ull, 0x8877665544332211ull};
    state.gpr[3].low64 = 0x1234u;

    const auto before = state;
    const auto unsupported = service.handle(
        R5900HostSyscallRequest{0x00100000u, 0x0000000cu},
        state,
        memory);

    expect(unsupported.status == R5900HostSyscallStatus::Unsupported,
           "unknown EE syscall selector must be unsupported");
    expect(unsupported.message.find("0x00100000") != std::string::npos,
           "unsupported diagnostic must include guest PC");
    expect(unsupported.message.find("4660") != std::string::npos,
           "unsupported diagnostic must include decimal selector");
    expect(state.gpr[1].low64 == before.gpr[1].low64 &&
               state.gpr[1].high64 == before.gpr[1].high64,
           "unsupported syscall must not mutate unrelated state");
    expect(state.gpr[0].low64 == 0u && state.gpr[0].high64 == 0u,
           "unsupported syscall must preserve architectural r0");

    const auto malformed = service.handle(
        R5900HostSyscallRequest{0x00100004u, 0x00000000u},
        state,
        memory);
    expect(malformed.status == R5900HostSyscallStatus::Fault,
           "non-SYSCALL raw word must fault the host-service contract");
    expect(malformed.message.find("not SYSCALL") != std::string::npos,
           "malformed diagnostic must identify instruction kind");

    R5900HostSyscallService setup_service{};
    R5900IrExecutionState setup_state{};
    for (std::size_t index = 0; index < 32u; ++index) {
        setup_state.gpr[index] = {
            0x1000000000000000ull + static_cast<std::uint64_t>(index),
            0x2000000000000000ull + static_cast<std::uint64_t>(index),
        };
    }
    setup_state.gpr[2] = {0xdeadbeefdeadbeefull, 0xa5a5a5a5a5a5a5a5ull};
    setup_state.gpr[3].low64 = 0x3cu;
    setup_state.gpr[4].low64 = 0x004e8670u;
    setup_state.gpr[5].low64 = 0x01ff0000u;
    setup_state.gpr[6].low64 = 0x00010000u;
    setup_state.gpr[7].low64 = 0x01d9ce80u;
    setup_state.gpr[8].low64 = 0x00100220u;
    setup_state.hi = 0x1111222233334444ull;
    setup_state.lo = 0x5555666677778888ull;
    setup_state.hi1 = 0x9999aaaabbbbccccull;
    setup_state.lo1 = 0xddddeeeeffff0000ull;
    setup_state.sa = 17u;
    setup_state.fpr[3] = 0x3f800000u;
    setup_state.fcr31 = 0x01020304u;
    setup_state.fp_acc = 0x05060708u;
    const auto setup_before = setup_state;
    const auto regions_before = memory.regions();

    const auto setup_result = setup_service.handle(
        R5900HostSyscallRequest{0x001001c8u, 0x0000000cu},
        setup_state,
        memory);

    expect(setup_result.status == R5900HostSyscallStatus::Handled,
           "SetupThread explicit stack must be handled");
    expect_only_v0_low_changed(setup_before, setup_state, 0x02000000u);
    expect(memory.regions().size() == regions_before.size(),
           "SetupThread must not alter memory-map regions");

    const auto& context = setup_service.setup_thread_context();
    expect(context.has_value(), "successful SetupThread must record context");
    expect(context->gp == 0x004e8670u &&
               context->stack_base == 0x01ff0000u &&
               context->stack_size == 0x00010000u &&
               context->stack_top == 0x02000000u &&
               context->args == 0x01d9ce80u &&
               context->root_func == 0x00100220u,
           "stored SetupThread context mismatch");
    const auto committed = *context;

    R5900IrExecutionState automatic{};
    automatic.gpr[2] = {0x1111222233334444ull, 0x5555666677778888ull};
    automatic.gpr[3].low64 = 0x3cu;
    automatic.gpr[5].low64 = 0xffffffffu;
    automatic.gpr[6].low64 = 0x1000u;
    const auto automatic_before = automatic;
    const auto automatic_result = setup_service.handle(
        R5900HostSyscallRequest{0x001001c8u, 0x0000000cu},
        automatic,
        memory);
    expect(automatic_result.status == R5900HostSyscallStatus::Unsupported,
           "automatic-stack SetupThread must remain unsupported");
    expect(automatic_result.message.find("automatic-stack") != std::string::npos,
           "automatic-stack diagnostic must identify unsupported mode");
    expect_states_equal(automatic_before, automatic,
                        "automatic-stack Unsupported must not mutate guest state");
    expect(setup_service.setup_thread_context().has_value() &&
               same_context(*setup_service.setup_thread_context(), committed),
           "automatic-stack Unsupported must preserve committed context");

    R5900IrExecutionState zero_size{};
    zero_size.gpr[2] = {0x2222333344445555ull, 0x6666777788889999ull};
    zero_size.gpr[3].low64 = 0x3cu;
    zero_size.gpr[5].low64 = 0x00100000u;
    zero_size.gpr[6].low64 = 0u;
    const auto zero_before = zero_size;
    const auto zero_result = setup_service.handle(
        R5900HostSyscallRequest{0x001001c8u, 0x0000000cu},
        zero_size,
        memory);
    expect(zero_result.status == R5900HostSyscallStatus::Fault,
           "zero SetupThread stack size must fault");
    expect(zero_result.message.find("stack_size") != std::string::npos,
           "zero-size diagnostic must identify stack_size");
    expect_states_equal(zero_before, zero_size,
                        "zero-size Fault must not mutate guest state");
    expect(setup_service.setup_thread_context().has_value() &&
               same_context(*setup_service.setup_thread_context(), committed),
           "zero-size Fault must preserve committed context");

    R5900IrExecutionState negative_size{};
    negative_size.gpr[2] = {0x3333444455556666ull, 0x777788889999aaaaull};
    negative_size.gpr[3].low64 = 0x3cu;
    negative_size.gpr[5].low64 = 0x00100000u;
    negative_size.gpr[6].low64 = 0xffffffffu;
    const auto negative_before = negative_size;
    const auto negative_result = setup_service.handle(
        R5900HostSyscallRequest{0x001001c8u, 0x0000000cu},
        negative_size,
        memory);
    expect(negative_result.status == R5900HostSyscallStatus::Fault,
           "negative SetupThread stack size must fault");
    expect_states_equal(negative_before, negative_size,
                        "negative-size Fault must not mutate guest state");
    expect(setup_service.setup_thread_context().has_value() &&
               same_context(*setup_service.setup_thread_context(), committed),
           "negative-size Fault must preserve committed context");

    R5900IrExecutionState overflow{};
    overflow.gpr[2] = {0x4444555566667777ull, 0x88889999aaaabbbbull};
    overflow.gpr[3].low64 = 0x3cu;
    overflow.gpr[5].low64 = 0xfffff000u;
    overflow.gpr[6].low64 = 0x00002000u;
    const auto overflow_before = overflow;
    const auto overflow_result = setup_service.handle(
        R5900HostSyscallRequest{0x001001c8u, 0x0000000cu},
        overflow,
        memory);
    expect(overflow_result.status == R5900HostSyscallStatus::Fault,
           "overflowing SetupThread stack top must fault");
    expect(overflow_result.message.find("overflows") != std::string::npos,
           "overflow diagnostic must identify guest-address overflow");
    expect_states_equal(overflow_before, overflow,
                        "overflow Fault must not mutate guest state");
    expect(setup_service.setup_thread_context().has_value() &&
               same_context(*setup_service.setup_thread_context(), committed),
           "overflow Fault must preserve committed context");

    std::cout << "r5900_host_syscall_service_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
