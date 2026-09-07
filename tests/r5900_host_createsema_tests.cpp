#include "recompiler/windows/r5900_host_syscall_service.h"
#include "r5900_createsema_test_support.h"

#include <algorithm>
#include <array>
#include <string>

using namespace b3r::recompiler;
using namespace b3r::test_support::createsema;

namespace {

constexpr std::uint32_t kDescriptor = 0x01ffffa0u;
constexpr R5900HostSyscallRequest kRequest{0x0010be24u, 0x0000000cu};

R5900IrExecutionState make_state(std::uint32_t address = kDescriptor) {
    R5900IrExecutionState state{};
    for (std::size_t i = 1; i < state.gpr.size(); ++i) {
        state.gpr[i] = {0xaaaa000000000000ull + i, 0xbbbb000000000000ull + i};
        state.fpr[i] = static_cast<std::uint32_t>(0x12340000u + i);
    }
    state.gpr[3].low64 = 0xcccc000000000040ull;
    state.gpr[4].low64 = 0xdddd000000000000ull | address;
    state.hi = 1u; state.lo = 2u; state.hi1 = 3u; state.lo1 = 4u;
    state.sa = 5u; state.fcr31 = 6u; state.fp_acc = 7u;
    return state;
}

void expect_state(const R5900IrExecutionState& actual,
                  const R5900IrExecutionState& expected) {
    for (std::size_t i = 0; i < actual.gpr.size(); ++i) {
        expect(actual.gpr[i].low64 == expected.gpr[i].low64 &&
                   actual.gpr[i].high64 == expected.gpr[i].high64,
               "unexpected GPR mutation");
    }
    expect(actual.hi == expected.hi && actual.lo == expected.lo &&
               actual.hi1 == expected.hi1 && actual.lo1 == expected.lo1 &&
               actual.sa == expected.sa && actual.fpr == expected.fpr &&
               actual.fcr31 == expected.fcr31 && actual.fp_acc == expected.fp_acc,
           "unexpected special/FP state mutation");
}

} // namespace

int main() {
    auto memory = make_memory();
    R5900HostSyscallService service;
    descriptor(memory, kDescriptor);
    const auto all_ram = *memory.translate(0u, 0x02000000u);
    const std::vector<std::uint8_t> before_ram(all_ram.begin(), all_ram.end());
    auto state = make_state();
    auto expected = state;
    const auto result = service.handle(kRequest, state, memory);
    expect(result.status == R5900HostSyscallStatus::Handled,
           "CreateSema must handle the observed startup descriptor");
    expected.gpr[2].low64 = 1u;
    expect_state(state, expected);
    expect(std::equal(all_ram.begin(), all_ram.end(), before_ram.begin()),
           "CreateSema must preserve all guest RAM");

    // A second call through the same pointer is a new object, not a memoized result.
    state = make_state();
    expect(service.handle(kRequest, state, memory).status == R5900HostSyscallStatus::Handled &&
               state.gpr[2].low64 == 2u,
           "each successful creation must return a distinct positive ID");

    // Failed calls must not change state or consume an ID.
    const auto reject = [&](std::uint32_t address, R5900HostSyscallStatus status,
                            const char* detail, R5900HostSyscallRequest request = kRequest) {
        auto bad = make_state(address);
        const auto before = bad;
        const auto rejected = service.handle(request, bad, memory);
        expect(rejected.status == status, "invalid create must stop with the expected status");
        expect(rejected.message.find(detail) != std::string::npos,
               "rejection must explain the parameter fault");
        expect_state(bad, before);
    };
    reject(0u, R5900HostSyscallStatus::Fault, "descriptor");
    reject(kDescriptor + 1u, R5900HostSyscallStatus::Fault, "aligned");
    reject(0x01ffffecu, R5900HostSyscallStatus::Fault, "descriptor");
    reject(0x02000000u, R5900HostSyscallStatus::Fault, "descriptor");
    reject(0xfffffff0u, R5900HostSyscallStatus::Fault, "descriptor");
    reject(kDescriptor, R5900HostSyscallStatus::Fault, "not SYSCALL", {kRequest.guest_pc, 0u});
    for (const auto counts : {std::array{0u, 0u}, std::array{0u, 0xffffffffu},
                              std::array{0xffffffffu, 1u}, std::array{2u, 1u}}) {
        descriptor(memory, kDescriptor, counts[0], counts[1]);
        reject(kDescriptor, R5900HostSyscallStatus::Fault, "count");
    }
    descriptor(memory, kDescriptor, 1u, 1u, 1u);
    reject(kDescriptor, R5900HostSyscallStatus::Unsupported, "attr");
    descriptor(memory, kDescriptor, 0u, 0x7fffffffu);
    state = make_state();
    expect(service.handle(kRequest, state, memory).status == R5900HostSyscallStatus::Handled &&
               state.gpr[2].low64 == 3u,
           "rejections must not consume IDs; zero initial count must be valid");

    // Exact last-byte fit and unbacked memory are distinct cases.
    descriptor(memory, 0x01ffffe8u);
    state = make_state(0x01ffffe8u);
    expect(service.handle(kRequest, state, memory).status == R5900HostSyscallStatus::Handled,
           "descriptor ending exactly at RAM end must work");
    b3r::runtime::Ps2MemoryMap empty;
    state = make_state();
    expected = state;
    expect(service.handle(kRequest, state, empty).status == R5900HostSyscallStatus::Fault,
           "unbacked memory must fault without dereference");
    expect_state(state, expected);

    // The fixed v0 capacity is checked before allocation, without ID wraparound.
    R5900HostSyscallService bounded;
    descriptor(memory, kDescriptor);
    for (std::uint64_t id = 1u; id <= 256u; ++id) {
        state = make_state();
        expect(bounded.handle(kRequest, state, memory).status == R5900HostSyscallStatus::Handled &&
                   state.gpr[2].low64 == id,
               "capacity IDs must be unique and monotonic");
    }
    state = make_state();
    expected = state;
    expect(bounded.handle(kRequest, state, memory).status == R5900HostSyscallStatus::Fault,
           "exhaustion must stop transactionally");
    expect_state(state, expected);
    R5900HostSyscallService fresh;
    expect(fresh.handle(kRequest, state, memory).status == R5900HostSyscallStatus::Handled &&
               state.gpr[2].low64 == 1u,
           "semaphore IDs must be owned by the service instance");
    std::cout << "r5900_host_createsema_tests: PASS\n";
}
