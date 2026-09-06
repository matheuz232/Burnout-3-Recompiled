# R5900 Host Syscall Infrastructure v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an injectable Windows-side R5900 host syscall service boundary, integrate it into `R5900BlockDispatcher`, preserve current trap behavior when no service is configured, and prove deterministic handled/unsupported/fault behavior without putting syscall logic into generated x64.

**Architecture:** Add a narrow `IR5900HostSyscallService` interface plus a production `R5900HostSyscallService` shell beside the Windows dispatcher. The dispatcher recognizes decoded `SYSCALL` as a host boundary, executes any native prefix first, calls the injected service, and resumes at `guest_pc + 4` only after `Handled`; unsupported and fault outcomes stop at the exact syscall PC. This plan intentionally stops before implementing named `SetupThread` kernel semantics; that becomes the next independent milestone once stack/result semantics are validated.

**Tech Stack:** C++20, CMake 3.25+, Visual Studio 2022 x64 / MSVC, CTest, GitHub Actions `windows-2022`, existing R5900 decoder/IR/x64 backend/dispatcher and `Ps2MemoryMap`.

**Spec:** `docs/superpowers/specs/2026-09-06-r5900-host-syscall-service-v0-design.md`

## Global Constraints

- Windows x86-64 only for the syscall service integration; portable targets must continue to configure/build as before.
- Add no third-party dependency.
- Generated x64 must contain no syscall-specific host logic.
- `SYSCALL` is never lowered into ordinary R5900 IR in this milestone.
- `Handled` always resumes at exactly `guest_pc + 4`; the service cannot choose an arbitrary next PC.
- `Unsupported` and `Fault` never advance guest PC.
- A null service pointer preserves the current `Trap` behavior and keeps existing callers source-compatible.
- `blocks_executed` and `instructions_executed` retain their current native-dispatch meanings; handled syscalls are counted only in `syscalls_handled`.
- `gpr[0]` remains zero; unrelated CPU state and guest memory remain unchanged unless a future validated handler explicitly mutates them.
- Guest memory remains owned by `Ps2MemoryMap`; the service receives it by reference and retains no translated span after `handle()` returns.
- No Burnout 3 ELF bytes, ISO data, assets, dumps, or proprietary payloads are committed.
- Do not implement `SetupThread`, `SetupHeap`, scheduler behavior, or any other named EE kernel service in this plan.
- Public ABI evidence for the next milestone is already available: PS2SDK identifies `SetupThread` as selector `0x3c`/60; the standard startup sequence places the selector in GPR3 (`v1`), arguments in GPR4..GPR8, and consumes GPR2 (`v0`) as the returned stack pointer. This plan may expose the selector diagnostically but must not claim the full kernel behavior is implemented.

---

## File Structure

**Create**

- `src/recompiler/windows/r5900_host_syscall_service.h` — injectable service contract and production shell declaration.
- `src/recompiler/windows/r5900_host_syscall_service.cpp` — raw-SYSCALL validation, EE selector extraction, deterministic unsupported result.
- `tests/r5900_host_syscall_service_windows_tests.cpp` — production-shell unit coverage.
- `tests/r5900_block_dispatcher_syscall_windows_tests.cpp` — fake-service dispatcher integration coverage.

**Modify**

- `src/recompiler/windows/r5900_block_dispatcher.h` — service pointer, stop reasons, syscall counter.
- `src/recompiler/windows/r5900_block_dispatcher.cpp` — syscall boundary detection, service dispatch, resume/stop handling.
- `tests/r5900_block_dispatcher_bss_clear_windows_tests.cpp` — BSS → OR → SYSCALL continuation regression.
- `CMakeLists.txt` — compile service into `b3r_recompiler_dispatcher_x64` and register two Windows tests.
- `README.md` — describe the new host syscall boundary without claiming `SetupThread` is handled.
- `docs/PROGRESS.md` — record CI evidence and the next `SetupThread` gate.

No changes are required in `r5900_ir.*`, `r5900_ir_executor.*`, `r5900_x64_backend.*`, or `Ps2MemoryMap` for this milestone.

---

### Task 1: Host syscall service contract and deterministic production shell

