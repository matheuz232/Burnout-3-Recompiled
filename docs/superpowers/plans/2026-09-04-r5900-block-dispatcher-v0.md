# R5900 Block Dispatcher v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Windows x86-64 R5900 dispatcher that analyzes guest basic blocks, executes only the current supported straight-line prefix through the native backend, caches compiled blocks by guest PC plus exact-code fingerprint, and returns deterministic stop/accounting data.

**Architecture:** Introduce a Windows-only dispatcher target layered over `b3r_analysis`, `b3r_runtime`, and `b3r_recompiler_x64`. The dispatcher owns guest-PC progression and a native block cache; it does not change `R5900IrExecutionState`, the x64 generated-function ABI, CFG semantics, or the executable-memory policy. Current control-flow instructions, traps, delay slots, guest loads/stores, and unsupported decoded instructions remain non-executed boundaries.

**Tech Stack:** C++20, CMake 3.25+, Visual Studio 2022 x64 / MSVC, Windows 10+ APIs through the existing x64 backend, CTest, GitHub Actions `windows-2022`.

**Spec:** `docs/superpowers/specs/2026-09-04-r5900-block-dispatcher-v0-design.md`

## Global Constraints

- Windows x86-64 only; keep all portable analysis/recompiler targets buildable on non-Windows hosts.
- Do not add third-party dependencies.
- Do not change `R5900IrExecutionState`; guest PC remains dispatcher-owned.
- Do not execute branches, jumps, calls, traps, branch-likely semantics, or delay slots in v0.
- Do not add guest load/store execution, direct native block chaining, register allocation, cache eviction, or thread-safe dispatch.
- Preserve the existing x64 backend's RW -> RX W^X publication, instruction-cache flush, and RAII ownership; the dispatcher must not allocate executable memory itself.
- Cache validity requires `start_pc`, instruction count, FNV-1a fingerprint, and exact guest-word vector equality.
- Metrics in `R5900DispatchResult` are per `run()` call.
- A semantic boundary (`ControlFlow`, `UnsupportedInstruction`, `Trap`) takes precedence over `BlockBudgetExhausted` after an executed prefix.
- Existing 27 tests must remain green; the new dispatcher suite increases the Windows CTest count.
- Do not claim real Burnout 3 ELF/game execution after this milestone.

---

## File Structure

**Create:**

- `src/recompiler/windows/r5900_block_dispatcher.h` — public dispatcher contract, options/result types, cache ownership.
- `src/recompiler/windows/r5900_block_dispatcher.cpp` — supported-prefix selection, fingerprinting, cache validation, lowering/compile-on-demand, native execution, stop/accounting semantics.
- `tests/r5900_block_dispatcher_windows_tests.cpp` — synthetic executable guest-memory fixtures and all dispatcher TDD coverage.

**Modify:**

- `CMakeLists.txt` — add Windows-only `b3r_recompiler_dispatcher_x64` and `r5900_block_dispatcher_windows_tests` targets.
- `README.md` — document the native block-dispatch bridge and its deliberate limitations.
- `PROGRESS.md` — record dispatcher v0 status and Windows CI evidence after implementation is green.

The existing `src/recompiler/windows/r5900_x64_backend.*`, CFG analyzer, IR lowering, executor, and `Ps2MemoryMap` remain behaviorally unchanged.

---

### Task 1: Public dispatcher contract and failure-safe entry path

**Files:**
- Create: `src/recompiler/windows/r5900_block_dispatcher.h`
- Create: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Create: `tests/r5900_block_dispatcher_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `analysis::R5900ControlFlowOptions`, `runtime::Ps2MemoryMap`, `R5900IrExecutionState`, `R5900X64CompiledBlock`.
- Produces:

```cpp
enum class R5900DispatchStopReason {
    BlockBudgetExhausted,
    ControlFlow,
    UnsupportedInstruction,
    Trap,
    InvalidBlockBudget,
    AnalysisFailure,
    LoweringFailure,
    CompileFailure,
};

