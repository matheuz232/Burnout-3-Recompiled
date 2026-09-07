# PS2 PAD Guest Bridge v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the validated `Ps2PadReport` consumable by EE guest code through a reusable guest-function HLE boundary and a minimal port-0/slot-0 `libpad`-compatible service.

**Architecture:** Add a generic `IR5900GuestCallService` that the block dispatcher consults before cache lookup and block analysis. Implement `Ps2PadHleService` above that interface, with explicit guest-PC bindings and transactional writes into `Ps2MemoryMap`. Keep Windows/XInput, SIO2, SIF RPC, IOP/PADMAN, real Burnout 3 addresses, and game-specific patches outside this milestone.

**Tech Stack:** C++20, CMake 3.25+, MSVC/Visual Studio 2022 x64, existing R5900 dispatcher/IR state, `Ps2MemoryMap`, `Ps2PadReport`, GitHub Actions Windows CI.

**Spec:** `docs/superpowers/specs/2026-09-07-ps2-pad-guest-bridge-v0-design.md`

## Global Constraints

- Do not hard-code or guess Burnout 3 `libpad` entry addresses; tests use synthetic PCs only.
- `IR5900GuestCallService` stays separate from `IR5900HostSyscallService`.
- Guest-call HLE is queried before native cache lookup and block analysis/compilation.
- A handled guest call resumes at low32(`$ra`) and does not count as a guest instruction or native block.
- PAD HLE supports only port 0 / slot 0 in v0.
- `padPortOpen` requires a non-zero, 64-byte-aligned, fully backed 256-byte EE RAM region.
- Connected `padRead` writes exactly 32 bytes and returns 32; disconnected `padRead` returns 0 and writes nothing.
- Guest-memory writes are transactional from the bridge perspective.
- No SIO2, SIF RPC, IOP, PADMAN, pressure mode, rumble, multitap, port 1, automatic symbol discovery, boot/menu/gameplay claim, or proprietary game data.
- Preserve all existing dispatcher, syscall, cache, memory-fault, pacing and package-validation behavior.

---

## File Structure

### New files

- `src/recompiler/r5900_guest_call_service.h` — generic guest-call request/status/result/interface; forward-declare `runtime::Ps2MemoryMap` to avoid a library dependency cycle.
- `src/runtime/ps2_pad_hle_service.h` — PAD bindings and public service API/state access required by tests.
- `src/runtime/ps2_pad_hle_service.cpp` — explicit-PC routing, EE ABI extraction, lifecycle handlers and `padRead` packing/copy.
- `tests/r5900_block_dispatcher_guest_call_windows_tests.cpp` — dispatcher hook, cache and syscall regressions.
- `tests/ps2_pad_hle_service_tests.cpp` — portable PAD HLE contract.
- `docs/validation/2026-09-07-ps2-pad-guest-bridge-v0.md` — TDD/CI evidence and remaining external binding boundary.

### Modified files

- `src/recompiler/windows/r5900_block_dispatcher.h` — add `GuestCallFailure`, `guest_calls_handled`, and `guest_calls` option.
- `src/recompiler/windows/r5900_block_dispatcher.cpp` — consult guest-call service before cache/analysis, resume through `$ra`, stop on fault.
- `CMakeLists.txt` — add service source, link `b3r_runtime` to `b3r_input`, register portable/Windows tests.
- `docs/PROGRESS.md` — record the bridge without claiming real Burnout 3 bindings/input consumption.

---

### Task 1: Generic R5900 guest-call boundary

**Files:**
- Create: `src/recompiler/r5900_guest_call_service.h`
- Create: `tests/r5900_block_dispatcher_guest_call_windows_tests.cpp`
- Modify: `src/recompiler/windows/r5900_block_dispatcher.h`
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `R5900IrExecutionState`, `runtime::Ps2MemoryMap`, existing `R5900BlockDispatcher` loop.
- Produces:

```cpp
#pragma once

#include "recompiler/r5900_ir_executor.h"

#include <cstdint>
#include <string>

namespace b3r::runtime { class Ps2MemoryMap; }

namespace b3r::recompiler {

struct R5900GuestCallRequest {
    std::uint32_t guest_pc{};
};

enum class R5900GuestCallStatus {
    Handled,
    NotHandled,
    Fault,
};

struct R5900GuestCallResult {
    R5900GuestCallStatus status{R5900GuestCallStatus::NotHandled};
    std::string message{};
};

class IR5900GuestCallService {
public:
    virtual ~IR5900GuestCallService() = default;
    [[nodiscard]] virtual R5900GuestCallResult try_handle(
        const R5900GuestCallRequest& request,
        R5900IrExecutionState& state,
        runtime::Ps2MemoryMap& memory) = 0;
};

} // namespace b3r::recompiler
```

Modify the existing dispatcher types without replacing their existing members:

```cpp
// Append to R5900DispatchStopReason:
GuestCallFailure,

// Append to R5900DispatchResult counters:
std::size_t guest_calls_handled{};

// Append to R5900BlockDispatcherOptions:
IR5900GuestCallService* guest_calls{};
```

- [ ] **Step 1: Write the failing dispatcher test**

Create `tests/r5900_block_dispatcher_guest_call_windows_tests.cpp`. Reuse the synthetic ELF/memory construction pattern from `tests/r5900_block_dispatcher_syscall_windows_tests.cpp`. Define this fake:

```cpp
class FakeGuestCallService final : public b3r::recompiler::IR5900GuestCallService {
public:
    b3r::recompiler::R5900GuestCallStatus status{
        b3r::recompiler::R5900GuestCallStatus::NotHandled};
    std::uint32_t expected_pc{};
    std::size_t calls{};

    b3r::recompiler::R5900GuestCallResult try_handle(
        const b3r::recompiler::R5900GuestCallRequest& request,
        b3r::recompiler::R5900IrExecutionState& state,
        b3r::runtime::Ps2MemoryMap&) override {
        ++calls;
        if (request.guest_pc != expected_pc) {
            return {b3r::recompiler::R5900GuestCallStatus::Fault,
                    "unexpected guest PC"};
        }
        if (status == b3r::recompiler::R5900GuestCallStatus::Handled) {
            state.gpr[2].low64 = 0x12345678u;
            return {status, {}};
        }
        if (status == b3r::recompiler::R5900GuestCallStatus::Fault) {
            return {status, "guest-call fault"};
        }
        return {status, {}};
    }
};
```

Assert all cases:

```text
1. null guest service -> existing unsupported-instruction result unchanged
2. NotHandled at entry -> existing dispatcher path unchanged
3. Handled at entry -> guest_calls_handled=1, no native block/instruction consumed,
   v0 mutation survives, dispatcher resumes at low32(ra)
4. intercepted entry creates no cache entry
5. Fault -> GuestCallFailure, next_pc is intercepted PC, exact message preserved
6. Handled wins before analysis even if intercepted PC contains an unsupported instruction
7. existing host SYSCALL service still handles SYSCALL when guest service returns NotHandled
```

Register:

```cmake
if(WIN32)
  add_executable(r5900_block_dispatcher_guest_call_windows_tests
    tests/r5900_block_dispatcher_guest_call_windows_tests.cpp)
  target_link_libraries(r5900_block_dispatcher_guest_call_windows_tests PRIVATE
    b3r_recompiler_dispatcher_x64)
  add_test(NAME r5900_block_dispatcher_guest_call_windows_tests
    COMMAND r5900_block_dispatcher_guest_call_windows_tests)
endif()
```

- [ ] **Step 2: Commit and verify RED**

```bash
git add tests/r5900_block_dispatcher_guest_call_windows_tests.cpp CMakeLists.txt
git commit -m "test: define R5900 guest call boundary"
```

Run Windows CI on that exact commit. Expected: Build fails because `r5900_guest_call_service.h` and the new dispatcher members do not exist. An infrastructure/checkout failure is not an acceptable RED.

