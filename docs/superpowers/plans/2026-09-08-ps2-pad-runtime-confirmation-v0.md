# PS2 PAD Runtime Confirmation v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add read-only R5900 call observation and PAD-specific runtime confirmation so evidence-backed `pad*` candidate PCs can be dynamically classified without activating HLE or changing guest execution.

**Architecture:** `R5900BlockDispatcher` emits immutable post-delay-slot snapshots for successful `JAL/JALR` blocks through a generic `IR5900CallObserver`. `Ps2PadRuntimeConfirmation` matches those snapshots only against PCs already present in `PadBindingDiscoveryResult`, validates the existing PAD ABI contract against const EE memory, aggregates bounded evidence, and exposes a deterministic `PAD_RUNTIME_CONFIRMATION_V0` report. No component in this milestone creates or mutates `Ps2PadHleBindings`.

**Tech Stack:** C++20, CMake 3.25+, Visual Studio 2022 x64, existing R5900 IR/x64 dispatcher, `Ps2MemoryMap`, `PadBindingDiscoveryResult`, CTest, GitHub Actions Windows CI.

**Spec:** `docs/superpowers/specs/2026-09-08-ps2-pad-runtime-confirmation-v0-design.md`

## Global Constraints

- Validated base SHA: `d7b9fc436805dc7e5908d277409eed208ded8f32`.
- Implementation branch: `feature/ps2-pad-runtime-confirmation-v0`, created from the final plan head.
- Strict RED→GREEN for each production unit.
- No hardcoded Burnout 3 guest PCs.
- No proprietary ELF/code/RAM bytes in repository tests or docs.
- No weakening of ELF/PT_LOAD validation.
- No production `WinMain` wiring in this milestone.
- No automatic analyzer-to-runtime transport in this milestone.
- No runtime-confirmation path may create or mutate active `Ps2PadHleBindings`.
- `src/runtime/ps2_pad_hle_service.h/.cpp` remain functionally unchanged.
- `IR5900CallObserver::observe()` is `noexcept`, returns `void`, and cannot stop dispatch.
- Observe only successful supported `JAL/JALR` blocks.
- Capture `$a0..$a3` after the delay slot.
- Use successful `native_execution.next_pc` as the actual target.
- Cold compile, ordinary cache hit, and fast-cache replay emit exactly one event per completed call block.
- Never emit for `J`, `JR`, branches, fallthrough, syscall, HLE interception itself, or failed/memory-faulted call blocks.
- Runtime ABI checks use low 32-bit arguments exactly like the current PAD HLE service.
- `padInit`: `a0 == 0`.
- `padPortOpen`: `a0 == 0`, `a1 == 0`, `a2 != 0`, `a2 % 64 == 0`, full 256-byte const EE RAM translation succeeds.
- `padGetState`: `a0 == 0`, `a1 == 0`.
- `padRead`: `a0 == 0`, `a1 == 0`, `a2 != 0`, full 32-byte const EE RAM translation succeeds.
- `padPortClose`: `a0 == 0`, `a1 == 0`.
- `padEnd`: no argument restriction in v0.
- Runtime counters saturate at `std::numeric_limits<std::size_t>::max()`.
- Static confidence and runtime confirmation remain separate dimensions.
- Static score never resolves a runtime tie.
- If one numeric PC is evidence for multiple PAD functions, evaluate the observation independently for each function-scoped `(function, pc)` record.
- Final completion requires fresh Windows CI on the exact final documentation head SHA.

## File Map

### Create
- `src/recompiler/r5900_call_observer.h`
- `src/analysis/ps2_pad_runtime_confirmation.h`
- `src/analysis/ps2_pad_runtime_confirmation.cpp`
- `src/analysis/ps2_pad_runtime_report.h`
- `src/analysis/ps2_pad_runtime_report.cpp`
- `tests/r5900_block_dispatcher_call_observer_windows_tests.cpp`
- `tests/ps2_pad_runtime_confirmation_tests.cpp`
- `tests/ps2_pad_runtime_report_tests.cpp`
- `tests/ps2_pad_runtime_confirmation_dispatcher_windows_tests.cpp`
- `docs/validation/2026-09-08-ps2-pad-runtime-confirmation-v0.md`