struct R5900DispatchResult {
    R5900DispatchStopReason reason{R5900DispatchStopReason::AnalysisFailure};
    std::uint32_t next_pc{};
    std::size_t blocks_executed{};
    std::size_t instructions_executed{};
    std::size_t cache_hits{};
    std::size_t cache_misses{};
    std::size_t recompilations{};
    std::string message{};
};

struct R5900BlockDispatcherOptions {
    analysis::R5900ControlFlowOptions block_options{};
};

class R5900BlockDispatcher {
public:
    explicit R5900BlockDispatcher(
        const runtime::Ps2MemoryMap& memory,
        R5900BlockDispatcherOptions options = {});

    [[nodiscard]] R5900DispatchResult run(
        std::uint32_t start_pc,
        R5900IrExecutionState& state,
        std::size_t max_blocks);

    void clear_cache() noexcept;
    [[nodiscard]] std::size_t cache_size() const noexcept;
};
```

- [ ] **Step 1: Add the Windows test target and write the first failing contract tests**

Add to the `if(WIN32)` test section in `CMakeLists.txt`:

```cmake
add_executable(r5900_block_dispatcher_windows_tests
  tests/r5900_block_dispatcher_windows_tests.cpp
)
target_link_libraries(r5900_block_dispatcher_windows_tests PRIVATE
  b3r_recompiler_dispatcher_x64
)
add_test(NAME r5900_block_dispatcher_windows_tests COMMAND r5900_block_dispatcher_windows_tests)
```

Add the production target beside `b3r_recompiler_x64`:

```cmake
add_library(b3r_recompiler_dispatcher_x64 STATIC
  src/recompiler/windows/r5900_block_dispatcher.cpp
)
target_include_directories(b3r_recompiler_dispatcher_x64 PUBLIC src)
target_link_libraries(b3r_recompiler_dispatcher_x64 PUBLIC
  b3r_analysis
  b3r_recompiler_x64
)
if(MSVC)
  target_compile_options(b3r_recompiler_dispatcher_x64 PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()
```

Start `tests/r5900_block_dispatcher_windows_tests.cpp` with the repository's existing `fail()` / `expect()` style and synthetic ELF helpers copied in focused form from `ps2_memory_map_tests.cpp`. The helper must produce an executable PT_LOAD (`flags = 5`) containing little-endian instruction words.

```cpp
#include "recompiler/windows/r5900_block_dispatcher.h"
#include "recompiler/ps2_elf.h"
#include "runtime/ps2_memory_map.h"

// ... fail/expect, put_u16, put_u32, make_executable_memory helpers ...

int main() {
    using namespace b3r::recompiler;

    auto memory = make_executable_memory({0u}, 0x00100000u);
    R5900BlockDispatcher dispatcher(memory);
    R5900IrExecutionState state{};
    state.gpr[1] = {0x1122334455667788ull, 0x8877665544332211ull};

    const auto zero_budget = dispatcher.run(0x00100000u, state, 0u);
    expect(zero_budget.reason == R5900DispatchStopReason::InvalidBlockBudget,
           "zero block budget must reject explicitly");
    expect(zero_budget.next_pc == 0x00100000u,
           "zero block budget must retain start PC");
    expect(zero_budget.blocks_executed == 0u && zero_budget.instructions_executed == 0u,
           "zero block budget must execute nothing");
    expect(zero_budget.cache_hits == 0u && zero_budget.cache_misses == 0u &&
               zero_budget.recompilations == 0u,
           "zero block budget must not touch cache accounting");
    expect(state.gpr[1].low64 == 0x1122334455667788ull &&
               state.gpr[1].high64 == 0x8877665544332211ull,
           "zero block budget must not mutate state");

    const auto unaligned = dispatcher.run(0x00100002u, state, 1u);
    expect(unaligned.reason == R5900DispatchStopReason::AnalysisFailure,
           "unaligned PC must map analyzer failure");
    expect(unaligned.next_pc == 0x00100002u,
           "analysis failure must report failing PC");
    expect(unaligned.blocks_executed == 0u,
           "analysis failure must not consume budget");

    std::cout << "r5900_block_dispatcher_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
```

- [ ] **Step 2: Run RED verification**

Run:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --parallel
```

Expected: build fails because `recompiler/windows/r5900_block_dispatcher.h` / dispatcher implementation does not exist yet.

Commit the RED state:

```bash
git add CMakeLists.txt tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "test: define R5900 block dispatcher contract"
```

On GitHub-only execution, confirm the corresponding Windows CI run fails for the expected missing-dispatcher reason before implementing GREEN.

- [ ] **Step 3: Implement the public contract and entry-path validation**

Create `r5900_block_dispatcher.h` with the exact public types above. Keep cache representation private. A straightforward v0 representation is:

```cpp
struct CachedBlock {
    std::uint32_t start_pc{};
    std::uint32_t end_pc_exclusive{};
    std::uint64_t fingerprint{};
    std::vector<std::uint32_t> guest_words{};
    std::size_t guest_instruction_count{};
    R5900X64CompiledBlock native_block{};
};

const runtime::Ps2MemoryMap& memory_;
R5900BlockDispatcherOptions options_{};
std::unordered_map<std::uint32_t, CachedBlock> cache_{};
```

Create `r5900_block_dispatcher.cpp`. Implement constructor, `clear_cache()`, `cache_size()`, and the first two `run()` guards:

```cpp
R5900DispatchResult R5900BlockDispatcher::run(
    std::uint32_t start_pc,
    R5900IrExecutionState& state,
    std::size_t max_blocks) {
    (void)state;

    R5900DispatchResult result{};
    result.next_pc = start_pc;

    if (max_blocks == 0u) {
        result.reason = R5900DispatchStopReason::InvalidBlockBudget;
        result.message = "R5900 dispatcher block budget must be non-zero";
        return result;
    }

    const auto analyzed = analysis::analyze_r5900_basic_block(
        memory_, start_pc, options_.block_options);
    if (!analyzed.ok()) {
        result.reason = R5900DispatchStopReason::AnalysisFailure;
        result.message = format_stage_error("analysis", start_pc, analyzed.message);
        return result;
    }

    // Temporary GREEN for Task 1 only: a successfully analyzed block is not executed yet.
    result.reason = R5900DispatchStopReason::UnsupportedInstruction;
    result.message = "R5900 dispatcher execution bridge not reached by Task 1 tests";
    return result;
}
```

Implement a private `format_stage_error(stage, pc, detail)` with zero-padded 8-digit hexadecimal guest PC, e.g. `analysis at guest PC 0x00100002: ...`.

- [ ] **Step 4: Run GREEN verification for Task 1**

Run:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release -R r5900_block_dispatcher_windows_tests --output-on-failure
```

Expected: dispatcher test passes for invalid-budget and unaligned-analysis cases.

Also run:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

Expected: all existing tests plus the new dispatcher test pass.

- [ ] **Step 5: Commit Task 1 GREEN**

```bash
git add CMakeLists.txt src/recompiler/windows/r5900_block_dispatcher.h src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "feat: add R5900 block dispatcher contract"
```

---

### Task 2: Supported-prefix compilation and semantic boundaries

**Files:**
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Modify: `tests/r5900_block_dispatcher_windows_tests.cpp`

**Interfaces:**
- Consumes: `analysis::R5900BasicBlock`, `lower_r5900_instruction()`, `compile_r5900_ir_x64()`.
- Produces internal behavior:
  - eligibility predicate for exactly `Nop`, `Addu`, `Addiu`, `Ori`;
  - boundary classification to `ControlFlow`, `UnsupportedInstruction`, `Trap`;
  - complete-prefix lowering and native execution;
  - `next_pc = block_start + instruction_count * 4`.

- [ ] **Step 1: Write failing tests for a supported prefix and boundaries**

Add encoding helpers:

```cpp
constexpr std::uint32_t r_type(std::uint8_t rs, std::uint8_t rt,
                               std::uint8_t rd, std::uint8_t sa,
                               std::uint8_t funct) {
    return (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) |
           funct;
}

constexpr std::uint32_t i_type(std::uint8_t op, std::uint8_t rs,
                               std::uint8_t rt, std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           imm;
}

constexpr std::uint32_t j_type(std::uint8_t op, std::uint32_t target) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           ((target >> 2u) & 0x03ffffffu);
}
```

Add one mixed supported-prefix test using:

```cpp
const std::uint32_t words[] = {
    0u,                                  // NOP
    r_type(9, 10, 8, 0, 0x21),         // ADDU r8,r9,r10
    i_type(0x09, 29, 29, 0xfff0),      // ADDIU r29,r29,-16
    i_type(0x0d, 4, 5, 0xff00),        // ORI r5,r4,0xff00
    i_type(0x0c, 1, 2, 0x00ff),        // ANDI: decoded, unsupported by IR v0
};
```

Use `block_options.max_instructions = 1024`. Expect the first four instructions to execute once, then:

```cpp
expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction,
       "supported prefix must stop before unsupported decoded instruction");