**Files:**
- Create: `src/recompiler/windows/r5900_host_syscall_service.h`
- Create: `src/recompiler/windows/r5900_host_syscall_service.cpp`
- Create: `tests/r5900_host_syscall_service_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `R5900IrExecutionState`, `runtime::Ps2MemoryMap`, `decode_r5900()`.
- Produces: `R5900HostSyscallStatus`, `R5900HostSyscallRequest`, `R5900HostSyscallResult`, `IR5900HostSyscallService`, `R5900HostSyscallService::handle(...)`.

- [ ] **Step 1: Add the failing unit-test target**

Inside the existing Windows test block in `CMakeLists.txt`, add:

```cmake
add_executable(r5900_host_syscall_service_windows_tests
  tests/r5900_host_syscall_service_windows_tests.cpp
)
target_link_libraries(r5900_host_syscall_service_windows_tests PRIVATE
  b3r_recompiler_dispatcher_x64
)
add_test(NAME r5900_host_syscall_service_windows_tests
  COMMAND r5900_host_syscall_service_windows_tests)
```

Also add the future production source to the existing dispatcher library:

```cmake
add_library(b3r_recompiler_dispatcher_x64 STATIC
  src/recompiler/windows/r5900_block_dispatcher.cpp
  src/recompiler/windows/r5900_host_syscall_service.cpp
)
```

- [ ] **Step 2: Write the failing service test**

Create `tests/r5900_host_syscall_service_windows_tests.cpp`:

```cpp
#include "recompiler/windows/r5900_host_syscall_service.h"

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

} // namespace

int main() {
    using namespace b3r::recompiler;

    runtime::Ps2MemoryMap memory{};
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

    std::cout << "r5900_host_syscall_service_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
```

- [ ] **Step 3: Run RED verification**

```powershell
cmake --preset vs2022-release
cmake --build --preset vs2022-release --target r5900_host_syscall_service_windows_tests
```

Expected: build fails because `r5900_host_syscall_service.h/.cpp` do not exist.

Commit the RED state:

```bash
git add CMakeLists.txt tests/r5900_host_syscall_service_windows_tests.cpp
git commit -m "test: define R5900 host syscall service contract"
```

- [ ] **Step 4: Create the public service header**

Create `src/recompiler/windows/r5900_host_syscall_service.h`:

```cpp
#pragma once

#include "recompiler/r5900_ir_executor.h"
#include "runtime/ps2_memory_map.h"

#include <cstdint>
#include <string>

namespace b3r::recompiler {

enum class R5900HostSyscallStatus {
    Handled,
    Unsupported,
    Fault,
};

struct R5900HostSyscallRequest {
    std::uint32_t guest_pc{};
    std::uint32_t raw_instruction{};
};

struct R5900HostSyscallResult {
    R5900HostSyscallStatus status{R5900HostSyscallStatus::Unsupported};
    std::string message{};
};

class IR5900HostSyscallService {
public:
    virtual ~IR5900HostSyscallService() = default;

    [[nodiscard]] virtual R5900HostSyscallResult handle(
        const R5900HostSyscallRequest& request,
        R5900IrExecutionState& state,
        runtime::Ps2MemoryMap& memory) = 0;
};

class R5900HostSyscallService final : public IR5900HostSyscallService {
public:
    [[nodiscard]] R5900HostSyscallResult handle(
        const R5900HostSyscallRequest& request,
        R5900IrExecutionState& state,
        runtime::Ps2MemoryMap& memory) override;
};

} // namespace b3r::recompiler
```

- [ ] **Step 5: Implement the minimum production shell**

Create `src/recompiler/windows/r5900_host_syscall_service.cpp`:

```cpp
#include "recompiler/windows/r5900_host_syscall_service.h"

#include "recompiler/r5900_decoder.h"

#include <iomanip>
#include <sstream>

namespace b3r::recompiler {
namespace {

std::int32_t ee_syscall_selector(const R5900IrExecutionState& state) noexcept {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(state.gpr[3].low64));
}

std::string format_pc(std::uint32_t pc) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << pc;
    return out.str();
}

} // namespace

