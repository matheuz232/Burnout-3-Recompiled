# PS2 PAD Runtime Confirmation v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add read-only R5900 call observation and PAD-specific runtime confirmation so evidence-backed `pad*` candidate PCs can be dynamically classified without activating HLE or changing guest execution.

**Architecture:** `R5900BlockDispatcher` emits immutable post-delay-slot snapshots for successful `JAL/JALR` blocks through a generic `IR5900CallObserver`. `Ps2PadRuntimeConfirmation` consumes those snapshots, matches only PCs already present in `PadBindingDiscoveryResult`, validates the existing PAD ABI contract against read-only EE memory, aggregates bounded evidence, and exposes a deterministic `PAD_RUNTIME_CONFIRMATION_V0` report. No component in this milestone creates or mutates `Ps2PadHleBindings`.

**Tech Stack:** C++20, CMake 3.25+, Visual Studio 2022 x64, existing R5900 IR/x64 dispatcher, `Ps2MemoryMap`, `PadBindingDiscoveryResult`, CTest, GitHub Actions Windows CI.

**Spec:** `docs/superpowers/specs/2026-09-08-ps2-pad-runtime-confirmation-v0-design.md`

## Global Constraints

- Base validated milestone SHA: `d7b9fc436805dc7e5908d277409eed208ded8f32`.
- Implementation starts from the final plan commit, on `feature/ps2-pad-runtime-confirmation-v0`.
- Strict TDD: every production behavior begins with a RED commit/run whose failure is attributable to the missing behavior, then a minimal GREEN commit/run.
- No hardcoded Burnout 3 guest PCs.
- No proprietary ELF/code/RAM bytes in repository tests or docs.
- No weakening of `Ps2ElfImage` or PT_LOAD validation.
- No production `WinMain` wiring in this milestone.
- No automatic analyzer-to-runtime transport in this milestone.
- No code path may create or mutate active `Ps2PadHleBindings` from runtime confirmation.
- `src/runtime/ps2_pad_hle_service.cpp` and `.h` must remain functionally unchanged.
- `IR5900CallObserver::observe()` is read-only, `noexcept`, returns `void`, and cannot stop dispatch.
- Call observations are emitted only after a supported `JAL/JALR` block completes successfully.
- Call arguments are the low 64-bit values of `$a0..$a3` after the architectural delay slot executes.
- `target_pc` is the successful native block's actual `native_execution.next_pc`, never a target re-derived from mutable post-delay-slot GPRs.
- Cold compile, ordinary cache hit, and fast-cache replay each emit exactly one observation for a completed call block.
- No observation for `J`, `JR`, branches, fallthrough, syscall, HLE interception itself, or failed call blocks.
- Runtime ABI compatibility uses the low 32 bits of arguments exactly as the existing `Ps2PadHleService` does.
- `padPortOpen`: `a0=0`, `a1=0`, `a2!=0`, 64-byte alignment, full 256-byte EE RAM backing.
- `padRead`: `a0=0`, `a1=0`, `a2!=0`, full 32-byte EE RAM backing.
- `padGetState` and `padPortClose`: `a0=0`, `a1=0`.
- `padInit`: `a0=0`.
- `padEnd`: no v0 argument restriction.
- Counters saturate at `std::numeric_limits<std::size_t>::max()` and never wrap.
- Static confidence and runtime confirmation remain independent dimensions; static score never breaks a runtime tie.
- If the same guest PC appears as evidence for more than one `PadBindingFunction`, the observation is evaluated independently for each function-scoped `(function, pc)` evidence record; this is diagnostic only and cannot activate HLE.
- Windows CI must preserve Configure, Build, full CTest, frame pacing telemetry, 120 Hz probe, analyzer package validation, and pacing package validation.
- Final completion requires a fresh successful Windows CI run on the exact final documentation head SHA.

## File Structure

### Create