expect(result.next_pc == base + 16u, "unsupported boundary PC must be exact");
expect(result.blocks_executed == 1u && result.instructions_executed == 4u,
       "only supported prefix must be accounted");
```

Add boundary-at-entry tests:

- `ANDI` -> `UnsupportedInstruction`, zero blocks/instructions, unchanged state.
- `SYSCALL` raw word `0x0000000cu` -> `Trap`, zero blocks/instructions.
- `BEQ` with mapped NOP delay slot -> `ControlFlow`, zero blocks/instructions.

Add prefix + control-flow tests for representative terminators:

```text
ADDIU + BEQ + NOP delay slot
ADDIU + J   + NOP delay slot
ADDIU + JAL + NOP delay slot
ADDIU + JR  + NOP delay slot
```

For each, expect the ADDIU side effect only, `next_pc` at the terminator, `ControlFlow`, and no delay-slot side effect.

- [ ] **Step 2: Run RED verification**

Run:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release -R r5900_block_dispatcher_windows_tests --output-on-failure
```

Expected: FAIL because Task 1 implementation does not compile/execute supported prefixes or classify all boundaries.

Commit RED:

```bash
git add tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "test: cover R5900 dispatcher prefix boundaries"
```

- [ ] **Step 3: Implement explicit eligibility and candidate selection**