### Modify
- `src/recompiler/windows/r5900_block_dispatcher.h`
- `src/recompiler/windows/r5900_block_dispatcher.cpp`
- `CMakeLists.txt`
- `docs/PROGRESS.md`

### Must remain functionally unchanged
- `src/runtime/ps2_pad_hle_service.h`
- `src/runtime/ps2_pad_hle_service.cpp`
- `src/platform/windows/win_main.cpp`
- `src/recompiler/ps2_elf.h`
- `src/recompiler/ps2_elf.cpp`

---

## Task 1 — Generic R5900 Call Observer

**Files**
- Create: `src/recompiler/r5900_call_observer.h`
- Modify: `src/recompiler/windows/r5900_block_dispatcher.h`
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Create: `tests/r5900_block_dispatcher_call_observer_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Produces**

```cpp
namespace b3r::recompiler {

struct R5900CallObservation {
    std::uint32_t call_pc{};
    std::uint32_t target_pc{};
    std::uint32_t return_pc{};
    bool indirect{};
    std::array<std::uint64_t, 4> args{};
};

class IR5900CallObserver {
public:
    virtual ~IR5900CallObserver() = default;
    virtual void observe(const R5900CallObservation& observation) noexcept = 0;
};

} // namespace b3r::recompiler
```

`R5900BlockDispatcherOptions` gains:

```cpp
IR5900CallObserver* call_observer{};
```

`R5900BlockDispatcher` gains private metadata and one private helper:

```cpp
struct CachedCallMetadata {
    std::uint32_t call_pc{};
    std::uint32_t return_pc{};
    bool indirect{};
};

void observe_completed_call(
    const std::optional<CachedCallMetadata>& metadata,
    std::uint32_t target_pc,
    const R5900IrExecutionState& state) const noexcept;
```

`CachedBlock` gains:

```cpp
std::optional<CachedCallMetadata> call_metadata{};
```

- [ ] **1. Write RED test target and first direct-call test**

Add under the existing Windows test section:

```cmake
add_executable(r5900_block_dispatcher_call_observer_windows_tests
  tests/r5900_block_dispatcher_call_observer_windows_tests.cpp
)
target_link_libraries(r5900_block_dispatcher_call_observer_windows_tests PRIVATE
  b3r_recompiler_dispatcher_x64
)
add_test(NAME r5900_block_dispatcher_call_observer_windows_tests
  COMMAND r5900_block_dispatcher_call_observer_windows_tests)
```

The test file defines:

```cpp
class RecordingCallObserver final : public b3r::recompiler::IR5900CallObserver {
public:
    void observe(const b3r::recompiler::R5900CallObservation& value) noexcept override {
        observations.push_back(value);
    }