- `src/recompiler/r5900_call_observer.h` — generic immutable R5900 call-observer contract.
- `src/analysis/ps2_pad_runtime_confirmation.h` — PAD runtime-confirmation public types and observer class.
- `src/analysis/ps2_pad_runtime_confirmation.cpp` — evidence normalization, ABI classification, saturating aggregation, runtime resolution.
- `src/analysis/ps2_pad_runtime_report.h` — deterministic formatter declaration.
- `src/analysis/ps2_pad_runtime_report.cpp` — `PAD_RUNTIME_CONFIRMATION_V0` formatter.
- `tests/r5900_block_dispatcher_call_observer_windows_tests.cpp` — dispatcher call-observer semantics on Windows x64.
- `tests/ps2_pad_runtime_confirmation_tests.cpp` — portable PAD ABI and runtime-resolution tests.
- `tests/ps2_pad_runtime_report_tests.cpp` — portable deterministic report tests.
- `tests/ps2_pad_runtime_confirmation_dispatcher_windows_tests.cpp` — synthetic end-to-end dispatcher→observer→PAD confirmation integration.
- `docs/validation/2026-09-08-ps2-pad-runtime-confirmation-v0.md` — milestone validation ledger.

### Modify

- `src/recompiler/windows/r5900_block_dispatcher.h` — observer option and cached call metadata.
- `src/recompiler/windows/r5900_block_dispatcher.cpp` — post-success call observation for cold/cache/fast-cache paths.
- `CMakeLists.txt` — add analysis sources and four test targets.
- `docs/PROGRESS.md` — milestone status and final exact-head CI evidence.

### Must not functionally modify

- `src/runtime/ps2_pad_hle_service.h`
- `src/runtime/ps2_pad_hle_service.cpp`
- `src/platform/windows/win_main.cpp`
- `src/recompiler/ps2_elf.h`
- `src/recompiler/ps2_elf.cpp`

---

### Task 1: Generic R5900 Call Observer

**Files:**
- Create: `src/recompiler/r5900_call_observer.h`
- Modify: `src/recompiler/windows/r5900_block_dispatcher.h`
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Create: `tests/r5900_block_dispatcher_call_observer_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: existing `R5900IrExecutionState`, direct/indirect call terminators, dispatcher cache, x64 `native_execution.next_pc`.
- Produces:

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

`R5900BlockDispatcher::CachedBlock` gains:

```cpp
struct CachedCallMetadata {
    std::uint32_t call_pc{};
    std::uint32_t return_pc{};
    bool indirect{};
};

std::optional<CachedCallMetadata> call_metadata{};
```

- [ ] **Step 1: Add the dedicated call-observer test target and write the first RED tests**

Add under the existing `if(WIN32)` test section:

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

The first test file must define a recording observer and prove a direct call captures post-delay-slot state:

```cpp
class RecordingCallObserver final : public b3r::recompiler::IR5900CallObserver {
public:
    void observe(const b3r::recompiler::R5900CallObservation& value) noexcept override {
        observations.push_back(value);
    }

    std::vector<b3r::recompiler::R5900CallObservation> observations{};
};
```

Use a synthetic `JAL` block whose delay slot changes `$a3`. Initialize `$a0=0x11`, `$a1=0x22`, `$a2=0x33`, `$a3=0x44`; encode delay-slot `ORI a3, zero, 0x55`. Run exactly one block and assert:

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

Also add a baseline test that runs the same block without an observer and compares `R5900DispatchResult` and relevant GPRs to the pre-observer behavior.

- [ ] **Step 2: Run the RED**

On Windows/VS2022:

```powershell
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug --target r5900_block_dispatcher_call_observer_windows_tests
ctest --preset vs2022-debug -R r5900_block_dispatcher_call_observer_windows_tests
```

Expected RED: compile failure because `recompiler/r5900_call_observer.h` and/or `IR5900CallObserver` does not exist yet. Configure must otherwise succeed.

Commit the RED:

```bash
git add CMakeLists.txt tests/r5900_block_dispatcher_call_observer_windows_tests.cpp
git commit -m "test: define R5900 call observer contract"
```

- [ ] **Step 3: Implement the generic observer header and dispatcher option**

Create `src/recompiler/r5900_call_observer.h` with the exact public interface above and required `<array>` / `<cstdint>` includes.

Modify `r5900_block_dispatcher.h` to include the observer header and `<optional>`, add `call_observer` to options, define `CachedCallMetadata` privately, and add `std::optional<CachedCallMetadata> call_metadata` to `CachedBlock`.

Do not add a dispatcher stop reason or counter for observations.

- [ ] **Step 4: Implement one shared successful-call emission helper**

In `r5900_block_dispatcher.cpp`, add a helper with this behavior:

```cpp
void observe_completed_call(
    IR5900CallObserver* observer,
    const std::optional<R5900BlockDispatcher::CachedCallMetadata>& metadata,
    std::uint32_t target_pc,
    const R5900IrExecutionState& state) noexcept;
