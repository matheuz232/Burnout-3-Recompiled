# R5900 Block Dispatcher v0 Design

Status: approved design, pending implementation plan
Date: 2026-09-04
Base commit: `465cf27778e33a9e7cc990928dc7b6aa25b71ecd`
Target: Windows x86-64 only

## 1. Purpose

Add the first execution bridge between the existing R5900 control-flow analysis, IR lowering, and Windows x86-64 backend.

The dispatcher compiles and executes supported straight-line guest instruction prefixes on demand, caches native blocks, reuses unchanged blocks, and invalidates cached code when the underlying guest instruction words change.

This milestone does **not** execute R5900 control-flow instructions, delay slots, guest memory operations, or real Burnout 3 game code. It is an infrastructure milestone that proves the pipeline:

```text
guest memory
    -> basic-block analysis
    -> supported straight-line prefix
    -> R5900 IR
    -> Windows x86-64 compile-on-demand
    -> native execution
    -> cache / reuse / invalidation
```

## 2. Existing architecture and constraints

The existing project already has:

- `Ps2MemoryMap`, with readable and writable mapped guest memory;
- `analyze_r5900_basic_block()`, which identifies instruction sites, block end kind, delay slots, and control-flow edges;
- IR v0 with `Nop`, `AddWordSignExtend`, and `Or64`;
- deterministic reference execution over `R5900IrExecutionState`;
- a Windows x86-64 backend that compiles the current IR subset into callable native machine code using RW -> RX publication and RAII ownership.

`R5900IrExecutionState` currently models the 32 EE GPRs only. This design intentionally keeps the guest program counter outside that state and under dispatcher control. The x64 backend ABI and current GPR layout therefore remain unchanged.

## 3. Scope

### 3.1 In scope

- Windows-only R5900 block dispatcher.
- `run(start_pc, state, max_blocks)` execution contract.
- Explicit block budget.
- Compile-on-demand.
- Native block cache.
- Automatic cache invalidation when compiled guest words change.
- Straight-line execution only.
- Deterministic stop reason and `next_pc`.
- Cache hit/miss/recompilation accounting.
- Differential semantic testing against the IR reference executor.
- Reuse of the existing x64 backend and W^X executable-memory policy.

### 3.2 Out of scope

- Conditional branch evaluation.
- Branch-likely semantics.
- Delay-slot execution.
- `J`, `JAL`, `JR`, `JALR` execution.
- Link-register side effects from calls.
- Guest loads/stores in native code.
- PC as an IR value or part of `R5900IrExecutionState`.
- Direct native linking/chaining between compiled blocks.
- Register allocation or optimization.
- Code-cache eviction policy.
- Concurrent/thread-safe dispatch.
- Real Burnout 3 ELF guest execution, boot, graphics, audio, input, menus, or gameplay.

## 4. Chosen architecture

Use an external dispatcher layered over the existing analyzer, lowering pipeline, and x64 backend.

The dispatcher owns control of `guest_pc`; generated native blocks continue to operate only on `R5900IrExecutionState`.

Conceptual flow:

```text
run(start_pc, state, max_blocks)
    |
    v
analyze_r5900_basic_block(memory, pc)
    |
    v
determine supported straight-line prefix
    |
    +--> no executable prefix: return stop reason at current pc
    |
    v
read exact guest words for prefix
    |
    v
cache validation
    |            \
    | hit         \ miss / stale
    v              v
execute       lower -> validate -> compile -> cache
    \              /
     \            /
      v          v
       native execute(state)
              |
              v
      pc += instruction_count * 4
              |
              v
  continue if the analyzed block ended only by instruction limit
  and block budget remains; otherwise return deterministic stop
```

This avoids prematurely adding control-flow semantics to IR v0 or changing the x64 generated-function ABI.

## 5. Public contract

The exact namespace/file names may follow existing repository conventions, but the public semantics are fixed by this section.

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
```

Dispatcher shape:

```cpp
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

`block_options` exists so tests and future callers can bound straight-line chunk size without changing the `run()` contract. The default uses the existing control-flow analyzer defaults.

