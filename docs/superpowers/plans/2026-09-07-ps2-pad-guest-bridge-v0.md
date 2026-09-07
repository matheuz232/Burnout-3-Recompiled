# PS2 PAD Guest Bridge v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the validated `Ps2PadReport` consumable by EE guest code through a reusable guest-function HLE boundary and a minimal port-0/slot-0 `libpad`-compatible service.

**Architecture:** Add a generic `IR5900GuestCallService` that the block dispatcher consults before block lookup/analysis. Implement `Ps2PadHleService` above that interface, with explicit guest-PC bindings and transactional writes into `Ps2MemoryMap`. Keep Windows/XInput, SIO2, SIF RPC, IOP/PADMAN, real Burnout 3 addresses, and game-specific patches outside this milestone.

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

- `src/recompiler/r5900_guest_call_service.h` — generic guest-call request/status/result/interface. Forward-declare `runtime::Ps2MemoryMap` to avoid introducing a library dependency cycle.
- `src/runtime/ps2_pad_hle_service.h` — PAD bindings, constants, public service API/state access needed by tests.
- `src/runtime/ps2_pad_hle_service.cpp` — explicit-PC routing, EE ABI argument extraction, lifecycle handlers, `padRead` packing/transactional copy.
- `tests/r5900_block_dispatcher_guest_call_windows_tests.cpp` — generic dispatcher hook behavior and syscall/cache regressions.
- `tests/ps2_pad_hle_service_tests.cpp` — portable PAD HLE contract.
- `docs/validation/2026-09-07-ps2-pad-guest-bridge-v0.md` — TDD/CI evidence and remaining external binding boundary.

### Modified files

- `src/recompiler/windows/r5900_block_dispatcher.h` — add `GuestCallFailure`, `guest_calls_handled`, and `guest_calls` option.
- `src/recompiler/windows/r5900_block_dispatcher.cpp` — query guest-call service at `current_pc` before cache/analysis, resume through `$ra`, stop on fault.
- `CMakeLists.txt` — add service source, link `b3r_runtime` to `b3r_input`, register new portable and Windows tests.
- `docs/PROGRESS.md` — record the new bridge status without claiming real Burnout 3 bindings/input consumption.

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

}
```

Dispatcher additions:

```cpp
enum class R5900DispatchStopReason {
    // existing values...
    GuestCallFailure,
};

struct R5900DispatchResult {
    // existing fields...
    std::size_t guest_calls_handled{};
};

struct R5900BlockDispatcherOptions {
    analysis::R5900ControlFlowOptions block_options{};
    IR5900HostSyscallService* host_syscalls{};
    IR5900GuestCallService* guest_calls{};
};
```

- [ ] **Step 1: Write the failing dispatcher tests**

Create `tests/r5900_block_dispatcher_guest_call_windows_tests.cpp` using the same synthetic ELF/memory helpers as `r5900_block_dispatcher_syscall_windows_tests.cpp` and a fake service:

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
        return {status, status == b3r::recompiler::R5900GuestCallStatus::Fault
                            ? "guest-call fault"
                            : std::string{}};
    }
};
```

Test all of these cases explicitly:

```text
1. null guest service -> existing unsupported instruction result unchanged
2. NotHandled at entry -> existing dispatcher path unchanged
3. Handled at entry -> no block/instruction consumed, guest_calls_handled=1,
   v0 mutation survives, current PC becomes low32(ra), and execution continues there
4. Handled call -> intercepted entry does not create a native cache entry
5. Fault -> GuestCallFailure, next_pc is intercepted PC, exact message preserved
6. Guest-call hook is observed before analysis by using an otherwise unmappable/
   unsupported instruction at the intercepted PC and requiring Handled to win
7. Existing host SYSCALL service still handles SYSCALL after guest service returns NotHandled
```

Register the Windows test target in `CMakeLists.txt`:

```cmake
if(WIN32)
  add_executable(r5900_block_dispatcher_guest_call_windows_tests
    tests/r5900_block_dispatcher_guest_call_windows_tests.cpp
  )
  target_link_libraries(r5900_block_dispatcher_guest_call_windows_tests PRIVATE
    b3r_recompiler_dispatcher_x64
  )
  add_test(NAME r5900_block_dispatcher_guest_call_windows_tests
    COMMAND r5900_block_dispatcher_guest_call_windows_tests)
endif()
```

- [ ] **Step 2: Commit and run RED**

Commit only the test/CMake registration before production interface/dispatcher changes.