```

If private nested type visibility makes this free function illegal, define an equivalent private static/member helper or a local metadata struct in the `.cpp`; do not make cache internals public merely for the helper.

The emitted snapshot must be exactly:

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
observer->observe(observation);
```

Call the helper only after `native_execution.ok()` is known true.

- [ ] **Step 5: Populate call metadata during cold compilation**

When building a replacement cache entry:

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

`J` and `JR` must leave `call_metadata` empty.

After successful execution in the non-fast path, use the metadata from the cache entry that actually executed, not stale analysis-local state. This covers cold insert, exact cache hit, and recompilation replacement.

- [ ] **Step 6: Cover the fast-cache path**

Immediately after a successful fast-cache native execution and before returning/continuing, call the same observation helper with `fast_cached->second.call_metadata` and `native_execution.next_pc`.

Do not emit if native execution fails or reports a memory fault.

- [ ] **Step 7: Expand Task 1 tests before closing GREEN**

Add exact tests for:

```text
JAL        -> one observation, indirect=false
JALR       -> one observation, indirect=true
J          -> zero observations
JR         -> zero observations
BEQ/BNE    -> zero observations
fallthrough-> zero observations
SYSCALL    -> zero observations
HLE intercept at target -> call block observed once; intercept itself adds no second event
memory-faulted call block -> zero observations
ordinary cache hit -> one event per run
fast-cache hit -> one event per run
```

For `JALR`, set the source target GPR before dispatch, then alter that same GPR in the delay slot. Assert `target_pc` equals the pre-delay captured target while `$a0..$a3` reflect post-delay values.

For counter integrity, compare observer-on vs observer-null runs and assert equal values for:

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

- [ ] **Step 8: Run Task 1 GREEN and full regression**

```powershell
cmake --build --preset vs2022-debug --target r5900_block_dispatcher_call_observer_windows_tests
ctest --preset vs2022-debug -R r5900_block_dispatcher_call_observer_windows_tests
ctest --preset vs2022-debug --output-on-failure
```

Expected: dedicated observer tests PASS and full CTest PASS.

Commit production GREEN:

```bash
git add src/recompiler/r5900_call_observer.h src/recompiler/windows/r5900_block_dispatcher.h src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_call_observer_windows_tests.cpp CMakeLists.txt
git commit -m "feat: observe completed R5900 guest calls"
```

- [ ] **Step 9: Run Windows CI on the exact Task 1 head and review**

Require Configure, Build, CTest, pacing telemetry, pacing probe, analyzer package validation, and pacing package validation all green. Review the diff specifically for duplicate observation paths and accidental changes to non-call dispatch behavior before proceeding.

---

### Task 2: PAD Runtime ABI Classification and Bounded Aggregation

**Files:**
- Create: `src/analysis/ps2_pad_runtime_confirmation.h`
- Create: `src/analysis/ps2_pad_runtime_confirmation.cpp`
- Create: `tests/ps2_pad_runtime_confirmation_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `PadBindingDiscoveryResult`, `PadBindingFunction`, `PadBindingConfidence`, `R5900CallObservation`, `const runtime::Ps2MemoryMap&`.
- Produces:

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

class Ps2PadRuntimeConfirmation final : public recompiler::IR5900CallObserver {
public:
    Ps2PadRuntimeConfirmation(
        const PadBindingDiscoveryResult& discovery,
        const runtime::Ps2MemoryMap& memory);

    void observe(const recompiler::R5900CallObservation& observation) noexcept override;

    [[nodiscard]] PadRuntimeConfirmationResult result() const;

private:
    // Internal normalized function/PC state only; no unbounded trace.
};

} // namespace b3r::analysis
```

The constructor must copy/normalize only the static confidence and distinct evidence PCs it needs. It must not retain a mutable reference to `PadBindingDiscoveryResult`.

- [ ] **Step 1: Register the source and dedicated portable test target, then write RED tests**

Add `src/analysis/ps2_pad_runtime_confirmation.cpp` to `b3r_analysis`.