    std::vector<b3r::recompiler::R5900CallObservation> observations{};
};
```

Use a synthetic `JAL` whose delay slot is `ORI a3, zero, 0x55`. Initialize `$a0=0x11`, `$a1=0x22`, `$a2=0x33`, `$a3=0x44`; run one block and assert:

```cpp
CHECK(observer.observations.size() == 1u);
const auto& call = observer.observations.front();
CHECK(call.call_pc == caller_pc);
CHECK(call.target_pc == target_pc);
CHECK(call.return_pc == caller_pc + 8u);
CHECK(!call.indirect);
CHECK(call.args[0] == 0x11u);
CHECK(call.args[1] == 0x22u);
CHECK(call.args[2] == 0x33u);
CHECK(call.args[3] == 0x55u);
```

- [ ] **2. Run RED**

```powershell
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug --target r5900_block_dispatcher_call_observer_windows_tests
ctest --preset vs2022-debug -R r5900_block_dispatcher_call_observer_windows_tests
```

Expected: compile failure because `IR5900CallObserver` does not exist. Configure must pass.

Commit:

```bash
git add CMakeLists.txt tests/r5900_block_dispatcher_call_observer_windows_tests.cpp
git commit -m "test: define R5900 call observer contract"
```

- [ ] **3. Add observer interface, dispatcher option, metadata and private helper declaration**

Create `src/recompiler/r5900_call_observer.h` with the exact interface above. Include it from the dispatcher header; add `<optional>`.

- [ ] **4. Implement the one permitted emission path**

`R5900BlockDispatcher::observe_completed_call()` must return immediately when `options_.call_observer == nullptr` or metadata is empty. Otherwise:

```cpp
R5900CallObservation observation{};
observation.call_pc = metadata->call_pc;
observation.target_pc = target_pc;
observation.return_pc = metadata->return_pc;
observation.indirect = metadata->indirect;
observation.args = {
    state.gpr[4].low64,
    state.gpr[5].low64,
    state.gpr[6].low64,
    state.gpr[7].low64,
};
options_.call_observer->observe(observation);
```

Call this helper only after `native_execution.ok()` is true.

- [ ] **5. Populate call metadata during cache replacement creation**

```cpp
if (has_supported_jal) {
    replacement.call_metadata = CachedCallMetadata{
        transfer_site->pc,
        transfer_site->pc + 8u,
        false,
    };
} else if (has_supported_jalr) {
    replacement.call_metadata = CachedCallMetadata{
        transfer_site->pc,
        transfer_site->pc + 8u,
        true,
    };
}
```

`J`, `JR`, branches and fallthrough keep empty metadata.

After successful execution in the non-fast path, call `observe_completed_call()` with the metadata from the cache entry that actually executed and `native_execution.next_pc`.

- [ ] **6. Add fast-cache emission**

After successful fast-cache native execution and before advancing `current_pc`, call:

```cpp
observe_completed_call(
    fast_cached->second.call_metadata,
    native_execution.next_pc,
    state);
```

Do not emit on execution error or memory fault.

- [ ] **7. Complete Task 1 test matrix**

Add exact tests:

```text
JAL -> 1 event, indirect=false
JALR -> 1 event, indirect=true
J -> 0
JR -> 0
BEQ/BNE -> 0
fallthrough -> 0
SYSCALL -> 0
HLE intercept at target -> caller emits once; HLE interception emits none
memory-faulted call block -> 0
ordinary cache hit -> exactly one event per run
fast-cache hit -> exactly one event per run
```

For `JALR`, put the target in its source GPR and change that same GPR in the delay slot. Assert `target_pc` remains the pre-delay target and args reflect post-delay state.

Run observer-enabled and observer-null copies of the same fixture and compare:

```cpp
blocks_executed
instructions_executed
syscalls_handled
guest_calls_handled
cache_hits
fast_cache_hits
cache_misses
recompilations
```

- [ ] **8. Run GREEN and full CTest**

```powershell
cmake --build --preset vs2022-debug --target r5900_block_dispatcher_call_observer_windows_tests
ctest --preset vs2022-debug -R r5900_block_dispatcher_call_observer_windows_tests
ctest --preset vs2022-debug --output-on-failure
```

Commit:

```bash
git add src/recompiler/r5900_call_observer.h src/recompiler/windows/r5900_block_dispatcher.h src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_call_observer_windows_tests.cpp CMakeLists.txt
git commit -m "feat: observe completed R5900 guest calls"
```

- [ ] **9. Exact-head Windows CI and review**

Require all workflow gates green. Reject Task 1 if the diff contains duplicate observer emission logic or any new stop reason/counter.

---

## Task 2 — PAD ABI Classification and Bounded Aggregation

**Files**
- Create: `src/analysis/ps2_pad_runtime_confirmation.h`
- Create: `src/analysis/ps2_pad_runtime_confirmation.cpp`
- Create: `tests/ps2_pad_runtime_confirmation_tests.cpp`
- Modify: `CMakeLists.txt`

**Produces**

```cpp
namespace b3r::analysis {

enum class PadRuntimeConfirmationStatus : std::uint8_t {
    Unobserved,
    ObservedIncompatible,
    RuntimeConfirmed,
    RuntimeAmbiguous,
};

struct Ps2PadRuntimePcEvidence {
    PadBindingFunction function{};
    std::uint32_t guest_pc{};
    std::size_t calls_observed{};
    std::size_t compatible_calls{};
    std::size_t incompatible_calls{};
    std::optional<recompiler::R5900CallObservation> first_compatible{};
    std::optional<recompiler::R5900CallObservation> first_incompatible{};
};

struct PadRuntimeFunctionResult {
    PadBindingFunction function{};
    PadBindingConfidence static_confidence{PadBindingConfidence::Unresolved};
    PadRuntimeConfirmationStatus runtime_status{PadRuntimeConfirmationStatus::Unobserved};
    std::optional<std::uint32_t> guest_pc{};
    std::size_t calls_observed{};
    std::size_t compatible_calls{};
    std::size_t incompatible_calls{};
    std::vector<Ps2PadRuntimePcEvidence> pc_evidence{};
};

struct PadRuntimeConfirmationResult {
    std::array<PadRuntimeFunctionResult, 6> functions{};
};

namespace ps2_pad_runtime_confirmation_detail {
void saturating_increment(std::size_t& value) noexcept;
[[nodiscard]] std::size_t saturating_add(std::size_t lhs, std::size_t rhs) noexcept;
}

class Ps2PadRuntimeConfirmation final : public recompiler::IR5900CallObserver {
public:
    Ps2PadRuntimeConfirmation(
        const PadBindingDiscoveryResult& discovery,
        const runtime::Ps2MemoryMap& memory);