```bash
git add tests/r5900_block_dispatcher_guest_call_windows_tests.cpp CMakeLists.txt
git commit -m "test: define R5900 guest call boundary"
```

Run Windows CI on that exact commit. Expected: **Build failure** because `recompiler/r5900_guest_call_service.h` and the new dispatcher members do not exist yet. Any unrelated Configure/infrastructure failure does not count as RED.

- [ ] **Step 3: Implement the minimal interface and dispatcher hook**

Create `src/recompiler/r5900_guest_call_service.h` with the exact interface above. Include it from `r5900_block_dispatcher.h`.

At the top of each dispatcher loop iteration, before `cache_.find(current_pc)` and before `analyze_r5900_basic_block(...)`, add equivalent logic:

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

`NotHandled` falls through unchanged. Do not increment `blocks_executed`, `instructions_executed`, or `syscalls_handled` for a handled guest call.

- [ ] **Step 4: Run GREEN and full regression suite**

Run locally when available:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Then run Windows CI on the production commit. Expected: all tests pass, including the new guest-call test; pacing/package gates remain green.

- [ ] **Step 5: Commit production**

```bash
git add src/recompiler/r5900_guest_call_service.h \
        src/recompiler/windows/r5900_block_dispatcher.h \
        src/recompiler/windows/r5900_block_dispatcher.cpp
git commit -m "feat: add R5900 guest call HLE boundary"
```

Review gate: verify hook order, counter semantics, `$ra` resume, fault provenance, cache non-consumption, and syscall independence before Task 2.

---

### Task 2: PAD HLE lifecycle and explicit routing

**Files:**
- Create: `src/runtime/ps2_pad_hle_service.h`
- Create: `src/runtime/ps2_pad_hle_service.cpp`
- Create: `tests/ps2_pad_hle_service_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `IR5900GuestCallService`, `Ps2PadReport`, `R5900IrExecutionState`, `Ps2MemoryMap`.
- Produces:

```cpp
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

}
```

Constants used internally/tests:

```cpp
constexpr std::uint32_t kPadStateDisconnected = 0x00u;
constexpr std::uint32_t kPadStateStable = 0x06u;
constexpr std::size_t kPadAreaSize = 256u;
constexpr std::size_t kPadAreaAlignment = 64u;
```

- [ ] **Step 1: Write lifecycle/routing tests before production**

Create a portable test executable that builds a synthetic EE RAM map and uses synthetic non-zero aligned function PCs such as:

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
- unbound PC and binding value 0 -> NotHandled, no state/register mutation
- each non-read binding routes to its intended handler
- padInit(0) -> Handled, initialized=true, low32(v0)=1, v0.high64 preserved
- padInit(nonzero) -> Fault and no initialization
- padPortOpen before init -> Fault
- padPortOpen only accepts port=0, slot=0
- padPortOpen rejects null padArea
- padPortOpen rejects non-64-byte alignment
- padPortOpen rejects a 256-byte region crossing the end of EE RAM
- valid padPortOpen records address, opens port, returns 1
- any failed re-open leaves previous open/address state unchanged
- padGetState closed -> 0x00
- padGetState open + disconnected report -> 0x00
- padGetState open + connected report -> 0x06
- port/slot != 0/0 for getState -> Fault
- padPortClose valid -> closed, address=0, return 1
- repeated padPortClose -> remains closed, return 1
- padEnd -> initialized=false, closed, address=0, return 1
- set_report snapshot survives padEnd and can be used after re-init/re-open
```

Register:

```cmake
add_executable(ps2_pad_hle_service_tests tests/ps2_pad_hle_service_tests.cpp)
target_link_libraries(ps2_pad_hle_service_tests PRIVATE b3r_runtime)
add_test(NAME ps2_pad_hle_service_tests COMMAND ps2_pad_hle_service_tests)
```

Also add the production source to `b3r_runtime` and dependency:

```cmake
add_library(b3r_runtime
  src/runtime/ps2_memory_map.cpp
  src/runtime/ps2_pad_hle_service.cpp
)
target_link_libraries(b3r_runtime PUBLIC b3r_recompiler b3r_input)
```

- [ ] **Step 2: Commit and run RED**

Commit tests and CMake registration while `ps2_pad_hle_service.cpp` is absent.

```bash
git add tests/ps2_pad_hle_service_tests.cpp CMakeLists.txt
git commit -m "test: define PS2 PAD HLE lifecycle contract"
```