Add:

```cmake
add_executable(ps2_pad_runtime_confirmation_tests
  tests/ps2_pad_runtime_confirmation_tests.cpp
)
target_link_libraries(ps2_pad_runtime_confirmation_tests PRIVATE b3r_analysis)
add_test(NAME ps2_pad_runtime_confirmation_tests
  COMMAND ps2_pad_runtime_confirmation_tests)
```

The initial RED must create a synthetic discovery result with a `PadRead` evidence PC and assert that a compatible call becomes `RuntimeConfirmed`.

Build a test memory map from a synthetic ELF segment large enough to back the requested destination. Do not inject proprietary bytes.

- [ ] **Step 2: Run and commit the RED**

```powershell
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug --target ps2_pad_runtime_confirmation_tests
ctest --preset vs2022-debug -R ps2_pad_runtime_confirmation_tests
```

Expected RED: missing `analysis/ps2_pad_runtime_confirmation.h` / implementation.

```bash
git add CMakeLists.txt tests/ps2_pad_runtime_confirmation_tests.cpp
git commit -m "test: define PAD runtime confirmation behavior"
```

- [ ] **Step 3: Normalize function-scoped evidence PCs deterministically**

At construction, for each of the six canonical `PadBindingFunction` values:

1. copy `resolution.confidence` into the function state;
2. collect every `resolution.evidence[*].guest_pc` for that function;
3. sort ascending;
4. erase duplicate PCs;
5. create one `Ps2PadRuntimePcEvidence` per distinct PC.

Do not use `resolution.guest_pc` as the sole observable set; ambiguous resolutions with `guest_pc=null` still need every evidence PC.

If the same numeric PC occurs under two different functions, retain one record in each function. A single observation may therefore update both function-scoped records independently if each ABI predicate matches.

- [ ] **Step 4: Implement exact ABI predicates using low 32-bit arguments**

Use:

```cpp
std::uint32_t arg32(const recompiler::R5900CallObservation& observation,
                    std::size_t index) noexcept {
    return static_cast<std::uint32_t>(observation.args[index]);
}
```

Implement compatibility exactly:

```cpp
padInit:
    arg32(a0) == 0

padPortOpen:
    a0 == 0 && a1 == 0 && a2 != 0 &&
    (a2 % 64u) == 0 &&
    memory.translate(a2, 256u).has_value()

padGetState:
    a0 == 0 && a1 == 0

padRead:
    a0 == 0 && a1 == 0 && a2 != 0 &&
    memory.translate(a2, 32u).has_value()

padPortClose:
    a0 == 0 && a1 == 0

padEnd:
    true
```

Use the `const Ps2MemoryMap::translate()` overload. Never write RAM during classification.

- [ ] **Step 5: Implement bounded `observe()` aggregation**

For each function state whose evidence PC equals `observation.target_pc`:

1. saturating-increment `calls_observed`;
2. evaluate that function's ABI predicate;
3. on compatible, saturating-increment `compatible_calls` and set `first_compatible` only if empty;
4. on incompatible, saturating-increment `incompatible_calls` and set `first_incompatible` only if empty.

Use this helper:

```cpp
void saturating_increment(std::size_t& value) noexcept {
    if (value != std::numeric_limits<std::size_t>::max()) {
        ++value;
    }
}
```

An observation whose target matches no PAD evidence PC returns immediately and changes no state.

- [ ] **Step 6: Implement `result()` dynamic resolution and saturating totals**

For each function:

- aggregate `calls_observed`, `compatible_calls`, `incompatible_calls` across PC records with saturating addition;
- count distinct PC records with `compatible_calls > 0`;
- zero compatible PCs + zero observed calls => `Unobserved`, `guest_pc=null`;
- zero compatible PCs + any observed call => `ObservedIncompatible`, `guest_pc=null`;
- exactly one compatible PC => `RuntimeConfirmed`, `guest_pc=that PC`;
- two or more compatible PCs => `RuntimeAmbiguous`, `guest_pc=null`.

Use saturating addition:

```cpp
std::size_t saturating_add(std::size_t lhs, std::size_t rhs) noexcept {
    const auto max = std::numeric_limits<std::size_t>::max();
    return rhs > max - lhs ? max : lhs + rhs;
}
```

Never consult evidence `score` while deciding runtime status.