    void observe(const recompiler::R5900CallObservation& observation) noexcept override;
    [[nodiscard]] PadRuntimeConfirmationResult result() const;

private:
    // store normalized fixed function/PC state and const memory reference
};

} // namespace b3r::analysis
```

The `detail` functions are intentionally testable internal helpers; they are not guest-facing API.

- [ ] **1. Add source and RED test target**

Add `src/analysis/ps2_pad_runtime_confirmation.cpp` to `b3r_analysis` and:

```cmake
add_executable(ps2_pad_runtime_confirmation_tests
  tests/ps2_pad_runtime_confirmation_tests.cpp
)
target_link_libraries(ps2_pad_runtime_confirmation_tests PRIVATE b3r_analysis)
add_test(NAME ps2_pad_runtime_confirmation_tests COMMAND ps2_pad_runtime_confirmation_tests)
```

First RED: create synthetic `PadRead` evidence, emit one compatible observation, expect `RuntimeConfirmed` at that PC.

- [ ] **2. Run RED and commit**

```powershell
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug --target ps2_pad_runtime_confirmation_tests
ctest --preset vs2022-debug -R ps2_pad_runtime_confirmation_tests
```

Expected: missing confirmation header/implementation.

```bash
git add CMakeLists.txt tests/ps2_pad_runtime_confirmation_tests.cpp
git commit -m "test: define PAD runtime confirmation behavior"
```

- [ ] **3. Normalize discovery evidence**

For each canonical `PadBindingFunction` index 0..5:

1. copy `resolution.confidence`;
2. collect every `resolution.evidence[*].guest_pc` for that function;
3. sort ascending;
4. erase duplicate PCs;
5. create one PC evidence record per distinct PC.

Do not rely solely on `resolution.guest_pc`; ambiguous resolutions must retain all evidence PCs.

- [ ] **4. Implement exact ABI predicates with const memory translation**

```cpp
std::uint32_t arg32(const recompiler::R5900CallObservation& observation,
                    std::size_t index) noexcept {
    return static_cast<std::uint32_t>(observation.args[index]);
}
```

Predicates:

```text
PadInit:      a0 == 0
PadPortOpen:  a0 == 0 && a1 == 0 && a2 != 0 && a2 % 64 == 0 && translate(a2,256)
PadGetState:  a0 == 0 && a1 == 0
PadRead:      a0 == 0 && a1 == 0 && a2 != 0 && translate(a2,32)
PadPortClose: a0 == 0 && a1 == 0
PadEnd:       true
```

Use only the const `Ps2MemoryMap::translate()` overload.

- [ ] **5. Implement saturating helpers and aggregation**

```cpp
void saturating_increment(std::size_t& value) noexcept {
    if (value != std::numeric_limits<std::size_t>::max()) {
        ++value;
    }
}