Add a private predicate:

```cpp
bool is_dispatcher_v0_eligible(R5900Instruction instruction) noexcept {
    switch (instruction) {
    case R5900Instruction::Nop:
    case R5900Instruction::Addu:
    case R5900Instruction::Addiu:
    case R5900Instruction::Ori:
        return true;
    default:
        return false;
    }
}
```

Walk `block.instructions` in PC order. Before eligibility, classify:

```cpp
if (site.decoded.is_branch() || site.decoded.is_jump()) {
    boundary_reason = R5900DispatchStopReason::ControlFlow;
    boundary_pc = site.pc;
    break;
}
if (site.decoded.instruction_class == R5900InstructionClass::System) {
    boundary_reason = R5900DispatchStopReason::Trap;
    boundary_pc = site.pc;
    break;
}
if (!is_dispatcher_v0_eligible(site.decoded.instruction)) {
    boundary_reason = R5900DispatchStopReason::UnsupportedInstruction;
    boundary_pc = site.pc;
    break;
}
prefix.push_back(site);
```

Never inspect or append `block.delay_slot`.

If `prefix.empty()`, return the boundary immediately with zero state/cache/budget mutation.

- [ ] **Step 4: Implement complete lowering, native compile, and one-block execution**

For each selected site:

```cpp
const auto lowered = lower_r5900_instruction(site.decoded, site.pc);
if (!lowered.ok()) {
    result.reason = R5900DispatchStopReason::LoweringFailure;
    result.next_pc = site.pc;
    result.message = format_stage_error("lowering", site.pc, lowered.message);
    return result;
}
ir.insert(ir.end(), lowered.instructions.begin(), lowered.instructions.end());
```