- [ ] **Step 7: Expand Task 2 test matrix**

Add individual tests for all six functions and these exact boundary conditions:

```text
padInit a0=0                                  -> compatible
padInit a0=1                                  -> incompatible
padPortOpen port=0 slot=0 aligned full 256   -> compatible
padPortOpen port=1                            -> incompatible
padPortOpen slot=1                            -> incompatible
padPortOpen a2=0                              -> incompatible
padPortOpen a2 misaligned                     -> incompatible
padPortOpen only 255 bytes backed             -> incompatible
padRead port=0 slot=0 full 32                 -> compatible
padRead a2=0                                  -> incompatible
padRead only 31 bytes backed                  -> incompatible
padGetState 0/0                               -> compatible
padGetState nonzero port/slot                 -> incompatible
padPortClose 0/0                              -> compatible
padPortClose nonzero port/slot                -> incompatible
padEnd arbitrary a0..a3                       -> compatible
unrelated target                              -> ignored
```

Also prove:

- `first_compatible` is not replaced by later compatible calls;
- `first_incompatible` is not replaced by later incompatible calls;
- duplicate static evidence for the same `(function, pc)` produces one PC record;
- same numeric PC under two functions is evaluated independently;
- counters saturate without wrap by exposing a test-only construction/helper only if required; do not add a production mutation API solely for tests. Prefer unit-testing private-free saturating helpers in the `.cpp` through a small internal header only if direct overflow setup is otherwise impossible.

- [ ] **Step 8: Run Task 2 GREEN and full portable regression**

```powershell
cmake --build --preset vs2022-debug --target ps2_pad_runtime_confirmation_tests
ctest --preset vs2022-debug -R ps2_pad_runtime_confirmation_tests
ctest --preset vs2022-debug --output-on-failure
```

Expected: all PASS.

```bash
git add src/analysis/ps2_pad_runtime_confirmation.h src/analysis/ps2_pad_runtime_confirmation.cpp tests/ps2_pad_runtime_confirmation_tests.cpp CMakeLists.txt
git commit -m "feat: classify PAD runtime call evidence"
```

- [ ] **Step 9: Run Windows CI on the exact Task 2 head and review**

Require all workflow gates green. Review specifically that `Ps2PadHleService`, `WinMain`, and ELF loader files are untouched.

---

### Task 3: Dynamic Resolution Report

**Files:**
- Create: `src/analysis/ps2_pad_runtime_report.h`
- Create: `src/analysis/ps2_pad_runtime_report.cpp`
- Modify: `src/analysis/ps2_pad_runtime_confirmation.h` only if a formatter-required accessor/type field was omitted in Task 2; do not change confirmation semantics.
- Create: `tests/ps2_pad_runtime_report_tests.cpp`
- Extend: `tests/ps2_pad_runtime_confirmation_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `PadRuntimeConfirmationResult`.
- Produces:

```cpp
namespace b3r::analysis {

[[nodiscard]] std::string format_ps2_pad_runtime_confirmation(
    const PadRuntimeConfirmationResult& result);

} // namespace b3r::analysis
```

- [ ] **Step 1: Write RED resolution cases and report target**

Add `src/analysis/ps2_pad_runtime_report.cpp` to `b3r_analysis` and register:

```cmake
add_executable(ps2_pad_runtime_report_tests
  tests/ps2_pad_runtime_report_tests.cpp
)
target_link_libraries(ps2_pad_runtime_report_tests PRIVATE b3r_analysis)
add_test(NAME ps2_pad_runtime_report_tests COMMAND ps2_pad_runtime_report_tests)
```

Extend confirmation tests with:

```text
no observations                            -> Unobserved
one/many incompatible calls, no compatible -> ObservedIncompatible
one compatible PC                          -> RuntimeConfirmed
many compatible calls to same PC           -> RuntimeConfirmed
one compatible + one incompatible other PC -> RuntimeConfirmed for compatible PC
two distinct compatible PCs                -> RuntimeAmbiguous
Trusted static PC, no runtime call          -> Trusted + Unobserved
Candidate static PC, one compatible call    -> Candidate + RuntimeConfirmed
multiple static fingerprints, one runtime-compatible PC -> RuntimeConfirmed
multiple static fingerprints, two runtime-compatible PCs -> RuntimeAmbiguous
```

Create the first report test expecting an exact string that begins:

```text
PAD_RUNTIME_CONFIRMATION_V0
```

Expected RED: missing formatter header/implementation.

- [ ] **Step 2: Run and commit RED**

```powershell
cmake --build --preset vs2022-debug --target ps2_pad_runtime_report_tests
ctest --preset vs2022-debug -R "ps2_pad_runtime_(confirmation|report)_tests"
```

```bash
git add CMakeLists.txt tests/ps2_pad_runtime_confirmation_tests.cpp tests/ps2_pad_runtime_report_tests.cpp
git commit -m "test: define PAD runtime confirmation report"
```

- [ ] **Step 3: Implement canonical names and status formatting**

`ps2_pad_runtime_report.cpp` must map functions exactly:

```text
PadInit      -> padInit
PadPortOpen  -> padPortOpen
PadGetState  -> padGetState
PadRead      -> padRead
PadPortClose -> padPortClose
PadEnd       -> padEnd
```

Static confidence strings:

```text
Unresolved -> unresolved
Candidate  -> candidate
Trusted    -> trusted
```

Runtime status strings:

```text
Unobserved           -> unobserved
ObservedIncompatible -> observed_incompatible
RuntimeConfirmed     -> runtime_confirmed
RuntimeAmbiguous     -> runtime_ambiguous
```

Format PCs with lowercase hexadecimal and exactly eight digits:

```cpp
out << "0x" << std::hex << std::nouppercase
    << std::setw(8) << std::setfill('0') << pc;