R5900HostSyscallResult R5900HostSyscallService::handle(
    const R5900HostSyscallRequest& request,
    R5900IrExecutionState& state,
    runtime::Ps2MemoryMap& memory) {
    (void)memory;

    if (decode_r5900(request.raw_instruction).instruction != R5900Instruction::Syscall) {
        return {
            R5900HostSyscallStatus::Fault,
            "host syscall at guest PC " + format_pc(request.guest_pc) +
                ": raw instruction is not SYSCALL",
        };
    }

    const auto selector = ee_syscall_selector(state);
    std::ostringstream out;
    out << "host syscall at guest PC " << format_pc(request.guest_pc)
        << ": unsupported EE syscall selector " << std::dec << selector
        << " (0x" << std::hex << static_cast<std::uint32_t>(selector) << ')';

    return {R5900HostSyscallStatus::Unsupported, out.str()};
}

} // namespace b3r::recompiler
```

The production shell deliberately has no `Handled` selector yet.

- [ ] **Step 6: Run GREEN verification and commit**

```powershell
cmake --build --preset vs2022-release --target r5900_host_syscall_service_windows_tests
ctest --preset vs2022-release -R r5900_host_syscall_service_windows_tests --output-on-failure
```

Expected: PASS.

```bash
git add src/recompiler/windows/r5900_host_syscall_service.h \
        src/recompiler/windows/r5900_host_syscall_service.cpp \
        CMakeLists.txt tests/r5900_host_syscall_service_windows_tests.cpp
git commit -m "feat: add R5900 host syscall service shell"
```

---

### Task 2: Dispatcher service injection and entry-SYSCALL outcomes

**Files:**
- Modify: `src/recompiler/windows/r5900_block_dispatcher.h`
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Create: `tests/r5900_block_dispatcher_syscall_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `IR5900HostSyscallService::handle(...)` from Task 1.
- Produces: `R5900BlockDispatcherOptions::host_syscalls`, `R5900DispatchStopReason::UnsupportedSyscall`, `R5900DispatchStopReason::HostSyscallFailure`, `R5900DispatchResult::syscalls_handled`.

- [ ] **Step 1: Add the failing dispatcher-syscall test target**

Add inside the Windows test block:

```cmake
add_executable(r5900_block_dispatcher_syscall_windows_tests
  tests/r5900_block_dispatcher_syscall_windows_tests.cpp
)
target_link_libraries(r5900_block_dispatcher_syscall_windows_tests PRIVATE
  b3r_recompiler_dispatcher_x64
)
add_test(NAME r5900_block_dispatcher_syscall_windows_tests
  COMMAND r5900_block_dispatcher_syscall_windows_tests)
```

- [ ] **Step 2: Write fake-service RED cases**

Create `tests/r5900_block_dispatcher_syscall_windows_tests.cpp`. Reuse the same compact synthetic ELF helper shape already used by `r5900_block_dispatcher_windows_tests.cpp`: one executable PT_LOAD containing the supplied word vector at `0x00100000`.

Use this fake service:

```cpp
class FakeHostSyscallService final : public b3r::recompiler::IR5900HostSyscallService {
public:
    explicit FakeHostSyscallService(b3r::recompiler::R5900HostSyscallStatus status)
        : status_(status) {}

    b3r::recompiler::R5900HostSyscallResult handle(
        const b3r::recompiler::R5900HostSyscallRequest& request,
        b3r::recompiler::R5900IrExecutionState& state,
        b3r::runtime::Ps2MemoryMap&) override {
        ++calls;
        last_request = request;
        if (status_ == b3r::recompiler::R5900HostSyscallStatus::Handled) {
            state.gpr[2].low64 = 0x12345678u;
            return {status_, "handled by fake"};
        }
        return {status_, status_ == b3r::recompiler::R5900HostSyscallStatus::Unsupported
                            ? "unsupported by fake"
                            : "fault from fake"};
    }

    std::size_t calls{};
    b3r::recompiler::R5900HostSyscallRequest last_request{};

private:
    b3r::recompiler::R5900HostSyscallStatus status_;
};
```

Use guest words:

```cpp
constexpr std::uint32_t kSyscall = 0x0000000cu;
constexpr std::uint32_t kUnsupportedXori =
    (0x0eu << 26u) | (1u << 21u) | (1u << 16u) | 1u;
```

Add four independent cases:

```cpp
// Null service: preserve current trap semantics.
{
    auto memory = make_memory({kSyscall}, base);
    R5900BlockDispatcher dispatcher(memory);
    R5900IrExecutionState state{};
    const auto result = dispatcher.run(base, state, 1u);
    expect(result.reason == R5900DispatchStopReason::Trap,
           "null syscall service must preserve Trap");
    expect(result.next_pc == base && result.syscalls_handled == 0u,
           "null service must not advance or count syscall");
}

// Handled service: resume at PC+4, then stop on unsupported XORI.
{
    auto memory = make_memory({kSyscall, kUnsupportedXori}, base);
    FakeHostSyscallService service(R5900HostSyscallStatus::Handled);
    R5900BlockDispatcherOptions options{};
    options.host_syscalls = &service;
    R5900BlockDispatcher dispatcher(memory, options);
    R5900IrExecutionState state{};

    const auto result = dispatcher.run(base, state, 1u);
    expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction,
           "handled syscall must resume to the next guest instruction");
    expect(result.next_pc == base + 4u,
           "handled syscall must resume at PC+4");
    expect(result.blocks_executed == 0u && result.instructions_executed == 0u,
           "host syscall must not count as a native block/instruction");
    expect(result.syscalls_handled == 1u && service.calls == 1u,
           "handled syscall must increment only host counter");
    expect(service.last_request.guest_pc == base &&
               service.last_request.raw_instruction == kSyscall,
           "dispatcher must forward exact syscall provenance");
    expect(state.gpr[2].low64 == 0x12345678u,
           "host-visible state mutation must survive resume");
}

// Unsupported service: stop exactly at syscall.
{
    auto memory = make_memory({kSyscall}, base);
    FakeHostSyscallService service(R5900HostSyscallStatus::Unsupported);
    R5900BlockDispatcherOptions options{};
    options.host_syscalls = &service;
    R5900BlockDispatcher dispatcher(memory, options);
    R5900IrExecutionState state{};
    const auto result = dispatcher.run(base, state, 1u);
    expect(result.reason == R5900DispatchStopReason::UnsupportedSyscall,
           "unsupported host syscall must have dedicated stop reason");
    expect(result.next_pc == base && result.syscalls_handled == 0u,
           "unsupported host syscall must not advance or count handled");
    expect(result.message.find("unsupported by fake") != std::string::npos,
           "dispatcher must preserve service diagnostic");
}

// Fault service: stop exactly at syscall.
{
    auto memory = make_memory({kSyscall}, base);
    FakeHostSyscallService service(R5900HostSyscallStatus::Fault);
    R5900BlockDispatcherOptions options{};
    options.host_syscalls = &service;
    R5900BlockDispatcher dispatcher(memory, options);
    R5900IrExecutionState state{};
    const auto result = dispatcher.run(base, state, 1u);
    expect(result.reason == R5900DispatchStopReason::HostSyscallFailure,
           "faulting host syscall must have dedicated stop reason");
    expect(result.next_pc == base && result.syscalls_handled == 0u,
           "faulting host syscall must not advance or count handled");
    expect(result.message.find("fault from fake") != std::string::npos,
           "dispatcher must preserve service fault diagnostic");
}
```

- [ ] **Step 3: Run RED verification**

```powershell
cmake --build --preset vs2022-release --target r5900_block_dispatcher_syscall_windows_tests
```

Expected: compile failure because dispatcher options/results do not expose the new host-syscall contract.

```bash
git add CMakeLists.txt tests/r5900_block_dispatcher_syscall_windows_tests.cpp
git commit -m "test: define dispatcher host syscall outcomes"
```

- [ ] **Step 4: Extend the dispatcher public contract**

In `r5900_block_dispatcher.h`, include the new service header:

```cpp
#include "recompiler/windows/r5900_host_syscall_service.h"
```

Add stop reasons immediately after `Trap`:

```cpp
UnsupportedSyscall,
HostSyscallFailure,
```

Add to `R5900DispatchResult`:

```cpp
std::size_t syscalls_handled{};
```

Add to `R5900BlockDispatcherOptions`:

```cpp
IR5900HostSyscallService* host_syscalls{};
```

The pointer is non-owning.

- [ ] **Step 5: Add one dispatcher-side host-boundary helper**

Near the existing local helper functions in `r5900_block_dispatcher.cpp`, add:

```cpp
enum class HostSyscallDisposition {
    Resume,
    Stop,
};

HostSyscallDisposition handle_host_syscall_boundary(
    IR5900HostSyscallService* service,
    runtime::Ps2MemoryMap& memory,
    const analysis::R5900InstructionSite& site,
    R5900IrExecutionState& state,
    R5900DispatchResult& result,
    std::uint32_t& current_pc) {
    if (service == nullptr) {
        result.reason = R5900DispatchStopReason::Trap;
        result.next_pc = site.pc;
        return HostSyscallDisposition::Stop;
    }

    const auto host = service->handle(
        R5900HostSyscallRequest{site.pc, site.decoded.raw}, state, memory);

    switch (host.status) {
    case R5900HostSyscallStatus::Handled:
        ++result.syscalls_handled;
        current_pc = site.pc + 4u;
        result.next_pc = current_pc;
        return HostSyscallDisposition::Resume;

    case R5900HostSyscallStatus::Unsupported:
        result.reason = R5900DispatchStopReason::UnsupportedSyscall;
        result.next_pc = site.pc;
        result.message = host.message;
        return HostSyscallDisposition::Stop;

    case R5900HostSyscallStatus::Fault:
        result.reason = R5900DispatchStopReason::HostSyscallFailure;
        result.next_pc = site.pc;
        result.message = host.message;
        return HostSyscallDisposition::Stop;
    }

    result.reason = R5900DispatchStopReason::HostSyscallFailure;
    result.next_pc = site.pc;
    result.message = "host syscall service returned invalid status";
    return HostSyscallDisposition::Stop;
}
```

- [ ] **Step 6: Detect `SYSCALL` separately from generic traps**

In the basic-block scan, declare:

```cpp
const analysis::R5900InstructionSite* syscall_site = nullptr;
```

Replace the generic system-class branch with:

```cpp
if (site.decoded.instruction_class == R5900InstructionClass::System) {
    boundary_pc = site.pc;
    if (site.decoded.instruction == R5900Instruction::Syscall) {
        syscall_site = &site;
    } else {
        boundary_reason = R5900DispatchStopReason::Trap;
    }
    break;
}
```

Do not push the syscall into `body_sites` and do not add it to `guest_words`.

- [ ] **Step 7: Handle an entry syscall before the empty-body early return**

Before the existing `body_sites.empty()` boundary return, add:

```cpp
if (body_sites.empty() && !has_supported_transfer && syscall_site != nullptr) {
    const auto disposition = handle_host_syscall_boundary(
        options_.host_syscalls,
        memory_,
        *syscall_site,
        state,
        result,
        current_pc);
    if (disposition == HostSyscallDisposition::Resume) {
        continue;
    }
    return result;
}
```

The remaining empty-body logic stays unchanged for non-syscall boundaries.

- [ ] **Step 8: Handle a syscall after a successfully executed native prefix**

After native execution updates `blocks_executed`, `instructions_executed`, `current_pc`, and `next_pc`, but before the existing `boundary_reason` return, add:

```cpp
if (syscall_site != nullptr) {
    const auto disposition = handle_host_syscall_boundary(
        options_.host_syscalls,
        memory_,
        *syscall_site,
        state,
        result,
        current_pc);
    if (disposition == HostSyscallDisposition::Resume) {
        continue;
    }
    return result;
}
```

This ordering is mandatory: the supported native prefix must commit before host syscall handling.

- [ ] **Step 9: Run GREEN verification and commit**

```powershell
cmake --build --preset vs2022-release --target r5900_block_dispatcher_syscall_windows_tests
ctest --preset vs2022-release -R "r5900_(host_syscall_service|block_dispatcher_syscall)_windows_tests" --output-on-failure
```

Expected: both tests PASS.

```bash
git add src/recompiler/windows/r5900_block_dispatcher.h \
        src/recompiler/windows/r5900_block_dispatcher.cpp \
        CMakeLists.txt tests/r5900_block_dispatcher_syscall_windows_tests.cpp
git commit -m "feat: dispatch R5900 syscalls to host service"
```

---

### Task 3: Prove BSS-clear → OR → host syscall → PC+4 integration

**Files:**
- Modify: `tests/r5900_block_dispatcher_bss_clear_windows_tests.cpp`

**Interfaces:**
- Consumes: dispatcher host-service integration from Task 2.
- Produces: synthetic startup-shaped evidence that native BSS/OR state is committed before host syscall handling and that handled continuation reaches the next boundary.

- [ ] **Step 1: Extend the synthetic executable by one unsupported instruction after `SYSCALL`**

In `make_memory()`, append one word after the existing syscall:

```cpp
i_type(0x0eu, 1u, 1u, 1u), // XORI r1,r1,1: deliberate post-syscall boundary
```