The dispatcher references `Ps2MemoryMap`; it does not own or copy it. The caller must keep the map alive for the dispatcher's lifetime.

## 6. Program-counter semantics

The dispatcher owns the current guest PC as a local value during `run()`.

For every native block actually executed:

```text
next_pc_after_execution = block_start_pc + compiled_instruction_count * 4
```

The native emitter does not read or write PC in v0.

If execution stops before any native instruction at a location, `next_pc` is exactly that location.

### 6.1 Example: control-flow boundary

Guest code:

```text
0x1000  ADDIU r1,r0,1
0x1004  ORI   r2,r1,2
0x1008  BEQ   r2,r0,target
0x100C  NOP                ; architectural delay slot
```

v0 executes only `0x1000` and `0x1004`, then returns:

```text
reason = ControlFlow
next_pc = 0x1008
blocks_executed = 1
instructions_executed = 2
```

`BEQ` and its delay slot are not executed.

If the first instruction is already a control-flow terminator or unsupported instruction, no native block is executed and no architectural state is mutated by the dispatcher.

## 7. Supported-prefix selection

The dispatcher uses the existing control-flow analysis as the source of block boundaries and end classification. It must not reimplement branch target analysis or delay-slot discovery.

For a returned `R5900BasicBlock`:

1. Walk `block.instructions` in PC order.
2. Stop before any instruction that cannot lower completely to the current IR subset.
3. A control-flow terminator is never included in the compiled prefix in v0.
4. A trap is never included in the compiled prefix.
5. `block.delay_slot`, when present, is never compiled or executed in v0.
6. Only a non-empty supported prefix can become a native cached block.

When the analyzer returns `InstructionLimit`, all supported instructions in that chunk may be compiled. After execution, the dispatcher may continue at the sequential next PC if budget remains.

When the analyzer returns a control-flow end kind, unsupported end, or trap, the supported straight-line prefix before that boundary may execute once; dispatch then stops at the first unexecuted instruction.

## 8. Stop-reason mapping

### 8.1 `BlockBudgetExhausted`

Returned after `max_blocks` native blocks have successfully executed and the next sequential PC is known.

The budget counts only blocks that reached native execution. Analysis attempts, cache probes, lowering attempts, and compile failures do not consume block budget.

### 8.2 `ControlFlow`

Returned when the next unexecuted guest instruction is a branch, jump, or call. This includes direct and indirect forms identified by the existing analyzer.

No terminator or delay-slot side effect is performed.

### 8.3 `UnsupportedInstruction`

Returned when the next unexecuted instruction is decoded but is not lowerable to the current supported IR subset, or when analysis classifies an unknown/unsupported instruction boundary.

### 8.4 `Trap`

Returned before executing a system/trap terminator.

### 8.5 `InvalidBlockBudget`

`max_blocks == 0` returns immediately with:

- `next_pc == start_pc`;
- zero blocks executed;
- zero instructions executed;
- zero cache statistics for that call;
- no state mutation.

### 8.6 `AnalysisFailure`

Maps failures from `analyze_r5900_basic_block()`, including unaligned, unmapped, and non-executable instruction fetches.

### 8.7 `LoweringFailure`

Used for a failure while lowering an instruction that was selected as part of the supported prefix. This is distinct from the intentional `UnsupportedInstruction` boundary.

### 8.8 `CompileFailure`

Maps x64 compile errors. No invalid compiled block may enter the cache.

Every failure message should include the relevant guest PC and the failing stage.

## 9. Cache design

The primary lookup is by `start_pc`.

A cache entry conceptually contains:

```cpp
struct CachedBlock {
    std::uint32_t start_pc{};
    std::uint32_t end_pc_exclusive{};
    std::uint64_t fingerprint{};
    std::vector<std::uint32_t> guest_words{};
    std::size_t guest_instruction_count{};
    R5900X64CompiledBlock native_block{};
};
```

The exact raw guest words are retained so cache validity never relies on hash collision resistance alone.

### 9.1 Fingerprint

Use a deterministic 64-bit FNV-1a fingerprint over:

1. `start_pc` encoded as four bytes in little-endian order;
2. `guest_instruction_count` encoded as a fixed 64-bit little-endian value;
3. each compiled guest word encoded as four little-endian bytes in instruction order.