```

Restore decimal formatting before counts.

- [ ] **Step 4: Implement exact deterministic report shape**

Emit exactly one header line, then six summary lines in enum/canonical order:

```text
PAD_RUNTIME_CONFIRMATION_V0
PAD_RUNTIME function=padInit static_confidence=unresolved runtime_status=unobserved pc=none observed=0 compatible=0 incompatible=0
PAD_RUNTIME function=padPortOpen static_confidence=unresolved runtime_status=unobserved pc=none observed=0 compatible=0 incompatible=0
PAD_RUNTIME function=padGetState static_confidence=unresolved runtime_status=unobserved pc=none observed=0 compatible=0 incompatible=0
PAD_RUNTIME function=padRead static_confidence=unresolved runtime_status=unobserved pc=none observed=0 compatible=0 incompatible=0
PAD_RUNTIME function=padPortClose static_confidence=unresolved runtime_status=unobserved pc=none observed=0 compatible=0 incompatible=0
PAD_RUNTIME function=padEnd static_confidence=unresolved runtime_status=unobserved pc=none observed=0 compatible=0 incompatible=0
```

After each function's summary line, emit one `PAD_RUNTIME_PC` line for **every evidence-backed PC in that function**, including zero-observation PCs, sorted ascending:

```text
PAD_RUNTIME_PC function=padRead pc=0x00124500 observed=15 compatible=15 incompatible=0
```

This keeps static candidates visible while remaining bounded by the static evidence set. Do not emit the stored `first_compatible` / `first_incompatible` arguments or any RAM bytes in v0.

- [ ] **Step 5: Prove byte-determinism and ambiguity formatting**

Tests must compare exact strings and prove:

- calling formatter twice on the same result gives byte-identical output;
- function order is fixed;
- per-PC order is ascending even if discovery evidence was supplied unsorted;
- `RuntimeConfirmed` prints exactly one selected `pc=0x........`;
- `Unobserved`, `ObservedIncompatible`, and `RuntimeAmbiguous` print `pc=none`;
- counts are decimal;
- hex is lowercase;
- no `score=` field appears;
- no `arg`, `memory`, `bytes`, or HLE activation field appears.

- [ ] **Step 6: Run Task 3 GREEN and regressions**

```powershell
cmake --build --preset vs2022-debug --target ps2_pad_runtime_confirmation_tests ps2_pad_runtime_report_tests
ctest --preset vs2022-debug -R "ps2_pad_runtime_(confirmation|report)_tests"
ctest --preset vs2022-debug --output-on-failure
```

```bash
git add src/analysis/ps2_pad_runtime_report.h src/analysis/ps2_pad_runtime_report.cpp tests/ps2_pad_runtime_confirmation_tests.cpp tests/ps2_pad_runtime_report_tests.cpp CMakeLists.txt
git commit -m "feat: report PAD runtime confirmation"
```

- [ ] **Step 7: Run Windows CI on exact Task 3 head and review**

Require full workflow green. Review that runtime status never rewrites `PadBindingConfidence` and formatter does not select by static score.

---

### Task 4: Synthetic Dispatcher-to-PAD Confirmation Integration

**Files:**
- Create: `tests/ps2_pad_runtime_confirmation_dispatcher_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `R5900BlockDispatcher`, `Ps2PadRuntimeConfirmation`, synthetic `PadBindingDiscoveryResult`, synthetic `Ps2MemoryMap`.
- Produces: proof that a completed synthetic guest call updates PAD runtime confirmation without HLE activation or guest-state mutation beyond normal call execution.