- [ ] **Step 3: Implement the minimal dispatcher hook**

Create the header exactly as defined above. Include it from `r5900_block_dispatcher.h`.

At the start of each dispatcher loop iteration, before `cache_.find(current_pc)` and before `analyze_r5900_basic_block(...)`, add:

```cpp
if (options_.guest_calls != nullptr) {
    const auto intercepted_pc = current_pc;
    const auto guest = options_.guest_calls->try_handle(
        R5900GuestCallRequest{intercepted_pc}, state, memory_);

    if (guest.status == R5900GuestCallStatus::Handled) {
        ++result.guest_calls_handled;
        current_pc = static_cast<std::uint32_t>(state.gpr[31].low64);
        result.next_pc = current_pc;
        continue;
    }
    if (guest.status == R5900GuestCallStatus::Fault) {
        result.reason = R5900DispatchStopReason::GuestCallFailure;
        result.next_pc = intercepted_pc;
        result.message = guest.message;
        return result;
    }
}
```

`NotHandled` falls through. Do not change `blocks_executed`, `instructions_executed`, `syscalls_handled`, or cache state for a handled guest call.

- [ ] **Step 4: Commit GREEN and verify**

```bash
git add src/recompiler/r5900_guest_call_service.h \
        src/recompiler/windows/r5900_block_dispatcher.h \
        src/recompiler/windows/r5900_block_dispatcher.cpp
git commit -m "feat: add R5900 guest call HLE boundary"
```

Run:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Then require Windows CI green on that production commit, including pacing/package gates.

Review gate: hook order, `$ra` resume, counter semantics, cache non-consumption, exact fault provenance and syscall independence.

---

### Task 2: PAD HLE lifecycle and explicit routing

**Files:**
- Create: `src/runtime/ps2_pad_hle_service.h`
- Create: `src/runtime/ps2_pad_hle_service.cpp`
- Create: `tests/ps2_pad_hle_service_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 guest-call interface, `Ps2PadReport`, `R5900IrExecutionState`, `Ps2MemoryMap`.
- Produces:

```cpp
#pragma once

#include "input/ps2_pad_report.h"
#include "recompiler/r5900_guest_call_service.h"

#include <cstdint>

namespace b3r::runtime {

struct Ps2PadHleBindings {
    std::uint32_t pad_init{};
    std::uint32_t pad_port_open{};
    std::uint32_t pad_get_state{};
    std::uint32_t pad_read{};
    std::uint32_t pad_port_close{};
    std::uint32_t pad_end{};
};

class Ps2PadHleService final : public recompiler::IR5900GuestCallService {
public:
    explicit Ps2PadHleService(Ps2PadHleBindings bindings) noexcept;
    void set_report(const input::Ps2PadReport& report) noexcept;

