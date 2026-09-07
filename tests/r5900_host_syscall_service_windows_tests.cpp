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

bool same_heap_context(
    const b3r::recompiler::R5900SetupHeapContext& lhs,
    const b3r::recompiler::R5900SetupHeapContext& rhs) {
    return lhs.heap_start == rhs.heap_start &&
           lhs.requested_heap_size == rhs.requested_heap_size &&
           lhs.heap_end == rhs.heap_end;
}

void initialize_setup_thread(
    b3r::recompiler::R5900HostSyscallService& service,
    b3r::runtime::Ps2MemoryMap& memory,
    std::uint32_t stack = 0x01ff0000u,
    std::uint32_t stack_size = 0x00010000u) {
    using namespace b3r::recompiler;
    R5900IrExecutionState state{};
    state.gpr[3].low64 = 0x3cu;
    state.gpr[4].low64 = 0x004e8670u;
    state.gpr[5].low64 = stack;
    state.gpr[6].low64 = stack_size;
    state.gpr[7].low64 = 0x01d9ce80u;
    state.gpr[8].low64 = 0x00100220u;
    const auto result = service.handle(
        R5900HostSyscallRequest{0x001001c8u, 0x0000000cu},
        state,
        memory);
    expect(result.status == R5900HostSyscallStatus::Handled,
           "test SetupThread prerequisite must succeed");
}