- [ ] **Step 1: Add integration target and write RED integration test**

Register:

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

Construct a synthetic executable memory image containing a caller `JAL` to a synthetic `padRead` candidate PC. Create discovery evidence:

```cpp
PadBindingEvidence{
    PadBindingFunction::PadRead,
    PadBindingEvidenceKind::StaticFingerprint,
    pad_read_pc,
    100u,
    "synthetic-padRead",
};
```

Set `$a0=0`, `$a1=0`, `$a2=valid_32_byte_guest_buffer`. Install `Ps2PadRuntimeConfirmation` as `R5900BlockDispatcherOptions::call_observer`. Run exactly the caller block.

Assert:

```cpp
const auto confirmation = runtime_confirmation.result();
const auto& pad_read = confirmation.functions[
    static_cast<std::size_t>(PadBindingFunction::PadRead)];
CHECK(pad_read.runtime_status == PadRuntimeConfirmationStatus::RuntimeConfirmed);
CHECK(pad_read.guest_pc == pad_read_pc);
CHECK(dispatch_result.blocks_executed == 1u);
CHECK(dispatch_result.guest_calls_handled == 0u);
```

Also snapshot the 32-byte guest buffer before dispatch and prove it is unchanged afterward. Confirmation is read-only and no HLE service is installed.

The RED for this task should be a behavioral failure only if the previous units do not yet integrate correctly; if it passes immediately because Tasks 1–3 already compose correctly, record it as an integration characterization test and do not invent production changes merely to force a RED. The strict RED requirement has already been satisfied for each production unit in Tasks 1–3.

- [ ] **Step 2: Add negative and cache integration cases**

Use separate synthetic runs to prove:

- same target with invalid `a2` -> `ObservedIncompatible`;
- unrelated JAL target -> PAD result remains `Unobserved`;
- second run through cached caller -> counts increase once, not twice;
- fast-cache replay -> counts increase once;
- observer installed with `guest_calls=nullptr` never activates HLE;
- installing an unrelated guest-call service at another PC does not affect confirmation.

- [ ] **Step 3: Run integration and full regression**

```powershell
cmake --build --preset vs2022-debug --target ps2_pad_runtime_confirmation_dispatcher_windows_tests
ctest --preset vs2022-debug -R ps2_pad_runtime_confirmation_dispatcher_windows_tests
ctest --preset vs2022-debug --output-on-failure
```

```bash
git add tests/ps2_pad_runtime_confirmation_dispatcher_windows_tests.cpp CMakeLists.txt
git commit -m "test: integrate PAD runtime confirmation with dispatcher"
```

- [ ] **Step 4: Run Windows CI on exact Task 4 head and perform two-stage review**

Review 1 — spec compliance:

```text
observer only on completed JAL/JALR
post-delay args
actual returned target
all evidence PCs considered
ABI rules exact
runtime ambiguity never score-resolved
no HLE activation
no proprietary data
```

Review 2 — code quality:

```text
no duplicated observer emission logic
no unbounded trace vector
const memory access for ABI checks
saturating counters
canonical deterministic ordering
no unnecessary Windows dependency in PAD confirmation/report
```

Do not proceed to docs until both reviews pass and CI is green.

---

### Task 5: Validation Documentation and Exact-Head CI

**Files:**
- Create: `docs/validation/2026-09-08-ps2-pad-runtime-confirmation-v0.md`
- Modify: `docs/PROGRESS.md`