    [[nodiscard]] recompiler::R5900GuestCallResult try_handle(
        const recompiler::R5900GuestCallRequest& request,
        recompiler::R5900IrExecutionState& state,
        Ps2MemoryMap& memory) override;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool port_open() const noexcept;
    [[nodiscard]] std::uint32_t pad_area_address() const noexcept;

private:
    Ps2PadHleBindings bindings_{};
    input::Ps2PadReport report_{};
    bool initialized_{};
    bool port_open_{};
    std::uint32_t pad_area_address_{};
};

} // namespace b3r::runtime
```

Use internal constants:

```cpp
constexpr std::uint32_t kPadStateDisconnected = 0x00u;
constexpr std::uint32_t kPadStateStable = 0x06u;
constexpr std::size_t kPadAreaSize = 256u;
constexpr std::size_t kPadAreaAlignment = 64u;
```

- [ ] **Step 1: Write lifecycle/routing tests before production**

Use synthetic non-zero PCs:

```cpp
constexpr b3r::runtime::Ps2PadHleBindings kBindings{
    .pad_init = 0x00110000u,
    .pad_port_open = 0x00110020u,
    .pad_get_state = 0x00110040u,
    .pad_read = 0x00110060u,
    .pad_port_close = 0x00110080u,
    .pad_end = 0x001100a0u,
};
```

Cover:

```text
- unbound PC and zero-valued binding -> NotHandled with no mutation
- each non-read binding routes to the correct handler
- padInit(0) -> Handled, initialized=true, low32(v0)=1, v0.high64 preserved
- padInit(nonzero) -> Fault and initialized remains false
- padPortOpen before init -> Fault
- only port=0, slot=0 accepted
- null, misaligned and 256-byte-crossing padArea -> Fault
- valid padPortOpen -> open=true, address recorded, v0=1
- failed re-open preserves prior open/address state
- padGetState closed -> 0x00
- open + disconnected report -> 0x00
- open + connected report -> 0x06
- invalid port/slot getState -> Fault
- padPortClose -> closed, address=0, v0=1; repeated close remains successful/idempotent
- padEnd -> initialized=false, closed, address=0, v0=1
- set_report snapshot remains stored across padEnd/re-init
```

Modify CMake:

```cmake
add_library(b3r_runtime
  src/runtime/ps2_memory_map.cpp
  src/runtime/ps2_pad_hle_service.cpp
)
target_link_libraries(b3r_runtime PUBLIC b3r_recompiler b3r_input)

add_executable(ps2_pad_hle_service_tests tests/ps2_pad_hle_service_tests.cpp)
target_link_libraries(ps2_pad_hle_service_tests PRIVATE b3r_runtime)
add_test(NAME ps2_pad_hle_service_tests COMMAND ps2_pad_hle_service_tests)
```

- [ ] **Step 2: Commit and verify RED**

```bash
git add tests/ps2_pad_hle_service_tests.cpp CMakeLists.txt
git commit -m "test: define PS2 PAD HLE lifecycle contract"
```

Expected Windows CI RED: Configure fails specifically because `src/runtime/ps2_pad_hle_service.cpp` is absent.

- [ ] **Step 3: Implement lifecycle routing**

Use helpers with concrete behavior:

```cpp
std::uint32_t gpr_low32(const recompiler::R5900IrExecutionState& state,
                        std::size_t index) noexcept {
    return static_cast<std::uint32_t>(state.gpr[index].low64);
}

recompiler::R5900GuestCallResult handled() {
    return {recompiler::R5900GuestCallStatus::Handled, {}};
}

recompiler::R5900GuestCallResult fault(std::string message) {
    return {recompiler::R5900GuestCallStatus::Fault, std::move(message)};
}
```

A request PC of zero is never intercepted. A binding with value zero is never intercepted. For a bound function, validate state/arguments and return `Fault` rather than falling through.

`padPortOpen` extracts `port=a0`, `slot=a1`, `padArea=a2`. After requiring initialized state and `port=slot=0`, validate before mutation:

```cpp
const auto pad_area = gpr_low32(state, 6u);
if (pad_area == 0u) {
    return fault("padPortOpen: padArea must be non-null");
}
if ((pad_area % kPadAreaAlignment) != 0u) {
    return fault("padPortOpen: padArea must be 64-byte aligned");
}
if (!memory.translate(pad_area, kPadAreaSize).has_value()) {
    return fault("padPortOpen: complete 256-byte padArea must be backed by EE RAM");
}