std::size_t saturating_add(std::size_t lhs, std::size_t rhs) noexcept {
    const auto max = std::numeric_limits<std::size_t>::max();
    return rhs > max - lhs ? max : lhs + rhs;
}
```

For every function-scoped PC matching `observation.target_pc`:

- increment `calls_observed` saturating;
- classify ABI;
- increment compatible or incompatible counter saturating;
- set the corresponding first observation only when the optional is empty.

Ignore unrelated targets.

- [ ] **6. Implement `result()` runtime resolution**

For each function, saturating-sum all PC counters and count distinct PC records with `compatible_calls > 0`:

```text
0 compatible PCs + 0 observed -> Unobserved, pc=null
0 compatible PCs + observed   -> ObservedIncompatible, pc=null
1 compatible PC               -> RuntimeConfirmed, pc=that PC
2+ compatible PCs             -> RuntimeAmbiguous, pc=null
```

Never consult static `score`.

- [ ] **7. Complete ABI and saturation test matrix**

Test exactly:

```text
padInit a0=0 / a0=1
padPortOpen valid 0/0 aligned 256
padPortOpen bad port
padPortOpen bad slot
padPortOpen null a2
padPortOpen misaligned a2
padPortOpen only 255 bytes backed
padRead valid 0/0 full 32
padRead null a2
padRead only 31 bytes backed
padGetState valid and bad port/slot
padPortClose valid and bad port/slot
padEnd arbitrary a0..a3
unrelated target ignored
first compatible retained
first incompatible retained
duplicate `(function,pc)` static evidence deduplicated
same numeric PC under two functions evaluated independently
```

Directly test detail helpers:

```cpp
std::size_t value = std::numeric_limits<std::size_t>::max();
ps2_pad_runtime_confirmation_detail::saturating_increment(value);
CHECK(value == std::numeric_limits<std::size_t>::max());

const auto saturated = ps2_pad_runtime_confirmation_detail::saturating_add(
    std::numeric_limits<std::size_t>::max() - 1u,
    2u);
CHECK(saturated == std::numeric_limits<std::size_t>::max());
```

- [ ] **8. Run GREEN, full CTest, commit and exact-head CI**

```powershell
cmake --build --preset vs2022-debug --target ps2_pad_runtime_confirmation_tests
ctest --preset vs2022-debug -R ps2_pad_runtime_confirmation_tests
ctest --preset vs2022-debug --output-on-failure
```

```bash
git add src/analysis/ps2_pad_runtime_confirmation.h src/analysis/ps2_pad_runtime_confirmation.cpp tests/ps2_pad_runtime_confirmation_tests.cpp CMakeLists.txt
git commit -m "feat: classify PAD runtime call evidence"
```

Require all Windows CI gates green and verify no diff in PAD HLE, WinMain, or ELF loader files.

---

## Task 3 — Deterministic Runtime Report

**Files**
- Create: `src/analysis/ps2_pad_runtime_report.h`
- Create: `src/analysis/ps2_pad_runtime_report.cpp`
- Create: `tests/ps2_pad_runtime_report_tests.cpp`
- Extend: `tests/ps2_pad_runtime_confirmation_tests.cpp`
- Modify: `CMakeLists.txt`

**Produces**

```cpp
[[nodiscard]] std::string format_ps2_pad_runtime_confirmation(
    const PadRuntimeConfirmationResult& result);