The fingerprint is a fast deterministic validation value and test-visible concept, not a security primitive.

### 9.2 Cache reuse

A cached block is reusable only when all of these match the current compilation candidate:

- `start_pc`;
- instruction count;
- fingerprint;
- exact `guest_words` vector.

A valid reuse increments `cache_hits`.

### 9.3 Cache miss

No entry at `start_pc` increments `cache_misses`. Successful lowering/compilation installs the new block.

### 9.4 Automatic invalidation

Because `Ps2MemoryMap` is writable, guest code can change.

On each candidate execution, the dispatcher re-reads the exact guest words selected for compilation and recomputes the fingerprint. A mismatch makes the old entry unusable immediately.

A stale mismatch increments `recompilations` when recompilation is attempted. The stale native block must never execute against changed guest words.

If recompilation fails, no current valid entry exists for those guest words and the call returns the corresponding failure. Future calls may retry compilation.

### 9.5 Cache lifetime

`R5900BlockDispatcher` owns all cached `R5900X64CompiledBlock` objects. Existing backend RAII remains responsible for `VirtualFree` and move ownership.

`clear_cache()` destroys all cached native blocks and resets cache size to zero. It does not mutate guest memory or execution state.

No eviction policy is added in v0.

## 10. Compile-on-demand transaction

Compilation of a candidate prefix follows this order:

1. Obtain successful block analysis.
2. Select non-empty supported prefix.
3. Capture exact guest raw words.
4. Validate/reuse cache entry if possible.
5. Lower every selected instruction to IR.
6. Require complete lowering of the selected prefix; never cache partial IR.
7. Compile the complete IR vector with `compile_r5900_ir_x64()`.
8. Only after compile success, install the resulting native block in the cache.
9. Execute the valid block.

A failed lowering or compile does not execute any instruction from that candidate native block.

The dispatcher does not create executable memory itself. It delegates executable-page allocation/protection/cache flush entirely to the existing x64 backend, preserving RW -> RX W^X behavior.

## 11. Accounting semantics

All metrics in `R5900DispatchResult` are per `run()` call.

- `blocks_executed`: number of cached or newly compiled native blocks actually called.
- `instructions_executed`: sum of guest instructions represented by those native blocks.
- `cache_hits`: native blocks reused without recompilation.
- `cache_misses`: candidate start PCs with no existing cache entry.
- `recompilations`: candidate start PCs where an existing entry was stale because current guest code differed and compilation of the current candidate was attempted.

A stale lookup is not also counted as a plain `cache_miss`; it is counted as a `recompilation` event.

No wall-clock timing or performance counters are added to this API.

## 12. State semantics

The dispatcher must preserve all current reference/backend semantics:

- all 32 EE GPRs are represented as 128-bit low/high halves;
- current integer writes preserve `high64`;
- GPR0 remains normalized to zero according to existing backend/reference rules;
- `AddWordSignExtend` wraps the low 32-bit addition and sign-extends the result;
- `Or64` uses full 64-bit inputs/immediates;
- source/destination aliasing remains valid.

The dispatcher itself performs no GPR transformations. State mutation occurs only by executing a successfully compiled native block.

## 13. Error atomicity

Atomicity is defined per candidate native block, not per entire `run()` call.

If earlier blocks in one `run()` call executed successfully and a later block fails analysis/lowering/compilation, earlier state mutations remain committed. The result reports the PC at which progress stopped plus the number of blocks/instructions already executed.

For a candidate block that fails before native execution, that candidate performs no state mutation and consumes no block budget.

This matches the project's existing fail-fast/partial-progress philosophy without pretending an entire multi-block run is transactional.

## 14. Testing strategy

Implementation follows TDD. Windows-specific dispatcher tests must be added without weakening existing portable tests.

### 14.1 Single straight-line block

Synthetic guest memory containing supported NOP/ADDU/ADDIU/ORI sequences.

Validate:

- native result equals the reference executor;
- correct `next_pc`;
- one block executed;
- correct instruction count.

### 14.2 Multiple dispatch iterations