Compile the complete IR vector only after all selected sites lower successfully:

```cpp
auto compiled = compile_r5900_ir_x64(ir);
if (!compiled.ok()) {
    result.reason = R5900DispatchStopReason::CompileFailure;
    result.next_pc = prefix.front().pc;
    result.message = format_stage_error("x64 compile", prefix.front().pc, compiled.message);
    return result;
}

compiled.block->execute(state);
result.blocks_executed = 1u;
result.instructions_executed = prefix.size();
result.next_pc = prefix.front().pc + static_cast<std::uint32_t>(prefix.size() * 4u);
```

Task 2 may temporarily compile every candidate without reuse; cache semantics are Task 3/4.

After execution, if a boundary was selected, return its semantic reason even when `max_blocks == 1`. For an `InstructionLimit` chunk with no boundary, return `BlockBudgetExhausted` when the single-block budget has been consumed.

- [ ] **Step 5: Run GREEN verification and commit**

Run:

```powershell
ctest --test-dir build -C Release -R r5900_block_dispatcher_windows_tests --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

Expected: all tests pass.

Commit:

```bash
git add src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "feat: execute supported R5900 block prefixes"
```

---

### Task 3: Multi-block run loop, budget semantics, and cache hits

**Files:**
- Modify: `src/recompiler/windows/r5900_block_dispatcher.h`
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Modify: `tests/r5900_block_dispatcher_windows_tests.cpp`

**Interfaces:**
- Consumes: Task 2 candidate selection and compile/execute path.
- Produces:
  - looped `run()` across `InstructionLimit` chunks;
  - per-call cumulative block/instruction metrics;
  - cache lookup by `start_pc`;
  - exact cache hit/miss accounting;
  - `cache_size()` and `clear_cache()` behavior.

- [ ] **Step 1: Write failing multi-block and cache tests**

Create a sequence of five eligible instructions followed by `ANDI`. Configure:

```cpp
R5900BlockDispatcherOptions options{};
options.block_options.max_instructions = 2u;
R5900BlockDispatcher dispatcher(memory, options);
```

Validate:

- `run(base, state, 1)` executes 2 instructions, returns `BlockBudgetExhausted`, `next_pc == base + 8`, `blocks_executed == 1`.
- fresh dispatcher/state with budget 2 executes 4 instructions, returns `BlockBudgetExhausted`, `next_pc == base + 16`, `blocks_executed == 2`.
- fresh dispatcher/state with budget 3 executes the fifth supported instruction, then returns `UnsupportedInstruction` at `base + 20`; semantic boundary wins over budget exhaustion.

Add cache reuse test using a two-instruction `InstructionLimit` chunk:

```cpp
const auto first = dispatcher.run(base, state1, 1u);
expect(first.cache_misses == 1u && first.cache_hits == 0u,
       "first candidate must be a cache miss");
expect(dispatcher.cache_size() == 1u, "successful compile must populate cache");

const auto second = dispatcher.run(base, state2, 1u);
expect(second.cache_hits == 1u && second.cache_misses == 0u,
       "identical guest code must reuse native block");
expect(dispatcher.cache_size() == 1u, "cache hit must not duplicate entry");