**Interfaces:**
- Consumes: exact implementation head SHA and CI evidence from Tasks 1–4.
- Produces: auditable milestone validation record; no code changes.

- [ ] **Step 1: Write the validation ledger**

Record:

```text
Milestone: PS2 PAD Runtime Confirmation v0
Status before final docs CI: PENDING_FINAL_EXACT_HEAD_CI
Base: d7b9fc436805dc7e5908d277409eed208ded8f32
Implementation branch: feature/ps2-pad-runtime-confirmation-v0
```

Document each TDD gate with exact RED SHA/run, GREEN SHA/run, failing reason for RED, and passing test counts for GREEN. Include explicit statements:

```text
No Burnout 3 guest PCs were hardcoded.
No proprietary ELF/code/RAM bytes were committed.
No Ps2PadHleBindings are created or mutated by runtime confirmation.
Ps2PadHleService behavior is unchanged.
WinMain production wiring is not part of this milestone.
Real Burnout 3 PAD runtime confirmation remains PENDING_EXTERNAL_VALIDATION until a complete lawful ELF reaches evidence-backed calls.
```

- [ ] **Step 2: Update `docs/PROGRESS.md` conservatively**

Mark:

```text
PS2 PAD Binding Discovery v0      CI_VALIDATED
PS2 PAD Runtime Confirmation v0   PENDING_FINAL_EXACT_HEAD_CI
PS2 PAD Runtime Activation        TODO
```

Do not claim game input works in real Burnout 3 yet.

- [ ] **Step 3: Commit docs atomically**

```bash
git add docs/PROGRESS.md docs/validation/2026-09-08-ps2-pad-runtime-confirmation-v0.md
git commit -m "docs: record PS2 PAD runtime confirmation validation"
```

- [ ] **Step 4: Run fresh Windows CI on this exact documentation head**

Require all steps green on the exact SHA:

```text
Configure
Build
Test / full CTest
Frame pacing telemetry
Pacing probe smoke at 120 Hz
Stage analyzer package
Validate analyzer package
Stage pacing probe package
Validate pacing probe package
```

Do not reuse a prior implementation-head CI result as final evidence.

- [ ] **Step 5: Verify the exact CI log and final status**

From the final exact-head workflow log, record:

- workflow/run ID;
- job ID;
- exact head SHA;
- CTest passed/total count;
- pacing telemetry result;
- probe result;
- analyzer package validation;
- pacing package validation.

If any step fails, keep status `PENDING_FINAL_EXACT_HEAD_CI`, debug the root cause, fix, recommit, and run a new exact-head CI.

- [ ] **Step 6: If CI is green, make one final status-only docs commit and validate it too**

Update the ledger and `docs/PROGRESS.md` from `PENDING_FINAL_EXACT_HEAD_CI` to `CI_VALIDATED`, including the successful run identifiers. Commit only those documentation files:

```bash
git add docs/PROGRESS.md docs/validation/2026-09-08-ps2-pad-runtime-confirmation-v0.md
git commit -m "docs: mark PS2 PAD runtime confirmation CI validated"
```

Because this creates a new head, run Windows CI **once more** on that exact final status SHA. Only after that second documentation-head CI is green may the milestone be reported `CI_VALIDATED`.

- [ ] **Step 7: Final integrity audit**

Compare the validated base to the final head and verify:

```text
Expected production changes only:
  src/recompiler/r5900_call_observer.h
  src/recompiler/windows/r5900_block_dispatcher.h
  src/recompiler/windows/r5900_block_dispatcher.cpp
  src/analysis/ps2_pad_runtime_confirmation.h/.cpp
  src/analysis/ps2_pad_runtime_report.h/.cpp
  CMakeLists.txt

Expected tests only:
  tests/r5900_block_dispatcher_call_observer_windows_tests.cpp
  tests/ps2_pad_runtime_confirmation_tests.cpp
  tests/ps2_pad_runtime_report_tests.cpp
  tests/ps2_pad_runtime_confirmation_dispatcher_windows_tests.cpp

Expected docs only:
  docs/PROGRESS.md
  docs/validation/2026-09-08-ps2-pad-runtime-confirmation-v0.md
```

Verify no changes to `ps2_pad_hle_service.*`, `win_main.cpp`, or `ps2_elf.*`. Report real Burnout 3 runtime confirmation as external validation pending, not complete.
