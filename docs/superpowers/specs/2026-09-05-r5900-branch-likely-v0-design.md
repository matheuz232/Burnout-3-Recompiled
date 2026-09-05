# R5900 BEQL + BNEL Branch-Likely v0 Design

Date: 2026-09-05
Status: Approved design pending implementation plan
Base branch: `feature/r5900-beq-delay-slot-v0`
Base SHA: `2d8030558fd0e8059e0f89a1c41da16261bac962`
Feature branch: `feature/r5900-branch-likely-v0`

## Goal

Add native Windows x86-64 execution support for ordinary R5900 `BEQL` and `BNEL` branch-likely instructions while preserving the PlayStation 2 architectural annulment rule: the delay slot executes exactly once only when the likely branch is taken and is skipped completely when the branch is not taken.

This milestone must not regress the already validated ordinary `BEQ`/`BNE`, direct `J`/`JAL`, indirect `JR`/`JALR`, `SQ` body execution, cache behavior, or 120 Hz pacing path.

## Scope

Included:

- `BEQL` (`primary opcode 0x14`)
- `BNEL` (`primary opcode 0x15`)
- explicit IR terminators for equal-likely and not-equal-likely predicates
- IR validation
- reference executor semantics
- Windows x86-64 backend emission
- native block dispatcher lowering
- cache/fingerprint behavior
- deterministic `SQ`-in-delay rejection for likely branches
- focused differential and dispatcher tests
- documentation and fresh Windows CI verification

Excluded:

- `BLEZL`
- `BGTZL`
- `BLTZL`
- `BGEZL`
- link-likely REGIMM variants
- new guest loads or stores
- enabling `SQ` inside dispatcher-managed delay slots
- extending the synthetic startup beyond its current mapped executable endpoint at `0x001001cc`
- external legal-ELF validation
- graphics, audio, input, menus, or gameplay

## Existing Project State

The decoder already recognizes `BEQL` and `BNEL`, marks them as branches with architectural delay slots, and sets `decoded.likely = true`.

The control-flow analyzer already fetches the architectural delay-slot instruction and records `delay_slot_executes_on_fallthrough = false` for likely branches. The remaining gap is execution: the current IR exposes only `BranchEqual64`, whose reference and x64 implementations always execute the delay slot on both predicate outcomes.

Ordinary `BNE` currently reuses `BranchEqual64` by swapping destination PCs. That technique is intentionally not reused for `BNEL`, because destination swapping cannot express which predicate outcome is allowed to execute the annulled delay slot.

## Chosen Architecture

Add two explicit terminator kinds and leave ordinary branch terminators unchanged:

```cpp
enum class R5900IrTerminatorKind {
    Fallthrough = 0,
    BranchEqual64,
    BranchEqualLikely64,
    BranchNotEqualLikely64,
    DirectJump,
    DirectCall,
    IndirectJump,
    IndirectCall,
};
```

This design intentionally avoids refactoring the already validated `BranchEqual64` contract.

`BranchEqualLikely64` directly models `BEQL`.

`BranchNotEqualLikely64` directly models `BNEL`.

Both carry the same structural fields as `BranchEqual64`:

- exactly two GPR inputs
- aligned `taken_pc`
- aligned `fallthrough_pc`
- exactly one lowered delay-slot IR instruction
- no static `target_pc`
- no link state

The delay-slot IR remains part of the terminator even though runtime execution may annul it. This preserves the guest-code fingerprint and allows a cached native block to execute a different branch outcome when GPR values change.

## Architectural Semantics

### BEQL

At terminator guest PC `P`:

```text
condition = GPR[rs].low64 == GPR[rt].low64

if condition:
    execute delay slot exactly once
    next_pc = branch_target
else:
    do not execute delay slot
    next_pc = P + 8
```

### BNEL

At terminator guest PC `P`:

```text
condition = GPR[rs].low64 != GPR[rt].low64

if condition:
    execute delay slot exactly once
    next_pc = branch_target
else:
    do not execute delay slot
    next_pc = P + 8
```