Use a small `block_options.max_instructions` in the dispatcher options to split a long straight-line synthetic sequence into multiple analyzer chunks.

Validate budgets of 1, 2, and N blocks, including exact `next_pc` and accounting.

### 14.3 Cache hit

First run compiles; second run with identical guest code reuses the cached block.

Validate `cache_misses`, then `cache_hits`, and stable `cache_size()`.

### 14.4 Automatic invalidation

Compile and execute a supported block, mutate one compiled guest word through `Ps2MemoryMap`, then run again.

Validate:

- current guest words differ;
- stale native code is not executed;
- recompilation occurs;
- the new semantic result is observed.

### 14.5 Control-flow boundary

Test supported prefix followed by representative `BEQ`, `J`, `JAL`, and `JR` terminators.

Validate:

- prefix executes;
- terminator does not execute;
- delay slot does not execute;
- `next_pc` equals terminator PC;
- stop reason is `ControlFlow`.

### 14.6 Unsupported boundary

Test decoded instructions not supported by current lowering after a supported prefix.

Validate stop exactly at the unsupported guest PC.

### 14.7 Unsupported/control-flow at entry

Validate zero blocks, zero instructions, unchanged state, and `next_pc == start_pc`.

### 14.8 Trap boundary

Validate no system/trap terminator side effects and stop reason `Trap`.

### 14.9 Analysis failures

Cover:

- unaligned start PC;
- unmapped fetch;
- mapped but non-executable fetch.

No state mutation or budget consumption.

### 14.10 Lowering/compile propagation

Exercise deterministic failure seams where practical. Validate stage-specific stop reason, diagnostics with guest PC, no invalid cache insertion, and no candidate state mutation.

If a naturally reachable compile error cannot be induced through valid current IR, tests may target a narrow internal seam rather than corrupting production semantics solely to manufacture failure.

### 14.11 GPR differential semantics

Compare dispatcher/native execution to the reference executor for all 32 GPR low/high halves, including:

- GPR0;
- high64 preservation;
- aliasing;
- positive/negative word-add cases;
- full-width OR behavior.

### 14.12 W^X regression

Verify the dispatcher continues using the existing x64 backend and introduces no alternate executable-memory allocation path.

### 14.13 Existing suite

All 27 tests present at the base commit must remain green. New dispatcher tests increase the total; the milestone cannot claim completion based only on the old count.

## 15. Build integration

The dispatcher implementation and native dispatcher tests are Windows-only because they depend on the Windows x86-64 backend.

Portable analyzer, decoder, IR, reference-executor, and ELF-analysis targets remain usable without the dispatcher.

`Burnout3Analyze` remains a non-executing static-analysis tool and must not invoke native guest execution.

`Burnout3Recompiled_Test` does not need to launch the dispatcher in this milestone. Runtime/UI integration is a later gate after the dispatcher contract is proven synthetically.

## 16. Completion criteria

The milestone is complete only when all of the following are true on the final feature head:

1. Windows x64 build succeeds with the repository's pinned VS2022/MSVC CI configuration.
2. Existing tests remain green.
3. New dispatcher TDD tests pass.
4. Synthetic supported guest code executes through memory -> analyzer -> lowering -> x64 -> native state mutation.
5. Multi-block straight-line dispatch obeys `max_blocks` exactly.
6. Control-flow, delay slots, traps, and unsupported instructions stop before unsupported semantics are executed.
7. Cache hit behavior is deterministic.
8. Guest-code mutation invalidates stale native code automatically.
9. Differential state matches the reference executor for the covered current IR subset.
10. No new RWX executable-memory path exists.
11. Documentation accurately states that real Burnout 3 guest execution is still not implemented.
12. A fresh final feature-head Windows CI run is green before integration.

## 17. Next milestone after v0

After this dispatcher is validated, the next architectural step is control-flow execution. That work should separately design:

- architectural guest PC ownership if it must enter execution state;
- branch condition evaluation;
- delay-slot and branch-likely semantics;
- direct/indirect target selection;
- call/link-register behavior;
- dispatcher interaction with compiled control-flow exits.

Those semantics are deliberately not smuggled into dispatcher v0.