```

- [ ] **1. Write RED report target and dynamic-resolution cases**

Add report source to `b3r_analysis` and:

```cmake
add_executable(ps2_pad_runtime_report_tests
  tests/ps2_pad_runtime_report_tests.cpp
)
target_link_libraries(ps2_pad_runtime_report_tests PRIVATE b3r_analysis)
add_test(NAME ps2_pad_runtime_report_tests COMMAND ps2_pad_runtime_report_tests)
```

Extend confirmation tests with:

```text
no observations -> Unobserved
incompatible only -> ObservedIncompatible
one compatible PC -> RuntimeConfirmed
many compatible calls to one PC -> RuntimeConfirmed
one compatible PC + incompatible other PC -> RuntimeConfirmed
2 compatible PCs -> RuntimeAmbiguous
Trusted + no call -> Trusted + Unobserved
Candidate + compatible call -> Candidate + RuntimeConfirmed
ambiguous static candidates + one compatible runtime PC -> RuntimeConfirmed
ambiguous static candidates + two compatible runtime PCs -> RuntimeAmbiguous
```

First report expectation begins exactly with `PAD_RUNTIME_CONFIRMATION_V0\n`.

- [ ] **2. Run RED and commit**

```powershell
cmake --build --preset vs2022-debug --target ps2_pad_runtime_report_tests
ctest --preset vs2022-debug -R "ps2_pad_runtime_(confirmation|report)_tests"
```

```bash
git add CMakeLists.txt tests/ps2_pad_runtime_confirmation_tests.cpp tests/ps2_pad_runtime_report_tests.cpp
git commit -m "test: define PAD runtime confirmation report"
```

- [ ] **3. Implement canonical formatter mappings**

Function names:

```text
PadInit -> padInit
PadPortOpen -> padPortOpen
PadGetState -> padGetState
PadRead -> padRead
PadPortClose -> padPortClose
PadEnd -> padEnd
```

Static confidence:

```text
Unresolved -> unresolved
Candidate -> candidate
Trusted -> trusted
```

Runtime status:

```text
Unobserved -> unobserved
ObservedIncompatible -> observed_incompatible
RuntimeConfirmed -> runtime_confirmed
RuntimeAmbiguous -> runtime_ambiguous
```

PC formatting:

```cpp
out << "0x" << std::hex << std::nouppercase
    << std::setw(8) << std::setfill('0') << pc;
out << std::dec;
```

- [ ] **4. Implement exact report shape**

Header:

```text
PAD_RUNTIME_CONFIRMATION_V0
```

Then six summary lines in canonical function order:

```text
PAD_RUNTIME function=padInit static_confidence=unresolved runtime_status=unobserved pc=none observed=0 compatible=0 incompatible=0
```

For `RuntimeConfirmed`, print the unique selected `pc=0x........`; all other statuses print `pc=none`.

After each summary, print every evidence-backed PC record, including zero-observation records, ascending by PC:

```text
PAD_RUNTIME_PC function=padRead pc=0x00124500 observed=15 compatible=15 incompatible=0
```

Do not print stored first-observation args, memory bytes, static scores, or HLE activation fields.

- [ ] **5. Prove byte determinism**

Tests compare exact complete strings and verify:

```text
same result formatted twice -> byte-identical
canonical six-function order
ascending per-PC order
lowercase 8-digit hex
counts decimal
RuntimeConfirmed gets one PC
all other statuses get pc=none
no score=
no args=
no bytes=
no hle=
```

- [ ] **6. Run GREEN, full CTest, commit and exact-head CI**

```powershell
cmake --build --preset vs2022-debug --target ps2_pad_runtime_confirmation_tests ps2_pad_runtime_report_tests
ctest --preset vs2022-debug -R "ps2_pad_runtime_(confirmation|report)_tests"
ctest --preset vs2022-debug --output-on-failure
```

```bash
git add src/analysis/ps2_pad_runtime_report.h src/analysis/ps2_pad_runtime_report.cpp tests/ps2_pad_runtime_confirmation_tests.cpp tests/ps2_pad_runtime_report_tests.cpp CMakeLists.txt
git commit -m "feat: report PAD runtime confirmation"
```

Require all Windows CI gates green.

---

## Task 4 — Synthetic Dispatcher→PAD Confirmation Integration

**Files**
- Create: `tests/ps2_pad_runtime_confirmation_dispatcher_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Consumes**
- `R5900BlockDispatcher`
- `Ps2PadRuntimeConfirmation`
- synthetic `PadBindingDiscoveryResult`
- synthetic `Ps2MemoryMap`