For both instructions, the branch predicate is evaluated from GPR low64 values before any delay-slot execution.

The not-taken path must not call the delay executor at all. Therefore a delay instruction that would mutate registers, write memory, or fail must have no observable effect when the likely branch is not taken.

## IR Validation

`BranchEqualLikely64` and `BranchNotEqualLikely64` use the same structural validation rules:

- terminator guest PC must be 4-byte aligned
- `taken_pc` and `fallthrough_pc` must be 4-byte aligned
- exactly two inputs
- both inputs must be valid GPR operands
- `target_pc == 0`
- `link_pc == 0`
- `link_gpr == 0`
- exactly one valid delay-slot instruction

The validator must reject malformed likely terminators independently of the executor/backend.

## Reference Executor

After successful body execution, the executor evaluates the appropriate predicate.

For a taken likely branch:

1. execute the single delay-slot IR instruction;
2. if delay execution fails, return that failure and do not return a next PC;
3. otherwise return `taken_pc`.

For a not-taken likely branch:

1. skip the delay sequence completely;
2. return `fallthrough_pc` immediately.

The ordinary `BranchEqual64` behavior remains unchanged and continues executing its delay exactly once on both predicate outcomes.

## Windows x86-64 Backend

Introduce a common internal likely-branch emitter parameterized by equality polarity, while keeping the public IR kinds explicit.

Generated control flow conceptually is:

```text
emit body
load lhs.low64
load rhs.low64
cmp lhs, rhs

if predicate is not satisfied:
    skip delay code
    eax = fallthrough_pc
    epilogue
    ret

emit delay code
if delay fails through a helper:
    existing helper failure path returns failure immediately

eax = taken_pc
epilogue
ret
```

For `BranchEqualLikely64`, the not-taken jump is emitted for `lhs != rhs`.

For `BranchNotEqualLikely64`, the not-taken jump is emitted for `lhs == rhs`.

The backend must still allocate a helper frame whenever either the body or delay sequence needs one. Compile-time frame requirements do not depend on which runtime branch outcome is taken.

The not-taken native path must branch around all delay code, including any helper call that would have been emitted for the delay.

## Dispatcher Lowering

The dispatcher recognizes a final analyzer `ConditionalBranch` site only when the decoded instruction is one of the explicitly supported ordinary or likely branches.

### BEQL

Lower as:

```text
kind = BranchEqualLikely64
inputs = [rs, rt]
taken_pc = direct_target(P)
fallthrough_pc = P + 8
```

### BNEL

Lower as:

```text
kind = BranchNotEqualLikely64
inputs = [rs, rt]
taken_pc = direct_target(P)
fallthrough_pc = P + 8
```

Unlike ordinary `BNE`, `BNEL` must not be represented by swapping equal-branch destinations.

The existing analyzer-provided delay site remains part of the guest block and lowering pipeline.

## Cache and Fingerprint Contract

The cache fingerprint continues to include:

```text
all selected body guest words
+ likely branch terminator word
+ architectural delay-slot word
```

Runtime GPR values are not part of the cache key.

Required behavior:

- executing identical guest words with a changed predicate outcome is a cache hit;
- a cached block may run taken on one execution and not-taken on another without recompilation;
- changing the likely branch instruction invalidates/recompiles the cached block;
- changing the delay-slot word invalidates/recompiles the cached block even if a particular execution would annul that delay;
- changing `BEQL` to `BNEL` must invalidate/recompile because the terminator guest word changes.

## Delay-Slot Store Restriction

`SQ` in any dispatcher-managed branch delay remains outside v0 scope.

For `BEQL`/`BNEL`, the dispatcher rejects an `SQ` delay during lowering just as it does for ordinary `BEQ`/`BNE`. The diagnostic should identify the branch family consistently, for example `BEQ/BNE/BEQL/BNEL`.