void expect_setup_heap_fault(
    b3r::recompiler::R5900HostSyscallService& service,
    b3r::runtime::Ps2MemoryMap& memory,
    std::uint32_t heap_start,
    std::uint32_t heap_size,
    const char* diagnostic_term,
    const b3r::recompiler::R5900SetupHeapContext* expected_context) {
    using namespace b3r::recompiler;
    R5900IrExecutionState state{};
    for (std::size_t index = 0; index < 32u; ++index) {
        state.gpr[index] = {
            0x5000000000000000ull + static_cast<std::uint64_t>(index),
            0x6000000000000000ull + static_cast<std::uint64_t>(index),
        };
    }
    state.gpr[3].low64 = 0x3du;
    state.gpr[4].low64 = heap_start;
    state.gpr[5].low64 = heap_size;
    state.hi = 1u;
    state.lo = 2u;
    state.hi1 = 3u;
    state.lo1 = 4u;
    state.sa = 5u;
    state.fpr[1] = 0x3f800000u;
    state.fcr31 = 6u;
    state.fp_acc = 7u;
    const auto before = state;

    const auto result = service.handle(
        R5900HostSyscallRequest{0x001001e4u, 0x0000000cu},
        state,
        memory);
    expect(result.status == R5900HostSyscallStatus::Fault,
           "invalid SetupHeap request must fault");
    expect(result.message.find(diagnostic_term) != std::string::npos,
           "SetupHeap fault diagnostic term mismatch");
    expect_states_equal(before, state,
                        "SetupHeap Fault must preserve guest state");
    if (expected_context == nullptr) {
        expect(!service.setup_heap_context().has_value(),
               "fault must not create SetupHeap context");
    } else {
        expect(service.setup_heap_context().has_value() &&
                   same_heap_context(*service.setup_heap_context(), *expected_context),
               "fault must preserve prior SetupHeap context");
    }
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

    R5900IrExecutionState heap_state{};
    for (std::size_t index = 0; index < 32u; ++index) {
        heap_state.gpr[index] = {
            0x3000000000000000ull + static_cast<std::uint64_t>(index),
            0x4000000000000000ull + static_cast<std::uint64_t>(index),
        };
    }
    heap_state.gpr[2] = {0x1122334455667788ull, 0x8877665544332211ull};
    heap_state.gpr[3].low64 = 0x3du;
    heap_state.gpr[4].low64 = 0x01ecea00u;
    heap_state.gpr[5].low64 = 0xffffffffu;
    heap_state.hi = 0x0102030405060708ull;
    heap_state.lo = 0x1112131415161718ull;
    heap_state.hi1 = 0x2122232425262728ull;
    heap_state.lo1 = 0x3132333435363738ull;
    heap_state.sa = 23u;
    heap_state.fpr[7] = 0x40400000u;
    heap_state.fcr31 = 0x41424344u;
    heap_state.fp_acc = 0x51525354u;
    const auto heap_before = heap_state;
    const auto heap_regions_before = memory.regions();

    const auto heap_result = setup_service.handle(
        R5900HostSyscallRequest{0x001001e4u, 0x0000000cu},
        heap_state,
        memory);

    expect(heap_result.status == R5900HostSyscallStatus::Handled,
           "Burnout automatic-size SetupHeap must be handled");
    expect_states_equal(heap_before, heap_state,
                        "SetupHeap must preserve all guest architectural state");
    expect(memory.regions().size() == heap_regions_before.size(),
           "SetupHeap must not alter memory-map regions");
    const auto& heap_context = setup_service.setup_heap_context();
    expect(heap_context.has_value(), "successful SetupHeap must record context");
    expect(heap_context->heap_start == 0x01ecea00u &&
               heap_context->requested_heap_size == 0xffffffffu &&
               heap_context->heap_end == 0x01ff0000u,
           "stored Burnout SetupHeap context mismatch");
    expect(heap_context->heap_end - heap_context->heap_start == 0x00121600u,
           "Burnout effective heap size mismatch");

    const auto committed_heap = *setup_service.setup_heap_context();

    R5900HostSyscallService no_thread_service{};
    expect_setup_heap_fault(no_thread_service, memory,
                            0x00100000u, 0xffffffffu,
                            "SetupThread", nullptr);

    R5900HostSyscallService explicit_service{};
    initialize_setup_thread(explicit_service, memory);
    R5900IrExecutionState explicit_heap{};
    explicit_heap.gpr[2] = {0xabcdef0123456789ull, 0x9876543210fedcbaull};
    explicit_heap.gpr[3].low64 = 0x3du;
    explicit_heap.gpr[4].low64 = 0x01f00000u;
    explicit_heap.gpr[5].low64 = 0x000f0000u;
    const auto explicit_before = explicit_heap;
    const auto explicit_result = explicit_service.handle(
        R5900HostSyscallRequest{0x001001e4u, 0x0000000cu},
        explicit_heap,
        memory);
    expect(explicit_result.status == R5900HostSyscallStatus::Handled,
           "positive SetupHeap ending at stack_base must be handled");
    expect_states_equal(explicit_before, explicit_heap,
                        "explicit SetupHeap must preserve guest state");
    expect(explicit_service.setup_heap_context().has_value() &&
               explicit_service.setup_heap_context()->heap_start == 0x01f00000u &&
               explicit_service.setup_heap_context()->requested_heap_size == 0x000f0000u &&
               explicit_service.setup_heap_context()->heap_end == 0x01ff0000u,
           "explicit SetupHeap context mismatch");
    const auto explicit_committed = *explicit_service.setup_heap_context();

    expect_setup_heap_fault(explicit_service, memory,
                            0x01ecea00u, 0x00000000u,
                            "positive", &explicit_committed);
    expect_setup_heap_fault(explicit_service, memory,
                            0x01ecea00u, 0xfffffffeu,
                            "positive", &explicit_committed);
    expect_setup_heap_fault(explicit_service, memory,
                            0xfffffff0u, 0x00000020u,
                            "overflows", &explicit_committed);
    expect_setup_heap_fault(explicit_service, memory,
                            0x01ff0000u, 0xffffffffu,
                            "below stack_base", &explicit_committed);
    expect_setup_heap_fault(explicit_service, memory,
                            0x01fe0000u, 0x00020000u,
                            "exceeds stack_base", &explicit_committed);

    R5900IrExecutionState replacement{};
    replacement.gpr[3].low64 = 0x3du;
    replacement.gpr[4].low64 = 0x01f80000u;
    replacement.gpr[5].low64 = 0x00070000u;
    const auto replacement_before = replacement;
    const auto replacement_result = explicit_service.handle(
        R5900HostSyscallRequest{0x001001e4u, 0x0000000cu},
        replacement,
        memory);
    expect(replacement_result.status == R5900HostSyscallStatus::Handled,
           "later valid SetupHeap must replace context");
    expect_states_equal(replacement_before, replacement,
                        "replacement SetupHeap must preserve guest state");
    expect(explicit_service.setup_heap_context().has_value() &&
               explicit_service.setup_heap_context()->heap_start == 0x01f80000u &&
               explicit_service.setup_heap_context()->requested_heap_size == 0x00070000u &&
               explicit_service.setup_heap_context()->heap_end == 0x01ff0000u,
           "replacement SetupHeap context mismatch");

    R5900HostSyscallService lifecycle_service{};
    initialize_setup_thread(lifecycle_service, memory);
    R5900IrExecutionState lifecycle_heap{};
    lifecycle_heap.gpr[3].low64 = 0x3du;
    lifecycle_heap.gpr[4].low64 = 0x01ecea00u;
    lifecycle_heap.gpr[5].low64 = 0xffffffffu;
    const auto lifecycle_heap_result = lifecycle_service.handle(
        R5900HostSyscallRequest{0x001001e4u, 0x0000000cu},
        lifecycle_heap,
        memory);
    expect(lifecycle_heap_result.status == R5900HostSyscallStatus::Handled &&
               lifecycle_service.setup_heap_context().has_value(),
           "lifecycle SetupHeap prerequisite must succeed");
    const auto lifecycle_committed = *lifecycle_service.setup_heap_context();

    R5900IrExecutionState unsupported_thread{};
    unsupported_thread.gpr[3].low64 = 0x3cu;
    unsupported_thread.gpr[5].low64 = 0xffffffffu;
    unsupported_thread.gpr[6].low64 = 0x1000u;
    const auto unsupported_thread_result = lifecycle_service.handle(
        R5900HostSyscallRequest{0x001001c8u, 0x0000000cu},
        unsupported_thread,
        memory);
    expect(unsupported_thread_result.status == R5900HostSyscallStatus::Unsupported,
           "automatic-stack SetupThread lifecycle probe must remain unsupported");
    expect(lifecycle_service.setup_heap_context().has_value() &&
               same_heap_context(*lifecycle_service.setup_heap_context(), lifecycle_committed),
           "unsupported SetupThread must preserve heap context");

    R5900IrExecutionState failed_thread{};
    failed_thread.gpr[3].low64 = 0x3cu;
    failed_thread.gpr[5].low64 = 0x00100000u;
    failed_thread.gpr[6].low64 = 0u;
    const auto failed_thread_result = lifecycle_service.handle(
        R5900HostSyscallRequest{0x001001c8u, 0x0000000cu},
        failed_thread,
        memory);
    expect(failed_thread_result.status == R5900HostSyscallStatus::Fault,
           "invalid SetupThread lifecycle probe must fault");
    expect(lifecycle_service.setup_heap_context().has_value() &&
               same_heap_context(*lifecycle_service.setup_heap_context(), lifecycle_committed),
           "failed SetupThread must preserve heap context");

    initialize_setup_thread(lifecycle_service, memory, 0x01fe0000u, 0x00010000u);
    expect(!lifecycle_service.setup_heap_context().has_value(),
           "successful replacement SetupThread must invalidate stale heap context");

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
    expect(setup_service.setup_heap_context().has_value() &&
               same_heap_context(*setup_service.setup_heap_context(), committed_heap),
           "automatic-stack Unsupported must preserve committed heap context");

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
    expect(setup_service.setup_heap_context().has_value() &&
               same_heap_context(*setup_service.setup_heap_context(), committed_heap),
           "zero-size SetupThread Fault must preserve committed heap context");

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
    expect(setup_service.setup_heap_context().has_value() &&
               same_heap_context(*setup_service.setup_heap_context(), committed_heap),
           "negative SetupThread Fault must preserve committed heap context");

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
    expect(setup_service.setup_heap_context().has_value() &&
               same_heap_context(*setup_service.setup_heap_context(), committed_heap),
           "overflow SetupThread Fault must preserve committed heap context");

#include "r5900_create_sema_test_cases.inc"

    std::cout << "r5900_host_syscall_service_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}