- [ ] **1. Register integration test target**

```cmake
add_executable(ps2_pad_runtime_confirmation_dispatcher_windows_tests
  tests/ps2_pad_runtime_confirmation_dispatcher_windows_tests.cpp
)
target_link_libraries(ps2_pad_runtime_confirmation_dispatcher_windows_tests PRIVATE
  b3r_recompiler_dispatcher_x64
  b3r_analysis
)
add_test(NAME ps2_pad_runtime_confirmation_dispatcher_windows_tests
  COMMAND ps2_pad_runtime_confirmation_dispatcher_windows_tests)
```

- [ ] **2. Add end-to-end synthetic `padRead` confirmation test**

Construct a synthetic executable memory image containing caller `JAL -> pad_read_pc`. Discovery contains:

```cpp
PadBindingEvidence{
    PadBindingFunction::PadRead,
    PadBindingEvidenceKind::StaticFingerprint,
    pad_read_pc,
    100u,
    "synthetic-padRead",
};
```

Set `$a0=0`, `$a1=0`, `$a2=valid_32_byte_guest_buffer`, install `Ps2PadRuntimeConfirmation` as `R5900BlockDispatcherOptions::call_observer`, leave `guest_calls=nullptr`, and run exactly one caller block.

Assert:

```cpp
const auto result = confirmation.result();
const auto& pad_read = result.functions[
    static_cast<std::size_t>(PadBindingFunction::PadRead)];
CHECK(pad_read.runtime_status == PadRuntimeConfirmationStatus::RuntimeConfirmed);
CHECK(pad_read.guest_pc == pad_read_pc);
CHECK(dispatch.blocks_executed == 1u);
CHECK(dispatch.guest_calls_handled == 0u);
```

Snapshot the 32-byte guest buffer before dispatch and verify byte-for-byte unchanged afterward.

- [ ] **3. Add negative/cache integration cases**

```text
same target + invalid a2 -> ObservedIncompatible
unrelated JAL target -> Unobserved
second cached caller run -> count +1 exactly
fast-cache replay -> count +1 exactly
unrelated guest-call service at another PC -> no effect
```

This task is an integration characterization of units already RED→GREEN validated in Tasks 1–3. Do not introduce production changes if it already passes.

- [ ] **4. Run integration, full CTest, commit and exact-head CI**

```powershell
cmake --build --preset vs2022-debug --target ps2_pad_runtime_confirmation_dispatcher_windows_tests
ctest --preset vs2022-debug -R ps2_pad_runtime_confirmation_dispatcher_windows_tests
ctest --preset vs2022-debug --output-on-failure
```

```bash
git add tests/ps2_pad_runtime_confirmation_dispatcher_windows_tests.cpp CMakeLists.txt
git commit -m "test: integrate PAD runtime confirmation with dispatcher"
```

Require all Windows CI gates green.

Perform two reviews before docs:

**Spec compliance**
```text
successful JAL/JALR only
post-delay args
actual returned target
all static evidence PCs considered
ABI exact
runtime tie never score-resolved
no HLE activation
no proprietary data
```