dispatcher.clear_cache();
expect(dispatcher.cache_size() == 0u, "clear_cache must destroy all entries");
```

- [ ] **Step 2: Run RED verification**

Run the dispatcher test; expect failures because Task 2 executes at most one candidate and does not retain native blocks.

Commit RED:

```bash
git add tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "test: cover dispatcher budgets and cache reuse"
```

- [ ] **Step 3: Refactor one-candidate logic into the run loop**

Use local `current_pc = start_pc`. At the top of each iteration, analyze/select the current candidate. After a valid native execution:

```cpp
++result.blocks_executed;
result.instructions_executed += prefix.size();
current_pc += static_cast<std::uint32_t>(prefix.size() * 4u);
result.next_pc = current_pc;
```

If the just-analyzed candidate has a semantic boundary after the prefix, return that boundary immediately. Otherwise, the analyzer chunk ended by `InstructionLimit`:

```cpp
if (result.blocks_executed == max_blocks) {
    result.reason = R5900DispatchStopReason::BlockBudgetExhausted;
    return result;
}
```

Then loop at `current_pc`.

Do not increment block budget for analysis, cache probe, lowering, or compile failures.

- [ ] **Step 4: Add cache insertion/reuse without fingerprint invalidation yet**

Before compiling, capture `guest_words` from the selected sites' current raw words. Look up `cache_.find(prefix.front().pc)`.

For Task 3 GREEN, reuse only if `guest_words` and instruction count are exactly equal; Task 4 adds the specified FNV fingerprint and stale accounting.

On exact hit:

```cpp
++result.cache_hits;
it->second.native_block.execute(state);
```

On no entry:

```cpp
++result.cache_misses;
// lower + compile
cache_.emplace(start_pc, CachedBlock{...});
cache_.at(start_pc).native_block.execute(state);
```

The cache must own the move-only `R5900X64CompiledBlock`; do not copy compiled blocks.

- [ ] **Step 5: Run GREEN verification and commit**

Run dispatcher-only then full CTest. Expected: all pass.

Commit:

```bash
git add src/recompiler/windows/r5900_block_dispatcher.h src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "feat: add R5900 dispatcher run loop and cache"
```

---

### Task 4: Exact fingerprint validation and automatic stale-code recompilation

**Files:**
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Modify: `tests/r5900_block_dispatcher_windows_tests.cpp`

**Interfaces:**
- Consumes: Task 3 cache keyed by `start_pc`.
- Produces:
  - deterministic 64-bit FNV-1a fingerprint over start PC, fixed-width instruction count, and guest words;
  - exact-word collision guard;
  - stale-code rejection and `recompilations` accounting.

- [ ] **Step 1: Write failing automatic-invalidation tests**

Use `block_options.max_instructions = 1` with:

```cpp
const auto addiu_one = i_type(0x09, 0, 1, 1);  // r1 = 1
const auto addiu_seven = i_type(0x09, 0, 1, 7); // r1 = 7
```

First run:

```cpp
const auto first = dispatcher.run(base, state1, 1u);
expect(state1.gpr[1].low64 == 1u, "first compiled word must execute");
expect(first.cache_misses == 1u && first.recompilations == 0u,
       "first compile must be a miss");
```

Mutate guest code through the original non-const memory object:

```cpp
expect(memory.write_u32(base, addiu_seven), "guest code mutation must succeed");
```

Second run from a reset state:

```cpp
const auto second = dispatcher.run(base, state2, 1u);
expect(state2.gpr[1].low64 == 7u,
       "stale native block must never execute after guest code change");
expect(second.recompilations == 1u,
       "stale candidate must be counted as recompilation");
expect(second.cache_hits == 0u && second.cache_misses == 0u,
       "stale candidate is neither hit nor plain miss");
expect(dispatcher.cache_size() == 1u,
       "successful replacement must keep one cache entry per start PC");
```

Also mutate the cached start word to unsupported `ANDI`. Expect `UnsupportedInstruction`, zero blocks, zero cache hit/miss/recompile for that call, and no stale execution. The old physical cache entry may remain, but it must be logically unusable.

- [ ] **Step 2: Run RED verification**

Run dispatcher test. Expected: stale-code test fails because Task 3 has no required fingerprint/recompilation semantics.

Commit RED:

```bash
git add tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "test: require dispatcher cache invalidation"
```

- [ ] **Step 3: Implement exact FNV-1a fingerprint**

Use the spec-defined constants and fixed little-endian encoding:

```cpp
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void fnv_byte(std::uint64_t& hash, std::uint8_t byte) noexcept {
    hash ^= byte;
    hash *= kFnvPrime;
}