This restriction applies even to a runtime not-taken likely branch. The dispatcher compiles the complete architectural block before execution and does not predicate lowering eligibility on current register values.

## Failure Ordering

Body failure occurs before predicate evaluation and prevents all terminator behavior.

Taken likely branch:

- body succeeds;
- predicate is satisfied;
- delay executes;
- delay failure is reported and no transfer target is returned.

Not-taken likely branch:

- body succeeds;
- predicate is not satisfied;
- delay is completely annulled;
- no delay-side memory callback or mutation may occur;
- `fallthrough_pc` is returned.

## Testing Strategy

Use strict RED -> GREEN gates.

### IR validation tests

Cover valid and malformed `BranchEqualLikely64` / `BranchNotEqualLikely64` terminators, including invalid GPRs, wrong input count/kind, nonzero static/link fields, misaligned PCs, and missing/multiple delay instructions.

### Reference executor tests

Prove:

- BEQL taken executes delay once and returns target;
- BEQL not-taken skips delay and returns `P+8`;
- BNEL taken executes delay once and returns target;
- BNEL not-taken skips delay and returns `P+8`;
- predicate uses low64 state before delay mutation;
- an annulled delay cannot modify a GPR;
- an annulled memory-failing delay does not invoke/fail the memory helper;
- taken delay failure propagates normally.

### Native x64 differential tests

Compare native and reference state/next-PC/error behavior for all four predicate outcomes. Include at least one delay that mutates one of the branch input GPRs and one helper-capable delay fixture to prove code generation skips the complete delay path when annulled.

### Dispatcher/cache tests

Prove:

- BEQL/BNEL at block entry;
- BEQL/BNEL after straight-line body instructions;
- taken/not-taken next PCs;
- one architectural delay instruction counted only when executed by the native semantic path while guest block fingerprint still covers it;
- cache hit across changed runtime predicate values;
- changed delay word recompiles;
- changed terminator word recompiles;
- BEQL <-> BNEL mutation recompiles;
- `SQ` in likely delay is rejected deterministically.

Dispatcher accounting must follow the project's established convention for `instructions_executed`. If that field represents selected guest words rather than dynamically retired guest instructions, this milestone must preserve that convention rather than silently redefining global accounting semantics. Tests must make the convention explicit.

### Regression suite

All existing 47 tests must remain green, with new dedicated likely-branch tests increasing the total suite count as appropriate.

Windows CI must also pass frame pacing telemetry, 120-frame pacing probe smoke, and analyzer/probe package validation.

## Synthetic Startup Contract

The current synthetic startup remains unchanged for this milestone:

```text
7 blocks
96 guest instructions under current dispatcher accounting
AnalysisFailure at 0x001001cc
```

No branch-likely instruction will be inserted artificially into that fixture solely to increase coverage.

After this milestone, the next execution-driven task should extend executable fixture evidence or run the legal external ELF far enough to identify the next real opcode/runtime requirement beyond the current synthetic endpoint.

## Documentation and Validation Claims

On successful implementation and fresh Windows CI:

- mark `BEQL + BNEL branch-likely v0` as `CI_VALIDATED`;
- retain `READY_FOR_EXTERNAL_VALIDATION` only where external legal-ELF execution is still pending;
- do not claim `EXTERNALLY_VALIDATED` unless the legal external ELF is actually executed through the expanded native path;
- do not claim game boot, graphics, audio, input, menus, or gameplay.

## Completion Criteria

The milestone is complete when:

1. both explicit likely terminator kinds validate correctly;
2. reference execution implements architectural annulment;
3. native x64 matches the reference executor;
4. dispatcher lowers BEQL/BNEL without altering ordinary BEQ/BNE semantics;
5. cache behavior is correct across runtime predicate changes and code mutation;
6. `SQ` likely-delay rejection remains deterministic;
7. existing regressions plus new likely-branch tests pass;
8. pacing and package validation remain green;
9. README/progress documentation reflects the exact validated scope;
10. no external-validation or boot claim is made without evidence.