Keep `kSyscallPc = kCodeBase + 0x20u`; define:

```cpp
constexpr std::uint32_t kPostSyscallPc = kSyscallPc + 4u;
```

The existing null-service run must still stop at `kSyscallPc` with the same native BSS/OR counters.

- [ ] **Step 2: Add a minimal handled fake service to the test file**

Inside the anonymous namespace:

```cpp
class HandledHostSyscalls final : public b3r::recompiler::IR5900HostSyscallService {
public:
    b3r::recompiler::R5900HostSyscallResult handle(
        const b3r::recompiler::R5900HostSyscallRequest& request,
        b3r::recompiler::R5900IrExecutionState&,
        b3r::runtime::Ps2MemoryMap&) override {
        ++calls;
        last_request = request;
        return {b3r::recompiler::R5900HostSyscallStatus::Handled, {}};
    }

    std::size_t calls{};
    b3r::recompiler::R5900HostSyscallRequest last_request{};
};
```

- [ ] **Step 3: Write the RED continuation assertions**

After the existing null-service case, create a fresh memory/dispatcher/state and inject the fake service:

```cpp
{
    auto handled_memory = make_memory(kCodeBase, kDataBase);
    HandledHostSyscalls host{};
    R5900BlockDispatcherOptions options{};
    options.host_syscalls = &host;
    R5900BlockDispatcher handled_dispatcher(handled_memory, options);

    R5900IrExecutionState handled_state{};
    handled_state.gpr[2].low64 = kClearBegin;
    handled_state.gpr[3].low64 = kClearEnd;
    handled_state.gpr[4] = {0x00ff00000000ff00ull, 0x1111111111111111ull};
    handled_state.gpr[5] = {0x0f000f000f00000full, 0x2222222222222222ull};
    handled_state.gpr[6] = {0u, 0xaaaaaaaaaaaaaaaaull};

    const auto handled = handled_dispatcher.run(kCodeBase, handled_state, 16u);

    expect(handled.reason == R5900DispatchStopReason::UnsupportedInstruction,
           "handled syscall must continue to the deliberate XORI boundary");
    expect(handled.next_pc == kPostSyscallPc,
           "handled syscall continuation must land at PC+4");
    expect(handled.blocks_executed == 10u && handled.instructions_executed == 28u,
           "host syscall must not change native BSS/OR accounting");
    expect(handled.syscalls_handled == 1u && host.calls == 1u,
           "exactly one syscall must be handled after the native prefix");
    expect(host.last_request.guest_pc == kSyscallPc &&
               host.last_request.raw_instruction == 0x0000000cu,
           "host service must receive exact synthetic syscall provenance");
    expect(handled_state.gpr[2].low64 == kClearEnd,
           "BSS pointer state must be committed before host handling");
    expect(handled_state.gpr[6].low64 == 0x0fff0f000f00ff0full &&
               handled_state.gpr[6].high64 == 0xaaaaaaaaaaaaaaaaull,
           "post-loop OR state must be committed before host handling");
}
```

- [ ] **Step 4: Run RED on the pre-integration commit if this task is developed independently**

If Task 2 has not yet landed, the test must fail because the dispatcher cannot inject/handle a syscall. If Task 2 is already GREEN, treat the new assertion set as the regression gate and verify it directly.

```powershell
cmake --build --preset vs2022-release --target r5900_block_dispatcher_bss_clear_windows_tests
ctest --preset vs2022-release -R r5900_block_dispatcher_bss_clear_windows_tests --output-on-failure
```

- [ ] **Step 5: Run GREEN and commit**

Expected: null-service case still stops at `Trap @ kSyscallPc`; handled-service case resumes to `kPostSyscallPc`, preserves native counters, records exactly one handled syscall, and stops on XORI.

```bash
git add tests/r5900_block_dispatcher_bss_clear_windows_tests.cpp
git commit -m "test: continue startup through handled syscall boundary"
```

---

### Task 4: Full regression, documentation, and handoff to `SetupThread`

**Files:**
- Modify: `README.md`
- Modify: `docs/PROGRESS.md`

**Interfaces:**
- Consumes: all infrastructure from Tasks 1–3.
- Produces: auditable milestone status and a clean next boundary for a separate `SetupThread` implementation plan.

- [ ] **Step 1: Run focused Windows tests**