**Code quality**
```text
one emission helper
no unbounded trace
const memory validation
saturating counters
canonical ordering
PAD confirmation/report remain host-independent
```

---

## Task 5 — Validation Ledger and Exact Final CI

**Files**
- Create: `docs/validation/2026-09-08-ps2-pad-runtime-confirmation-v0.md`
- Modify: `docs/PROGRESS.md`

- [ ] **1. Write validation ledger**

Start with:

```text
Milestone: PS2 PAD Runtime Confirmation v0
Status: PENDING_FINAL_EXACT_HEAD_CI
Base: d7b9fc436805dc7e5908d277409eed208ded8f32
Implementation branch: feature/ps2-pad-runtime-confirmation-v0
```

For each RED/GREEN gate record exact SHA, run ID, failing reason for RED, and passing test count for GREEN.

State explicitly:

```text
No Burnout 3 guest PCs were hardcoded.
No proprietary ELF/code/RAM bytes were committed.
Runtime confirmation does not create or mutate Ps2PadHleBindings.
Ps2PadHleService behavior is unchanged.
WinMain production wiring is outside this milestone.
Real Burnout 3 confirmation remains PENDING_EXTERNAL_VALIDATION until a complete lawful ELF reaches evidence-backed calls.
```

- [ ] **2. Update progress conservatively**

Use:

```text
PS2 PAD Binding Discovery v0      CI_VALIDATED
PS2 PAD Runtime Confirmation v0   PENDING_FINAL_EXACT_HEAD_CI
PS2 PAD Runtime Activation        NOT_STARTED
```

Do not claim real game input yet.

- [ ] **3. Commit docs atomically**

```bash
git add docs/PROGRESS.md docs/validation/2026-09-08-ps2-pad-runtime-confirmation-v0.md
git commit -m "docs: record PS2 PAD runtime confirmation validation"
```

- [ ] **4. Run Windows CI on that exact docs head**

Require green:

```text
Configure
Build
full CTest
Frame pacing telemetry
120 Hz pacing probe
analyzer package validation
pacing package validation
```

Record run ID, job ID, exact SHA, CTest passed/total count, pacing telemetry result, probe result, and both package validations.

- [ ] **5. Mark validated in a status-only docs commit**

After the previous exact docs head is fully green, change only the ledger and `docs/PROGRESS.md` from `PENDING_FINAL_EXACT_HEAD_CI` to `CI_VALIDATED`, include the successful run identifiers, and commit:

```bash
git add docs/PROGRESS.md docs/validation/2026-09-08-ps2-pad-runtime-confirmation-v0.md
git commit -m "docs: mark PS2 PAD runtime confirmation CI validated"
```

- [ ] **6. Run Windows CI again on the exact final status SHA**

Only after this second docs-head run is fully green may the milestone be reported `CI_VALIDATED`.

- [ ] **7. Final integrity audit**

Expected production changes:

```text
src/recompiler/r5900_call_observer.h
src/recompiler/windows/r5900_block_dispatcher.h
src/recompiler/windows/r5900_block_dispatcher.cpp
src/analysis/ps2_pad_runtime_confirmation.h
src/analysis/ps2_pad_runtime_confirmation.cpp
src/analysis/ps2_pad_runtime_report.h
src/analysis/ps2_pad_runtime_report.cpp
CMakeLists.txt
```

Expected tests:

```text
tests/r5900_block_dispatcher_call_observer_windows_tests.cpp
tests/ps2_pad_runtime_confirmation_tests.cpp
tests/ps2_pad_runtime_report_tests.cpp
tests/ps2_pad_runtime_confirmation_dispatcher_windows_tests.cpp
```

Expected docs:

```text
docs/PROGRESS.md
docs/validation/2026-09-08-ps2-pad-runtime-confirmation-v0.md
```

Verify no changes to `ps2_pad_hle_service.*`, `win_main.cpp`, or `ps2_elf.*`. Real Burnout 3 runtime confirmation remains external validation pending.