Run Windows CI. Expected: Configure fails specifically because `src/runtime/ps2_pad_hle_service.cpp` is missing. Do not accept an unrelated failure as the RED.

- [ ] **Step 3: Implement minimal routing/lifecycle behavior**

Routing rule:

```cpp
if (request.guest_pc == 0u) {
    return {recompiler::R5900GuestCallStatus::NotHandled, {}};
}
```

Only intercept when the request PC equals one of the **non-zero** binding fields. Extract low32 arguments with a small helper:

```cpp
std::uint32_t gpr_low32(const recompiler::R5900IrExecutionState& state,
                        std::size_t index) noexcept {
    return static_cast<std::uint32_t>(state.gpr[index].low64);
}
```

Write returns without clobbering `gpr[2].high64`:

```cpp
state.gpr[2].low64 = static_cast<std::uint32_t>(value);
```

For `padPortOpen`, validate the complete region before state mutation:

```cpp
const auto pad_area = gpr_low32(state, 6u);
if (pad_area == 0u || (pad_area % kPadAreaAlignment) != 0u ||
    !memory.translate(pad_area, kPadAreaSize).has_value()) {
    return fault(...);
}

port_open_ = true;
pad_area_address_ = pad_area;
state.gpr[2].low64 = 1u;
return handled();
```

`padGetState` must read `report_.connected`, not XInput metadata.

- [ ] **Step 4: Run GREEN and full suite**

Run the portable test plus full CTest; then Windows CI. Expected: all tests and pacing/package gates green.

- [ ] **Step 5: Commit production**

```bash
git add src/runtime/ps2_pad_hle_service.h src/runtime/ps2_pad_hle_service.cpp
git commit -m "feat: add PS2 PAD HLE lifecycle"
```

Review gate: explicit bindings only, no guessed game addresses, all state mutations transactional, only port 0/slot 0 supported, no Windows/SIO2/PADMAN dependencies.

---

### Task 3: `padRead` 32-byte ABI and transactional guest write

**Files:**
- Modify: `tests/ps2_pad_hle_service_tests.cpp`
- Modify: `src/runtime/ps2_pad_hle_service.cpp`

**Interfaces:**
- Consumes: Task 2 service and `input::Ps2PadReport`.
- Produces connected `padRead(0,0,data)` with exact 32-byte public `padButtonStatus` layout and disconnected no-write behavior.

- [ ] **Step 1: Extend tests first**

Add connected fixture:

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

After init/open, set arguments:

```cpp
state.gpr[4].low64 = 0u;          // port
state.gpr[5].low64 = 0u;          // slot
state.gpr[6].low64 = data_address; // 32-byte destination
```

Pre-fill destination with `0xa5` and assert successful `padRead` returns 32 and memory bytes are exactly:

```text
offset 0      00          ok
offset 1      79          DualShock 2 analog mode
offset 2..3   aa b5       btns little-endian
offset 4      11          right_x
offset 5      22          right_y
offset 6      33          left_x
offset 7      44          left_y
offset 8..31  00          pressure/reserved bytes
```

Add tests for:

```text
- connected padRead before initialization/open -> Fault and destination unchanged
- connected padRead port/slot != 0/0 -> Fault and destination unchanged
- connected padRead null destination -> Fault
- connected padRead destination with fewer than 32 backed bytes -> Fault and all backed bytes unchanged
- disconnected open pad -> Handled, v0=0, destination unchanged even if destination is invalid/null
- connected keyboard-originated-equivalent Ps2PadReport works identically; the service has no gamepad_connected input
- successful padRead preserves v0.high64
```

- [ ] **Step 2: Commit and run RED**

```bash
git add tests/ps2_pad_hle_service_tests.cpp
git commit -m "test: define PS2 PAD read ABI contract"
```

Run Windows CI. Expected: only `ps2_pad_hle_service_tests` fails on the newly asserted `padRead` behavior; existing tests/build remain green.

- [ ] **Step 3: Implement local 32-byte packing and one-shot copy**

Use fixed host-local storage:

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

The value-initialized array guarantees offsets 8..31 are zero.

Order the handler checks exactly:

```text
1. initialized
2. port/slot 0/0
3. port open
4. if !report_.connected: v0=0; Handled; no destination validation/write
5. connected: data != 0
6. translate(data, 32) succeeds
7. assemble full local array
8. copy all 32 bytes into the validated span
9. v0=32; Handled
```

Use the already validated mutable span from `memory.translate(data, 32)` and `std::copy(bytes.begin(), bytes.end(), span->begin())`; do not call 32 independent `write_u8` operations.