```powershell
cmake --preset vs2022-release
cmake --build --preset vs2022-release
ctest --preset vs2022-release -R "r5900_(host_syscall_service|block_dispatcher_syscall|block_dispatcher_bss_clear)_windows_tests" --output-on-failure
```

Expected: zero failures.

- [ ] **Step 2: Run the complete test suite**

```powershell
ctest --preset vs2022-release --output-on-failure
```

Expected: every registered test passes. Record the actual total rather than predicting a fixed count in documentation.

- [ ] **Step 3: Verify the full GitHub Actions Windows workflow**

Push the implementation branch and require the existing Windows workflow to complete with:

- Configure: success
- Build: success
- Test: success
- Frame pacing telemetry: success
- Pacing probe smoke: success
- Analyzer package validation: success
- Pacing probe package validation: success

Do not mark the milestone CI-validated before the workflow itself is complete.

- [ ] **Step 4: Update `README.md` capability wording**

Add these facts to the current milestone list:

```text
- an injectable Windows R5900 host-syscall boundary that keeps EE kernel HLE outside generated x64;
- null-service SYSCALL behavior remains a deterministic Trap;
- handled host syscalls resume at guest PC+4 and are counted separately from native blocks/instructions;
- unsupported/faulting host syscalls stop at the exact syscall PC with dedicated dispatcher reasons;
- the synthetic BSS-clear -> register OR path can now cross a handled syscall boundary and reach the following unsupported instruction.
```

Also state explicitly:

```text
SetupThread itself is not implemented by this milestone. The production host service currently decodes/diagnoses the EE selector and returns Unsupported until a named handler is validated.
```

- [ ] **Step 5: Update `docs/PROGRESS.md`**

Add a row:

```markdown
| R5900 host syscall service v0 | CI_VALIDATED | Injectable `IR5900HostSyscallService` keeps syscall HLE outside generated x64. Null service preserves `Trap`; fake handled/unsupported/fault paths prove PC+4 resume, exact-PC stop behavior, separate `syscalls_handled` accounting, and BSS-clear -> OR -> SYSCALL continuation. Production service currently reports unknown EE selectors as unsupported; `SetupThread` remains the next milestone. |
```

Replace `CI_VALIDATED` with the actual pre-CI status until the workflow is green, then record the final commit SHA/run/job IDs and actual CTest count.

- [ ] **Step 6: Commit documentation after fresh verification**

```bash
git add README.md docs/PROGRESS.md
git commit -m "docs: record R5900 host syscall infrastructure"
```

- [ ] **Step 7: Final verification before completion claim**

Re-run or re-read the fresh CI evidence for the final head commit. Completion may be claimed only if the final head—not an earlier commit—has a green full Windows workflow.

---

## Scope boundary for the next plan

After this plan is complete, create a separate `SetupThread` implementation plan. That plan may rely on these already-established observations but must validate the kernel-visible stack/result semantics before enabling selector `0x3c` in production:

- syscall selector: GPR3 / `v1` = 60 (`0x3c`);
- arguments: GPR4..GPR8 = `gp`, `stack`, `stack_size`, `args`, `root_func`;
- return consumed by startup: GPR2 / `v0`, immediately moved to `sp`;
- real Burnout 3 boundary already documented in-repository: `SYSCALL @ 0x001001c8`, followed later by `SetupHeap` selector `0x3d`.

The next plan must define the exact `SetupThread` return/stack calculation and any kernel bookkeeping with testable evidence; it must not infer those semantics solely from the function name.

## Plan Self-Review

- Spec coverage: service interface, production shell, null-service trap compatibility, handled/unsupported/fault outcomes, PC+4 resume, separate syscall accounting, no syscall IR/x64 emission, BSS/OR integration, documentation, and full Windows CI are all assigned to explicit tasks.
- Scope split: named `SetupThread` semantics are intentionally separated because they are independently reviewable and require additional kernel-behavior evidence.
- Placeholder scan: no `TBD`, `TODO`, unspecified validation step, or unnamed implementation action remains in this plan.
- Type consistency: `IR5900HostSyscallService`, `R5900HostSyscallRequest`, `R5900HostSyscallResult`, `R5900HostSyscallStatus`, `host_syscalls`, `UnsupportedSyscall`, `HostSyscallFailure`, and `syscalls_handled` use the same names/signatures across every task.