void fnv_u32_le(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        fnv_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void fnv_u64_le(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        fnv_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}
```

Fingerprint order:

```cpp
std::uint64_t hash = kFnvOffset;
fnv_u32_le(hash, start_pc);
fnv_u64_le(hash, static_cast<std::uint64_t>(guest_words.size()));
for (const auto word : guest_words) {
    fnv_u32_le(hash, word);
}
```

Re-read each selected word with `memory_.read_u32(site.pc)` before cache validation. If a selected word can no longer be read, return `AnalysisFailure` at that site without executing the candidate.

- [ ] **Step 4: Implement exact cache validity and stale replacement**

A hit requires all fields:

```cpp
const bool exact_match =
    cached.start_pc == start_pc &&
    cached.guest_instruction_count == guest_words.size() &&
    cached.fingerprint == fingerprint &&
    cached.guest_words == guest_words;
```

If exact: increment `cache_hits` and execute cached native block.

If no entry: increment `cache_misses`, compile, then insert.

If entry exists but differs: increment `recompilations` when compilation of the current eligible candidate is attempted. Never execute the stale entry. Compile the new IR into a temporary `R5900X64CompiledBlock`; only on success move it into the cache entry and replace metadata.

If current code produces no non-empty eligible prefix, return the semantic boundary before cache lookup. This intentionally leaves any old physical entry untouched but unreachable by exact current-candidate validation.

- [ ] **Step 5: Run GREEN verification and commit**

Run dispatcher-only and full CTest. Expected: all pass.

Commit:

```bash
git add src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "feat: invalidate stale R5900 native blocks"
```

---

### Task 5: Differential semantics, analysis failures, diagnostics, and full regression

**Files:**
- Modify: `tests/r5900_block_dispatcher_windows_tests.cpp`
- Modify if required by discovered test failures: `src/recompiler/windows/r5900_block_dispatcher.cpp`

**Interfaces:**
- Consumes: complete dispatcher v0 from Tasks 1-4.
- Produces: final semantic confidence across all 32 GPRs and failure-stage diagnostics.

- [ ] **Step 1: Add differential full-state test**

Initialize every GPR low/high half with distinct values, including nonzero GPR0 and aliasing-sensitive registers. Use the existing supported sequence:

```cpp
const std::vector<std::uint32_t> words = {
    0u,
    r_type(9, 10, 8, 0, 0x21),
    i_type(0x09, 29, 29, 0xfff0),
    i_type(0x0d, 4, 5, 0xff00),
};
```

Set `block_options.max_instructions = words.size()` so exactly one native chunk is dispatched. Build reference IR independently with `decode_r5900()` + `lower_r5900_instruction()` and run `execute_r5900_ir()` on `expected`. Run dispatcher on `actual` with budget 1. Compare every `gpr[index].low64` and `high64`.

Expect:

- exact equality for all 32 GPRs;
- GPR0 normalized to zero;
- high64 preserved for current writes;
- ADDIU aliasing preserved;
- dispatcher result `BlockBudgetExhausted`, one block, four guest instructions.

- [ ] **Step 2: Add mapped/non-executable and unmapped failure tests**

Extend the ELF fixture helper to accept segment flags.

- executable region is flags `5` (`PF_R | PF_X`);
- non-executable mapped region uses flags `6` (`PF_R | PF_W`).

Validate:

```cpp
expect(non_exec.reason == R5900DispatchStopReason::AnalysisFailure,
       "non-executable fetch must map to analysis failure");
expect(unmapped.reason == R5900DispatchStopReason::AnalysisFailure,
       "unmapped fetch must map to analysis failure");
expect(non_exec.blocks_executed == 0u && unmapped.blocks_executed == 0u,
       "analysis failures must not consume block budget");
```

Assert diagnostic messages contain both the stage word `analysis` and the zero-padded guest PC string.

Also set `options.block_options.max_instructions = 0` in a dedicated dispatcher and verify the analyzer's invalid-instruction-limit error maps to `AnalysisFailure` without state mutation.

- [ ] **Step 3: Run the dispatcher suite and fix only contract violations**

Run:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release -R r5900_block_dispatcher_windows_tests --output-on-failure
```

Expected: PASS. If it fails, fix only dispatcher/spec mismatches; do not broaden the instruction set or add control-flow execution.

- [ ] **Step 4: Run full Windows regression exactly like CI**

Run:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\frame_pacer_windows_tests.exe
.\build\Release\Burnout3PacingProbe.exe --seconds 1
```

Expected:

- configure/build success;
- all previous 27 CTest cases plus `r5900_block_dispatcher_windows_tests` pass (28 total unless another independent test was added meanwhile);
- frame pacing telemetry remains green;
- pacing probe still targets 120 Hz / 8.333 ms cadence.

Commit the final test/code state:

```bash
git add src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "test: validate R5900 dispatcher semantics"
```

- [ ] **Step 5: Verify the pushed commit with GitHub Actions**

Use `.github/workflows/windows-ci.yml`, which runs Windows Server 2022, Visual Studio 17 2022 x64, Release build, full CTest, frame pacing telemetry, and the one-second pacing probe.

Expected: workflow conclusion `success` on the exact feature-branch commit. Record the run ID and exact commit SHA for Task 6 documentation.

---

### Task 6: Documentation and final branch validation

**Files:**
- Modify: `README.md`
- Modify: `PROGRESS.md`

**Interfaces:**
- Consumes: green implementation and CI evidence from Task 5.
- Produces: accurate project status without overstating guest execution capability.

- [ ] **Step 1: Update README with the dispatcher milestone**

Add a concise section/status bullet stating that Windows now has a minimal R5900 native block dispatcher with:

- CFG-driven supported-prefix selection;
- NOP/ADDU/ADDIU/ORI current subset;
- compile-on-demand;
- native cache reuse;
- guest-word fingerprint + exact-word invalidation;
- explicit `next_pc`, stop reason, and block budget.

In the same paragraph state explicitly that branches/jumps/calls, delay slots, guest loads/stores, and real Burnout 3 ELF/game execution are not implemented by this milestone.

- [ ] **Step 2: Update PROGRESS with status and evidence**

Add or update a row such as:

```text
R5900 native block dispatcher v0 | CI_VALIDATED
```

Document the exact Task 5 green Windows CI run ID and commit SHA. State that the bridge proves:

```text
guest memory -> CFG/basic block -> current IR -> x64 compile-on-demand -> native execute -> cache/reuse/invalidate
```

Do not change graphics/audio/input/gameplay percentages based on this infrastructure-only milestone.

- [ ] **Step 3: Commit documentation**

```bash
git add README.md PROGRESS.md
git commit -m "docs: record R5900 block dispatcher validation"
```

- [ ] **Step 4: Run final CI on the documentation head**

Wait only for the current GitHub Actions result synchronously in this execution flow; do not claim success until the run completes. Verify the exact docs-head SHA has Windows CI conclusion `success` and the full CTest suite passes.

No new code changes are expected after this point unless final CI exposes a regression.

- [ ] **Step 5: Final review checklist**

Verify against the spec:

```text
[ ] start_pc + exact-code cache validity
[ ] FNV-1a + exact guest word comparison
[ ] automatic stale-code recompilation
[ ] no stale native execution
[ ] max_blocks budget counts native executions only
[ ] semantic boundary beats budget exhaustion
[ ] next_pc is first unexecuted guest PC
[ ] no branch/jump/call/trap/delay-slot execution
[ ] no guest load/store execution
[ ] GPR semantics match reference executor
[ ] dispatcher allocates no executable pages itself
[ ] clear_cache destroys native entries via existing RAII
[ ] analysis/lowering/compile failures never execute the failing candidate
[ ] all Windows tests green
[ ] README/PROGRESS limitations are explicit
```

The branch is then ready for code review/integration, but it must not be described as real Burnout 3 guest execution or a completed recompiler.