- [ ] **Step 4: Run GREEN and full suite**

Run `ps2_pad_hle_service_tests`, all CTest, then Windows CI. Expected: all tests pass; pacing/package gates remain green.

- [ ] **Step 5: Commit production**

```bash
git add src/runtime/ps2_pad_hle_service.cpp
git commit -m "feat: expose PS2 PAD reports to guest memory"
```

Review gate: exact ABI offsets, little-endian buttons, 32-byte return, disconnected zero/no-write semantics, no partial write path.

---

### Task 4: Milestone documentation and exact-head validation

**Files:**
- Create: `docs/validation/2026-09-07-ps2-pad-guest-bridge-v0.md`
- Modify: `docs/PROGRESS.md`

**Interfaces:**
- Consumes: exact RED/GREEN commit SHAs and Windows CI run/job evidence from Tasks 1-3.
- Produces: an auditable milestone record and current engineering snapshot.

- [ ] **Step 1: Re-read spec and compare implementation head**

Verify each completion criterion directly against source/tests:

```text
[ ] generic IR5900GuestCallService exists
[ ] dispatcher queries it before cache/analysis
[ ] Handled resumes through low32(ra)
[ ] Fault -> GuestCallFailure
[ ] NotHandled preserves normal execution
[ ] PAD bindings are explicit and zero is unbound
[ ] lifecycle calls satisfy v0 state rules
[ ] padPortOpen validates 64-byte alignment + full 256 bytes
[ ] padRead connected writes exact 32 bytes and returns 32
[ ] padRead disconnected returns 0 and writes nothing
[ ] no Burnout 3 addresses were guessed/hard-coded
[ ] no SIO2/SIF/IOP/PADMAN claim was introduced
[ ] existing syscall/cache/pacing/package regressions pass
```

If any item is false, return to the responsible task before documenting completion.

- [ ] **Step 2: Write validation record**

Create `docs/validation/2026-09-07-ps2-pad-guest-bridge-v0.md` with sections:

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

Record exact RED/GREEN SHAs, CI run numbers/IDs/jobs, test counts, pacing telemetry, and package-validation status. State explicitly:

```text
Real Burnout 3 libpad function PCs remain unbound. The bridge is guest-visible
when supplied explicit bindings, but this milestone does not prove that the
current Burnout 3 external ELF path consumes host input.
```

- [ ] **Step 3: Update `docs/PROGRESS.md`**

Add/update a row equivalent to:

```text
PS2 PAD guest bridge | CI_VALIDATED | Generic guest-call HLE + minimal libpad port0/slot0 bridge; real Burnout 3 function bindings remain pending external validation
```

Keep:

```text
Graphics / GS / VU      TODO
IOP / SPU2 / audio      TODO
Game initialization     TODO
Menu / gameplay         TODO
```

Do not change `Real external next boundary` from `PENDING_EXTERNAL_VALIDATION` without a complete lawful external ELF measurement.

- [ ] **Step 4: Commit documentation once**

```bash
git add docs/PROGRESS.md docs/validation/2026-09-07-ps2-pad-guest-bridge-v0.md
git commit -m "docs: record PS2 PAD guest bridge validation"
```

- [ ] **Step 5: Run fresh exact-documentation-head Windows CI**

Require the workflow checkout SHA to equal the documentation commit SHA. Verify from fresh logs:

```text
Configure                PASS
Build                    PASS
CTest                    all tests PASS, 0 failed
ps2_pad_hle_service      PASS
r5900 guest-call test    PASS
existing syscall tests   PASS
Frame pacing telemetry   PASS
120 Hz pacing probe      PASS
Analyzer package         PASS
Pacing package           PASS
```

Only after this exact-head run is green may the milestone be called `CI_VALIDATED` in chat/status. Do not create another documentation edit merely to encode the self-referential final CI number; record the implementation-head CI evidence in the file and use the subsequent exact-doc-head run as the final external gate.

---

## Execution Notes

- Use TDD RED -> GREEN for every behavior-changing task.
- Each RED must fail for the expected missing behavior, not infrastructure.
- After every GREEN, review both spec compliance and code quality before advancing.
- Prefer production-only GREEN commits after test-only RED commits so review diffs remain small.
- Do not weaken `Ps2MemoryMap`, ELF validation, or dispatcher fault behavior to make PAD tests pass.
- Do not fabricate proprietary bytes or guest addresses.