port_open_ = true;
pad_area_address_ = pad_area;
state.gpr[2].low64 = 1u;
return handled();
```

`padGetState` uses `report_.connected`; it must not inspect Windows/XInput state.

- [ ] **Step 4: Commit GREEN and verify**

```bash
git add src/runtime/ps2_pad_hle_service.h src/runtime/ps2_pad_hle_service.cpp
git commit -m "feat: add PS2 PAD HLE lifecycle"
```

Run portable test, full CTest and Windows CI. Require all tests plus pacing/package gates green.

Review gate: explicit bindings only, transactional lifecycle state, only port 0/slot 0, no Windows/SIO2/PADMAN dependency.

---

### Task 3: `padRead` 32-byte ABI and transactional guest write

**Files:**
- Modify: `tests/ps2_pad_hle_service_tests.cpp`
- Modify: `src/runtime/ps2_pad_hle_service.cpp`

**Interfaces:**
- Consumes: Task 2 service and `input::Ps2PadReport`.
- Produces exact connected `padButtonStatus` bytes and disconnected no-write behavior.

- [ ] **Step 1: Extend tests first**

Connected fixture:

```cpp
b3r::input::Ps2PadReport report{};
report.connected = true;
report.buttons_active_low = 0xb5aau;
report.right_x = 0x11u;
report.right_y = 0x22u;
report.left_x = 0x33u;
report.left_y = 0x44u;
service.set_report(report);
```

After init/open, call the `pad_read` binding with:

```cpp
state.gpr[4].low64 = 0u;
state.gpr[5].low64 = 0u;
state.gpr[6].low64 = data_address;
```

Pre-fill 32 destination bytes with `0xa5`. Assert success returns 32 and bytes are exactly:

```text
00: 00
01: 79
02: aa
03: b5
04: 11
05: 22
06: 33
07: 44
08..31: 00
```

Also test:

```text
- connected read before init/open -> Fault, destination unchanged
- invalid port/slot -> Fault, destination unchanged
- null destination -> Fault
- destination with fewer than 32 backed bytes -> Fault, all backed bytes unchanged
- disconnected open pad -> Handled, v0=0, destination unchanged even when pointer is null/invalid
- a report derived without XInput metadata behaves identically
- successful read preserves v0.high64
```

- [ ] **Step 2: Commit and verify RED**

```bash
git add tests/ps2_pad_hle_service_tests.cpp
git commit -m "test: define PS2 PAD read ABI contract"
```

Expected Windows CI RED: Build succeeds, existing tests remain green, and only `ps2_pad_hle_service_tests` fails on the new `padRead` assertions.

- [ ] **Step 3: Implement exact packing and one-shot copy**

Check in this order:

```text
1. service initialized
2. port/slot are 0/0
3. port open
4. if report_.connected == false: write v0=0 and return Handled without validating/writing data
5. connected data pointer is non-zero
6. memory.translate(data, 32) succeeds
7. assemble all 32 host-local bytes
8. copy all 32 bytes to the already validated span
9. write v0=32 and return Handled
```

Pack with:

```cpp
std::array<std::uint8_t, 32> bytes{};
bytes[0] = 0x00u;
bytes[1] = 0x79u;
bytes[2] = static_cast<std::uint8_t>(report_.buttons_active_low & 0xffu);
bytes[3] = static_cast<std::uint8_t>((report_.buttons_active_low >> 8u) & 0xffu);
bytes[4] = report_.right_x;
bytes[5] = report_.right_y;
bytes[6] = report_.left_x;
bytes[7] = report_.left_y;
```

The value-initialized array makes bytes 8..31 zero. Copy through the validated mutable span:

```cpp
const auto destination = memory.translate(data_address, bytes.size());
if (!destination.has_value()) {
    return fault("padRead: complete 32-byte destination must be backed by EE RAM");
}
std::copy(bytes.begin(), bytes.end(), destination->begin());
state.gpr[2].low64 = 32u;
return handled();
```

Do not perform 32 independent `write_u8` calls.

- [ ] **Step 4: Commit GREEN and verify**

```bash
git add src/runtime/ps2_pad_hle_service.cpp
git commit -m "feat: expose PS2 PAD reports to guest memory"
```

Run `ps2_pad_hle_service_tests`, complete CTest and Windows CI. Require all tests plus pacing/package gates green.

Review gate: exact 32-byte layout, little-endian buttons, return 32, disconnected return 0/no-write, no partial-write path.

---

### Task 4: Documentation and exact-head validation

**Files:**
- Create: `docs/validation/2026-09-07-ps2-pad-guest-bridge-v0.md`
- Modify: `docs/PROGRESS.md`

**Interfaces:**
- Consumes: exact RED/GREEN SHAs and CI evidence from Tasks 1-3.
- Produces: auditable milestone record and current engineering snapshot.

- [ ] **Step 1: Verify spec completion before documentation**

Require every check true:

```text
[ ] generic IR5900GuestCallService exists
[ ] dispatcher queries it before cache/analysis
[ ] Handled resumes through low32(ra)
[ ] Fault maps to GuestCallFailure
[ ] NotHandled preserves normal execution
[ ] PAD bindings are explicit and zero is unbound
[ ] lifecycle state rules are covered by tests
[ ] padPortOpen validates 64-byte alignment and full 256-byte backing
[ ] connected padRead writes exact 32 bytes and returns 32
[ ] disconnected padRead returns 0 and writes nothing
[ ] no Burnout 3 addresses were guessed/hard-coded
[ ] no SIO2/SIF/IOP/PADMAN claim was introduced
[ ] syscall/cache/pacing/package regressions pass
```

If any item is false, fix it in the responsible TDD task before documenting completion.

- [ ] **Step 2: Write validation record**

Create `docs/validation/2026-09-07-ps2-pad-guest-bridge-v0.md` with:

```markdown
# PS2 PAD Guest Bridge v0 Validation

## Scope
## Architecture delivered
## TDD evidence
## Generic guest-call dispatcher evidence
## PAD lifecycle evidence
## padRead ABI / transactionality evidence
## Windows CI evidence
## Remaining external boundary
```

Record exact RED/GREEN SHAs, CI run IDs/jobs, test counts, pacing telemetry, and package-validation status. Include this boundary statement:

```text
Real Burnout 3 libpad function PCs remain unbound. The bridge is guest-visible
when supplied explicit bindings, but this milestone does not prove that the
current Burnout 3 external ELF path consumes host input.
```

- [ ] **Step 3: Update progress snapshot**

Add/update:

```text
PS2 PAD guest bridge | CI_VALIDATED | Generic guest-call HLE + minimal libpad port0/slot0 bridge; real Burnout 3 function bindings remain pending external validation
```

Keep the pre-existing project statuses `TODO` for Graphics/GS/VU, IOP/SPU2/audio, Game initialization, and Menu/gameplay. These are status values, not implementation placeholders. Keep `Real external next boundary` as `PENDING_EXTERNAL_VALIDATION` unless a complete lawful external ELF has actually been measured.

- [ ] **Step 4: Commit documentation once**

```bash
git add docs/PROGRESS.md docs/validation/2026-09-07-ps2-pad-guest-bridge-v0.md
git commit -m "docs: record PS2 PAD guest bridge validation"
```

- [ ] **Step 5: Run fresh exact-documentation-head Windows CI**

Require workflow checkout SHA to equal the documentation commit SHA. Fresh logs must show:

```text
Configure                              PASS
Build                                  PASS
CTest                                  all tests PASS, 0 failed
ps2_pad_hle_service_tests              PASS
r5900_block_dispatcher_guest_call      PASS
existing syscall tests                 PASS
Frame pacing telemetry                 PASS
120 Hz pacing probe                    PASS
Analyzer package validation            PASS
Pacing package validation              PASS
```

Only after that exact-head run is green may the milestone be called `CI_VALIDATED`. Do not make another documentation edit solely to encode the self-referential final CI number; the validation record may cite the implementation-head CI, while the subsequent exact-doc-head run is the final external gate.

---

## Execution Notes

- Use TDD RED -> GREEN for every behavior-changing task.
- Each RED must fail for the expected missing behavior, not infrastructure.
- After every GREEN, perform spec-compliance and code-quality review before advancing.
- Prefer test-only RED commits followed by production-only GREEN commits.
- Do not weaken `Ps2MemoryMap`, ELF validation, or dispatcher faults to make PAD tests pass.
- Do not fabricate proprietary bytes or guest addresses.
