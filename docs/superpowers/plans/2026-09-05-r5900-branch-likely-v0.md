# R5900 BEQL + BNEL Branch-Likely v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add native Windows x86-64 execution for R5900 `BEQL` and `BNEL` with architectural delay-slot annulment on the not-taken path, without regressing ordinary BEQ/BNE, cache accounting, or 120 Hz validation.

**Architecture:** Add explicit `BranchEqualLikely64` and `BranchNotEqualLikely64` terminators. The reference executor and x64 backend evaluate the predicate before the delay; taken paths execute exactly one delay instruction, not-taken paths bypass delay code completely. The dispatcher lowers decoded BEQL/BNEL to these terminators, fingerprints branch+delay guest words regardless of runtime outcome, and preserves selected-guest-word `instructions_executed` accounting.

**Tech Stack:** C++20, typed R5900 IR, R5900 reference executor, handwritten Win64 x86-64 emitter, PS2 memory map, CMake/CTest, GitHub Actions Windows CI.

**Spec:** `docs/superpowers/specs/2026-09-05-r5900-branch-likely-v0-design.md`

## Global Constraints

- Base branch: `feature/r5900-beq-delay-slot-v0` at `2d8030558fd0e8059e0f89a1c41da16261bac962`.
- Feature branch: `feature/r5900-branch-likely-v0`.
- Scope: `BEQL` (`0x14`) and `BNEL` (`0x15`) only.
- Out of scope: `BLEZL`, `BGTZL`, `BLTZL`, `BGEZL`, REGIMM likely/link-likely variants, new guest loads/stores, and enabling `SQ` in dispatcher-managed delay slots.
- Taken likely branch: delay executes exactly once.
- Not-taken likely branch: delay produces no register, memory, helper-call, or fault effect.
- Predicate reads GPR low64 before delay execution.
- Existing `BranchEqual64` / BEQ / BNE semantics stay unchanged.
- Runtime GPR values never enter cache fingerprints.
- Branch word and delay word always remain in the cached guest-word set.
- `instructions_executed` remains selected-guest-word accounting, not dynamic retirement.
- Synthetic startup remains `7 blocks / 96 selected guest words / AnalysisFailure @ 0x001001cc`.
- Final Windows suite target count: 51 tests.
- 120 Hz pacing must remain approximately 8.333 ms with zero samples above 9/10/12 ms in CI telemetry.
- Do not claim external ELF validation or game boot without direct evidence.

---

## File Map

**Create**

- `tests/r5900_branch_likely_test_support.h`
- `tests/r5900_ir_branch_likely_validation_tests.cpp`
- `tests/r5900_ir_branch_likely_executor_tests.cpp`
- `tests/r5900_x64_branch_likely_windows_tests.cpp`
- `tests/r5900_block_dispatcher_branch_likely_windows_tests.cpp`

**Modify**

- `src/recompiler/r5900_ir.h`
- `src/recompiler/r5900_ir_validation.cpp`
- `src/recompiler/r5900_ir_executor.cpp`
- `src/recompiler/windows/r5900_x64_backend.cpp`
- `src/recompiler/windows/r5900_block_dispatcher.cpp`
- `CMakeLists.txt`
- `README.md`
- `PROGRESS.md`

`src/recompiler/r5900_decoder.cpp` and `src/analysis/r5900_control_flow.cpp` require no production changes: decoding already marks BEQL/BNEL as likely branches and analysis already records that the fallthrough path annuls the architectural delay slot.

---

### Task 1: IR terminators and validation

**Files:**
- Create: `tests/r5900_branch_likely_test_support.h`
- Create: `tests/r5900_ir_branch_likely_validation_tests.cpp`
- Modify: `src/recompiler/r5900_ir.h`
- Modify: `src/recompiler/r5900_ir_validation.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces `R5900IrTerminatorKind::BranchEqualLikely64` and `BranchNotEqualLikely64`.
- Produces test builders `branch_equal_likely(...)` and `branch_not_equal_likely(...)`.

- [ ] **Step 1: Create branch-likely test builders before production symbols exist**

Create `tests/r5900_branch_likely_test_support.h`:

```cpp
#pragma once
#include "r5900_direct_transfer_test_support.h"

namespace b3r::test_support {

inline R5900IrBlock branch_equal_likely(std::uint32_t pc,
                                        std::uint8_t rs,
                                        std::uint8_t rt,
                                        std::uint32_t target,
                                        R5900IrInstruction delay) {
    R5900IrBlock block{};
    block.terminator.guest_pc = pc;
    block.terminator.kind = R5900IrTerminatorKind::BranchEqualLikely64;
    block.terminator.inputs = {gpr(rs), gpr(rt)};
    block.terminator.taken_pc = target;
    block.terminator.fallthrough_pc = pc + 8u;
    block.terminator.delay_slot = {delay};
    return block;
}

inline R5900IrBlock branch_not_equal_likely(std::uint32_t pc,
                                            std::uint8_t rs,
                                            std::uint8_t rt,
                                            std::uint32_t target,
                                            R5900IrInstruction delay) {
    auto block = branch_equal_likely(pc, rs, rt, target, delay);
    block.terminator.kind = R5900IrTerminatorKind::BranchNotEqualLikely64;
    return block;
}

} // namespace b3r::test_support
```

- [ ] **Step 2: Create validation RED**

Create `tests/r5900_ir_branch_likely_validation_tests.cpp` with standard `fail`, `expect`, and `expect_malformed` helpers, then assert this matrix:

```cpp
const auto beql = branch_equal_likely(
    0x0010b000u, 4u, 5u, 0x0010b100u, nop(0x0010b004u));
const auto bnel = branch_not_equal_likely(
    0x0010b200u, 6u, 7u, 0x0010b300u, nop(0x0010b204u));

expect(validate_r5900_ir_block(beql).ok(), "valid BEQL IR must validate");
expect(validate_r5900_ir_block(bnel).ok(), "valid BNEL IR must validate");

{
    auto invalid = beql;
    invalid.terminator.inputs.clear();
    expect_malformed(invalid, "missing likely inputs must fail");
}
{
    auto invalid = bnel;
    invalid.terminator.inputs.push_back(gpr(8u));
    expect_malformed(invalid, "extra likely input must fail");
}
{
    auto invalid = beql;
    invalid.terminator.inputs[0].kind = R5900IrOperandKind::Immediate;
    expect_malformed(invalid, "non-GPR likely input must fail");
}
{
    auto invalid = bnel;
    invalid.terminator.inputs[1].gpr_index = 32u;
    expect(validate_r5900_ir_block(invalid).error ==
               R5900IrValidationError::InvalidRegister,
           "out-of-range likely GPR must fail");
}
{
    auto invalid = beql;
    invalid.terminator.target_pc = 0x0010b400u;
    expect_malformed(invalid, "likely target_pc must be zero");
}
{
    auto invalid = bnel;
    invalid.terminator.link_pc = 0x0010b208u;
    expect_malformed(invalid, "likely link_pc must be zero");
}
{
    auto invalid = beql;
    invalid.terminator.link_gpr = 1u;
    expect_malformed(invalid, "likely link_gpr must be zero");
}
{
    auto invalid = bnel;
    invalid.terminator.taken_pc |= 2u;
    expect_malformed(invalid, "unaligned likely taken_pc must fail");
}
{
    auto invalid = beql;
    invalid.terminator.fallthrough_pc |= 2u;
    expect_malformed(invalid, "unaligned likely fallthrough_pc must fail");
}
{
    auto invalid = bnel;
    invalid.terminator.delay_slot.clear();
    expect_malformed(invalid, "missing likely delay must fail");
}
{
    auto invalid = beql;
    invalid.terminator.delay_slot.push_back(nop(0x0010b008u));
    expect_malformed(invalid, "multiple likely delay instructions must fail");
}
{
    auto invalid = bnel;
    invalid.terminator.delay_slot.front().opcode =
        static_cast<R5900IrOpcode>(0xffu);
    expect(validate_r5900_ir_block(invalid).error ==
               R5900IrValidationError::UnsupportedOpcode,
           "invalid likely delay opcode must propagate");
}
```

Register:

```cmake
add_executable(r5900_ir_branch_likely_validation_tests
  tests/r5900_ir_branch_likely_validation_tests.cpp)
target_link_libraries(r5900_ir_branch_likely_validation_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_branch_likely_validation_tests
  COMMAND r5900_ir_branch_likely_validation_tests)
```

- [ ] **Step 3: Run RED**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target r5900_ir_branch_likely_validation_tests
```

Expected: compilation fails only because the two new terminator enum values do not exist.

- [ ] **Step 4: Add enum values**

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

- [ ] **Step 5: Add validator cases**

Add this shared case after the existing `BranchEqual64` case:

```cpp
case R5900IrTerminatorKind::BranchEqualLikely64:
case R5900IrTerminatorKind::BranchNotEqualLikely64:
    if ((terminator.taken_pc & 0x3u) != 0u ||
        (terminator.fallthrough_pc & 0x3u) != 0u ||
        terminator.target_pc != 0u ||
        terminator.link_pc != 0u ||
        terminator.link_gpr != 0u ||
        terminator.inputs.size() != 2u ||
        terminator.inputs[0].kind != R5900IrOperandKind::Gpr ||
        terminator.inputs[1].kind != R5900IrOperandKind::Gpr) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       terminator_index,
                       terminator.guest_pc,
                       "malformed likely-branch terminator");
    }
    for (const auto& operand : terminator.inputs) {
        const auto valid =
            validate_operand(operand, terminator_index, terminator.guest_pc);
        if (!valid.ok()) return valid;
    }
    return validate_single_delay_slot(terminator, terminator_index);
```

- [ ] **Step 6: Run GREEN and validation regressions**

```powershell
cmake --build build --config Release --target r5900_ir_branch_likely_validation_tests
ctest --test-dir build -C Release -R "^r5900_ir_branch_likely_validation_tests$" --output-on-failure
ctest --test-dir build -C Release -R "r5900_ir_(block_|direct_transfer_|indirect_transfer_)?validation_tests" --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/recompiler/r5900_ir.h src/recompiler/r5900_ir_validation.cpp tests/r5900_branch_likely_test_support.h tests/r5900_ir_branch_likely_validation_tests.cpp
git commit -m "feat: add R5900 likely branch IR"
```

---

### Task 2: Reference executor annulment

**Files:**
- Create: `tests/r5900_ir_branch_likely_executor_tests.cpp`
- Modify: `src/recompiler/r5900_ir_executor.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes Task 1 terminators/builders.
- Produces reference state/next-PC/fault behavior for Task 3 differential tests.

- [ ] **Step 1: Register executor target**

```cmake
add_executable(r5900_ir_branch_likely_executor_tests
  tests/r5900_ir_branch_likely_executor_tests.cpp)
target_link_libraries(r5900_ir_branch_likely_executor_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_branch_likely_executor_tests
  COMMAND r5900_ir_branch_likely_executor_tests)
```

- [ ] **Step 2: Create concrete memory-probe harness**

Use:

```cpp
struct MemoryProbe {
    bool succeed{true};
    std::size_t calls{};
};

bool write128(void* user,
              std::uint32_t,
              std::uint64_t,
              std::uint64_t) noexcept {
    auto& probe = *static_cast<MemoryProbe*>(user);
    ++probe.calls;
    return probe.succeed;
}

R5900IrExecutionContext context_for(R5900IrExecutionState& state,
                                    MemoryProbe& probe) {
    R5900IrExecutionContext context{};
    context.state = &state;
    context.memory.user = &probe;
    context.memory.write128 = &write128;
    return context;
}
```

- [ ] **Step 3: Write all four predicate outcome RED cases**

```cpp
{
    auto block = branch_equal_likely(
        0x0010c000u, 4u, 5u, 0x0010c100u,
        addiu(8u, 8u, 1, 0x0010c004u));
    R5900IrExecutionState state{};
    state.gpr[4].low64 = 7u;
    state.gpr[5].low64 = 7u;
    const auto result = execute_r5900_ir_block(block, state);
    expect(result.ok() && result.next_pc == 0x0010c100u && state.gpr[8].low64 == 1u,
           "BEQL taken must execute delay");
}
{
    auto block = branch_equal_likely(
        0x0010c000u, 4u, 5u, 0x0010c100u,
        addiu(8u, 8u, 1, 0x0010c004u));
    R5900IrExecutionState state{};
    state.gpr[4].low64 = 7u;
    state.gpr[5].low64 = 8u;
    const auto result = execute_r5900_ir_block(block, state);
    expect(result.ok() && result.next_pc == 0x0010c008u && state.gpr[8].low64 == 0u,
           "BEQL not-taken must annul delay");
}
{
    auto block = branch_not_equal_likely(
        0x0010c200u, 4u, 5u, 0x0010c300u,
        addiu(8u, 8u, 1, 0x0010c204u));
    R5900IrExecutionState state{};
    state.gpr[4].low64 = 7u;
    state.gpr[5].low64 = 8u;
    const auto result = execute_r5900_ir_block(block, state);
    expect(result.ok() && result.next_pc == 0x0010c300u && state.gpr[8].low64 == 1u,
           "BNEL taken must execute delay");
}
{
    auto block = branch_not_equal_likely(
        0x0010c200u, 4u, 5u, 0x0010c300u,
        addiu(8u, 8u, 1, 0x0010c204u));
    R5900IrExecutionState state{};
    state.gpr[4].low64 = 7u;
    state.gpr[5].low64 = 7u;
    const auto result = execute_r5900_ir_block(block, state);
    expect(result.ok() && result.next_pc == 0x0010c208u && state.gpr[8].low64 == 0u,
           "BNEL not-taken must annul delay");
}
```

- [ ] **Step 4: Write predicate-order and helper-annulment RED cases**

```cpp
{
    auto block = branch_equal_likely(
        0x0010c400u, 4u, 5u, 0x0010c500u,
        addiu(4u, 0u, 0, 0x0010c404u));
    R5900IrExecutionState state{};
    state.gpr[4].low64 = 9u;
    state.gpr[5].low64 = 9u;
    const auto result = execute_r5900_ir_block(block, state);
    expect(result.ok() && result.next_pc == 0x0010c500u && state.gpr[4].low64 == 0u,
           "predicate must be decided before delay mutates lhs");
}
{
    auto block = branch_equal_likely(
        0x0010c600u, 4u, 5u, 0x0010c700u,
        store128(2u, 3u, 0, 0x0010c604u));
    R5900IrExecutionState state{};
    state.gpr[4].low64 = 1u;
    state.gpr[5].low64 = 2u;
    state.gpr[2].low64 = 0x00004017u;
    MemoryProbe probe{false, 0u};
    auto context = context_for(state, probe);
    const auto result = execute_r5900_ir_block(block, context);
    expect(result.ok() && result.next_pc == 0x0010c608u,
           "annulled BEQL helper delay must return fallthrough");
    expect(probe.calls == 0u && !context.memory_fault.active,
           "annulled BEQL helper delay must never call helper");
}
{
    auto block = branch_not_equal_likely(
        0x0010c800u, 4u, 5u, 0x0010c900u,
        store128(2u, 3u, 0, 0x0010c804u));
    R5900IrExecutionState state{};
    state.gpr[4].low64 = 1u;
    state.gpr[5].low64 = 2u;
    state.gpr[2].low64 = 0x00005017u;
    MemoryProbe probe{false, 0u};
    auto context = context_for(state, probe);
    const auto result = execute_r5900_ir_block(block, context);
    expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
           "taken BNEL failing helper must propagate fault");
    expect(probe.calls == 1u && context.memory_fault.active &&
               context.memory_fault.guest_pc == 0x0010c804u,
           "taken BNEL helper fault must identify delay PC");
}
```

- [ ] **Step 5: Run RED**

```powershell
cmake --build build --config Release --target r5900_ir_branch_likely_executor_tests
ctest --test-dir build -C Release -R "^r5900_ir_branch_likely_executor_tests$" --output-on-failure
```

Expected: runtime failure because executor switch has no likely terminator case.

- [ ] **Step 6: Implement reference semantics**

```cpp
case R5900IrTerminatorKind::BranchEqualLikely64:
case R5900IrTerminatorKind::BranchNotEqualLikely64: {
    const bool equal =
        state.gpr[block.terminator.inputs[0].gpr_index].low64 ==
        state.gpr[block.terminator.inputs[1].gpr_index].low64;
    const bool taken =
        block.terminator.kind == R5900IrTerminatorKind::BranchEqualLikely64
            ? equal
            : !equal;
    if (!taken) {
        return {R5900IrExecutionError::None, {}, block.terminator.fallthrough_pc};
    }
    const auto delay_result = execute_ir_sequence(block.terminator.delay_slot, context);
    if (!delay_result.ok()) return map_block_execution_failure(delay_result);
    return {R5900IrExecutionError::None, {}, block.terminator.taken_pc};
}
```

Do not alter `BranchEqual64`.

- [ ] **Step 7: Run GREEN and executor regressions**

```powershell
cmake --build build --config Release --target r5900_ir_branch_likely_executor_tests
ctest --test-dir build -C Release -R "^r5900_ir_branch_likely_executor_tests$" --output-on-failure
ctest --test-dir build -C Release -R "r5900_ir_.*executor_tests" --output-on-failure
```

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt src/recompiler/r5900_ir_executor.cpp tests/r5900_ir_branch_likely_executor_tests.cpp
git commit -m "feat: execute R5900 branch-likely IR"
```

---

### Task 3: Native Win64 x64 branch-likely backend

**Files:**
- Create: `tests/r5900_x64_branch_likely_windows_tests.cpp`
- Modify: `src/recompiler/windows/r5900_x64_backend.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes Task 2 reference executor as oracle.
- Produces native code for both likely terminators.

- [ ] **Step 1: Register native differential test**

```cmake
add_executable(r5900_x64_branch_likely_windows_tests
  tests/r5900_x64_branch_likely_windows_tests.cpp)
target_link_libraries(r5900_x64_branch_likely_windows_tests PRIVATE b3r_recompiler_x64)
add_test(NAME r5900_x64_branch_likely_windows_tests
  COMMAND r5900_x64_branch_likely_windows_tests)
```

- [ ] **Step 2: Create differential helper**

In the test file, define `MemoryProbe`, `write128`, `context_for`, and `expect_states_equal`. Then use:

```cpp
void run_differential(const R5900IrBlock& block,
                      const R5900IrExecutionState& initial,
                      bool memory_succeeds,
                      std::size_t expected_helper_calls,
                      const char* message) {
    auto reference_state = initial;
    auto native_state = initial;
    MemoryProbe reference_probe{memory_succeeds, 0u};
    MemoryProbe native_probe{memory_succeeds, 0u};
    auto reference_context = context_for(reference_state, reference_probe);
    auto native_context = context_for(native_state, native_probe);

    const auto reference = execute_r5900_ir_block(block, reference_context);
    auto compiled = compile_r5900_ir_x64(block);
    expect(compiled.ok() && compiled.block.has_value(),
           "likely block must compile natively");
    const auto native = compiled.block->execute(native_context);

    expect(reference.error == native.error, "likely reference/native error mismatch");
    if (reference.ok()) {
        expect(reference.next_pc == native.next_pc,
               "likely reference/native next-PC mismatch");
    }
    expect_states_equal(reference_state, native_state, message);
    expect(reference_probe.calls == expected_helper_calls &&
               native_probe.calls == expected_helper_calls,
           message);
    expect(reference_context.memory_fault.active == native_context.memory_fault.active,
           message);
    if (reference_context.memory_fault.active) {
        expect(reference_context.memory_fault.guest_pc == native_context.memory_fault.guest_pc &&
                   reference_context.memory_fault.address == native_context.memory_fault.address &&
                   reference_context.memory_fault.width_bytes == native_context.memory_fault.width_bytes,
               message);
    }
}
```

- [ ] **Step 3: Add explicit native RED fixtures**

```cpp
R5900IrExecutionState equal{};
equal.gpr[4].low64 = 7u;
equal.gpr[5].low64 = 7u;
run_differential(
    branch_equal_likely(0x0010d000u, 4u, 5u, 0x0010d100u,
                        addiu(8u, 8u, 1, 0x0010d004u)),
    equal, true, 0u, "BEQL taken differential mismatch");

R5900IrExecutionState unequal{};
unequal.gpr[4].low64 = 7u;
unequal.gpr[5].low64 = 8u;
run_differential(
    branch_equal_likely(0x0010d200u, 4u, 5u, 0x0010d300u,
                        addiu(8u, 8u, 1, 0x0010d204u)),
    unequal, true, 0u, "BEQL annulled differential mismatch");
run_differential(
    branch_not_equal_likely(0x0010d400u, 4u, 5u, 0x0010d500u,
                            addiu(8u, 8u, 1, 0x0010d404u)),
    unequal, true, 0u, "BNEL taken differential mismatch");
run_differential(
    branch_not_equal_likely(0x0010d600u, 4u, 5u, 0x0010d700u,
                            addiu(8u, 8u, 1, 0x0010d604u)),
    equal, true, 0u, "BNEL annulled differential mismatch");
```

Predicate-before-delay + body:

```cpp
auto ordered = branch_equal_likely(
    0x0010d800u, 4u, 5u, 0x0010d900u,
    addiu(4u, 0u, 0, 0x0010d804u));
ordered.body = {addiu(24u, 0u, 0x55, 0x0010d7fcu)};
run_differential(ordered, equal, true, 0u,
                 "likely body/predicate ordering mismatch");
```

Annulled helper paths:

```cpp
R5900IrExecutionState beql_annul = unequal;
beql_annul.gpr[2].low64 = 0x00006017u;
run_differential(
    branch_equal_likely(0x0010da00u, 4u, 5u, 0x0010db00u,
                        store128(2u, 3u, 0, 0x0010da04u)),
    beql_annul, false, 0u, "annulled BEQL helper must be bypassed");

R5900IrExecutionState bnel_annul = equal;
bnel_annul.gpr[2].low64 = 0x00007017u;
run_differential(
    branch_not_equal_likely(0x0010dc00u, 4u, 5u, 0x0010dd00u,
                            store128(2u, 3u, 0, 0x0010dc04u)),
    bnel_annul, false, 0u, "annulled BNEL helper must be bypassed");
```

Taken helper success/failure:

```cpp
R5900IrExecutionState beql_store = equal;
beql_store.gpr[2].low64 = 0x00008017u;
run_differential(
    branch_equal_likely(0x0010de00u, 4u, 5u, 0x0010df00u,
                        store128(2u, 3u, 0, 0x0010de04u)),
    beql_store, true, 1u, "taken BEQL helper success mismatch");

R5900IrExecutionState bnel_store = unequal;
bnel_store.gpr[2].low64 = 0x00009017u;
run_differential(
    branch_not_equal_likely(0x0010e000u, 4u, 5u, 0x0010e100u,
                            store128(2u, 3u, 0, 0x0010e004u)),
    bnel_store, false, 1u, "taken BNEL helper failure mismatch");
```

- [ ] **Step 4: Run RED**

```powershell
cmake --build build --config Release --target r5900_x64_branch_likely_windows_tests
ctest --test-dir build -C Release -R "^r5900_x64_branch_likely_windows_tests$" --output-on-failure
```

Expected: compile result reports unsupported block terminator.

- [ ] **Step 5: Add shared native emitter**

```cpp
enum class LikelyBranchPolarity { Equal, NotEqual };

PendingX64Code compile_likely_branch_code(
    const R5900IrBlock& block,
    LikelyBranchPolarity polarity) {
    const bool helper_frame =
        sequence_needs_helper(block.body) ||
        sequence_needs_helper(block.terminator.delay_slot);

    std::vector<std::uint8_t> bytes;
    bytes.reserve(160u + block.body.size() * 128u +
                  block.terminator.delay_slot.size() * 256u);
    if (helper_frame) emit_helper_frame_prologue(bytes);
    emit_zero_gpr0(bytes);

    const auto body = emit_ir_sequence(bytes, block.body, 0u, helper_frame);
    if (!body.ok()) return pending_failure(body.error, body.message);

    emit_load_rax_from_state(
        bytes, gpr_low64_offset(block.terminator.inputs[0].gpr_index));
    emit_load_rdx_from_state(
        bytes, gpr_low64_offset(block.terminator.inputs[1].gpr_index));
    bytes.insert(bytes.end(), {0x48u, 0x39u, 0xd0u}); // cmp rax,rdx

    // Predicate false -> skip the entire emitted delay path.
    bytes.insert(bytes.end(), {
        0x0fu,
        polarity == LikelyBranchPolarity::Equal ? 0x85u : 0x84u,
    });
    const auto not_taken_rel32 = bytes.size();
    emit_u32(bytes, 0u);

    emit_zero_gpr0(bytes);
    const auto delay = emit_ir_sequence(
        bytes, block.terminator.delay_slot, block.body.size() + 1u, helper_frame);
    if (!delay.ok()) return pending_failure(delay.error, delay.message);
    emit_zero_gpr0(bytes);
    emit_mov_eax_imm32(bytes, block.terminator.taken_pc);
    if (helper_frame) emit_helper_frame_epilogue(bytes);
    bytes.push_back(0xc3u);

    const auto not_taken_offset = bytes.size();
    const auto branch_end = not_taken_rel32 + sizeof(std::uint32_t);
    const auto displacement =
        static_cast<std::int64_t>(not_taken_offset) -
        static_cast<std::int64_t>(branch_end);
    if (displacement < std::numeric_limits<std::int32_t>::min() ||
        displacement > std::numeric_limits<std::int32_t>::max()) {
        return pending_failure(R5900X64CompileError::UnsupportedOpcode,
                               "R5900 x64 likely-branch native path exceeds rel32 range");
    }
    patch_u32(bytes, not_taken_rel32,
              static_cast<std::uint32_t>(static_cast<std::int32_t>(displacement)));

    emit_zero_gpr0(bytes);
    emit_mov_eax_imm32(bytes, block.terminator.fallthrough_pc);
    if (helper_frame) emit_helper_frame_epilogue(bytes);
    bytes.push_back(0xc3u);
    return publish_code(bytes);
}
```

`0x85` is `JNE` for BEQL not-taken; `0x84` is `JE` for BNEL not-taken.

- [ ] **Step 6: Wire compile switch**

```cpp
case R5900IrTerminatorKind::BranchEqualLikely64:
    pending = compile_likely_branch_code(block, LikelyBranchPolarity::Equal);
    break;
case R5900IrTerminatorKind::BranchNotEqualLikely64:
    pending = compile_likely_branch_code(block, LikelyBranchPolarity::NotEqual);
    break;
```

Leave `BranchEqual64` on `compile_branch_equal_code`.

- [ ] **Step 7: Run GREEN and x64 regressions**

```powershell
cmake --build build --config Release --target r5900_x64_branch_likely_windows_tests
ctest --test-dir build -C Release -R "^r5900_x64_branch_likely_windows_tests$" --output-on-failure
ctest --test-dir build -C Release -R "^r5900_x64_.*windows_tests$" --output-on-failure
```

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt src/recompiler/windows/r5900_x64_backend.cpp tests/r5900_x64_branch_likely_windows_tests.cpp
git commit -m "feat: compile R5900 likely branches on x64"
```

---

### Task 4: Dispatcher, cache, accounting, and SQ-delay

**Files:**
- Create: `tests/r5900_block_dispatcher_branch_likely_windows_tests.cpp`
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes analyzer `ConditionalBranch`, decoded `Beql`/`Bnel`, and Task 3 native terminators.
- Produces dispatcher/cache behavior with unchanged selected-guest-word accounting.

- [ ] **Step 1: Register dispatcher test and make it self-contained**

```cmake
add_executable(r5900_block_dispatcher_branch_likely_windows_tests
  tests/r5900_block_dispatcher_branch_likely_windows_tests.cpp)
target_link_libraries(r5900_block_dispatcher_branch_likely_windows_tests PRIVATE
  b3r_recompiler_dispatcher_x64)
add_test(NAME r5900_block_dispatcher_branch_likely_windows_tests
  COMMAND r5900_block_dispatcher_branch_likely_windows_tests)
```

At the top of the test file include:

```cpp
#include "recompiler/ps2_elf.h"
#include "recompiler/windows/r5900_block_dispatcher.h"
#include "runtime/ps2_memory_map.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>
```

Use these exact local helpers so the test has no dependency on another test translation unit:

```cpp
using Bytes = std::vector<std::uint8_t>;

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}
void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

b3r::runtime::Ps2MemoryMap make_memory(const std::vector<std::uint32_t>& words,
                                       std::uint32_t base,
                                       std::uint32_t flags = 5u) {
    constexpr std::uint32_t ph = 52u;
    constexpr std::uint32_t payload = 0x100u;
    const auto payload_size = static_cast<std::uint32_t>(words.size() * 4u);
    Bytes bytes(static_cast<std::size_t>(payload + payload_size + 0x40u), 0u);
    bytes[0] = 0x7fu; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
    bytes[4] = 1u; bytes[5] = 1u; bytes[6] = 1u;
    put_u16(bytes, 16u, 2u); put_u16(bytes, 18u, 8u);
    put_u32(bytes, 20u, 1u); put_u32(bytes, 24u, base);
    put_u32(bytes, 28u, ph); put_u16(bytes, 40u, 52u);
    put_u16(bytes, 42u, 32u); put_u16(bytes, 44u, 1u);
    put_u32(bytes, ph + 0u, 1u); put_u32(bytes, ph + 4u, payload);
    put_u32(bytes, ph + 8u, base); put_u32(bytes, ph + 12u, base);
    put_u32(bytes, ph + 16u, payload_size); put_u32(bytes, ph + 20u, payload_size);
    put_u32(bytes, ph + 24u, flags); put_u32(bytes, ph + 28u, 0x1000u);
    for (std::size_t i = 0; i < words.size(); ++i) {
        put_u32(bytes, static_cast<std::size_t>(payload) + i * 4u, words[i]);
    }
    auto parsed = b3r::recompiler::parse_ps2_elf(bytes);
    expect(parsed.ok(), "likely dispatcher ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "likely dispatcher memory must map");
    return std::move(*built.memory);
}

constexpr std::uint32_t i_type(std::uint8_t op,
                               std::uint8_t rs,
                               std::uint8_t rt,
                               std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) | imm;
}
```

Also define `fail()` and `expect()` before `make_memory()`.

- [ ] **Step 2: Write four dispatcher outcome RED cases with selected-word accounting**

Use:

```cpp
constexpr std::uint32_t base = 0x00100000u;
const auto beql = i_type(0x14u, 1u, 2u, 2u);
const auto bnel = i_type(0x15u, 1u, 2u, 2u);
const auto delay = i_type(0x09u, 3u, 3u, 1u);
```

Assertions:

```text
BEQL equal:   next_pc=base+12, r3=1, blocks=1, instructions=2
BEQL unequal: next_pc=base+8,  r3=0, blocks=1, instructions=2
BNEL unequal: next_pc=base+12, r3=1, blocks=1, instructions=2
BNEL equal:   next_pc=base+8,  r3=0, blocks=1, instructions=2
```

Each result reason must be `BlockBudgetExhausted`. The not-taken cases deliberately still assert `instructions_executed == 2u`.

- [ ] **Step 3: Add body and cache RED cases**

BEQL after body:

```cpp
const auto body = i_type(0x09u, 0u, 10u, 0x55u);
auto memory = make_memory({body, beql, delay, 0u, 0u}, base);
b3r::recompiler::R5900BlockDispatcher dispatcher(memory);
b3r::recompiler::R5900IrExecutionState state{};
state.gpr[1].low64 = 1u;
state.gpr[2].low64 = 2u;
const auto result = dispatcher.run(base, state, 1u);
expect(result.next_pc == base + 12u && result.instructions_executed == 3u &&
           state.gpr[10].low64 == 0x55u && state.gpr[3].low64 == 0u,
       "BEQL after body must annul delay but preserve body");
```

BEQL cache predicate flip:

```cpp
auto memory = make_memory({beql, delay, 0u, 0u}, base);
b3r::recompiler::R5900BlockDispatcher dispatcher(memory);
b3r::recompiler::R5900IrExecutionState first{};
first.gpr[1].low64 = 4u; first.gpr[2].low64 = 4u;
const auto a = dispatcher.run(base, first, 1u);
expect(a.cache_misses == 1u && first.gpr[3].low64 == 1u,
       "first BEQL must compile taken path");
b3r::recompiler::R5900IrExecutionState second{};
second.gpr[1].low64 = 4u; second.gpr[2].low64 = 5u;
const auto b = dispatcher.run(base, second, 1u);
expect(b.cache_hits == 1u && b.recompilations == 0u &&
           b.next_pc == base + 8u && second.gpr[3].low64 == 0u &&
           dispatcher.cache_size() == 1u,
       "cached BEQL must change outcome without recompiling");
```

BNEL cache predicate flip:

```cpp
auto memory = make_memory({bnel, delay, 0u, 0u}, base);
b3r::recompiler::R5900BlockDispatcher dispatcher(memory);
b3r::recompiler::R5900IrExecutionState first{};
first.gpr[1].low64 = 9u; first.gpr[2].low64 = 9u;
const auto a = dispatcher.run(base, first, 1u);
expect(a.cache_misses == 1u && a.next_pc == base + 8u && first.gpr[3].low64 == 0u,
       "first BNEL equal path must annul delay");
b3r::recompiler::R5900IrExecutionState second{};
second.gpr[1].low64 = 9u; second.gpr[2].low64 = 8u;
const auto b = dispatcher.run(base, second, 1u);
expect(b.cache_hits == 1u && b.recompilations == 0u &&
           b.next_pc == base + 12u && second.gpr[3].low64 == 1u,
       "cached BNEL must become taken without recompiling");
```

- [ ] **Step 4: Add delay and terminator mutation RED cases**

Delay mutation:

```cpp
const auto delay_two = i_type(0x09u, 3u, 3u, 2u);
auto memory = make_memory({beql, delay, 0u, 0u}, base);
b3r::recompiler::R5900BlockDispatcher dispatcher(memory);
b3r::recompiler::R5900IrExecutionState first{};
first.gpr[1].low64 = 6u; first.gpr[2].low64 = 6u;
const auto a = dispatcher.run(base, first, 1u);
expect(a.cache_misses == 1u && first.gpr[3].low64 == 1u,
       "BEQL warm delay must execute");
expect(memory.write_u32(base + 4u, delay_two), "delay mutation must write");
b3r::recompiler::R5900IrExecutionState second{};
second.gpr[1].low64 = 6u; second.gpr[2].low64 = 6u;
const auto b = dispatcher.run(base, second, 1u);
expect(b.recompilations == 1u && b.cache_hits == 0u && second.gpr[3].low64 == 2u,
       "changed likely delay must recompile");
```

BEQL -> BNEL mutation:

```cpp
auto memory = make_memory({beql, delay, 0u, 0u}, base);
b3r::recompiler::R5900BlockDispatcher dispatcher(memory);
b3r::recompiler::R5900IrExecutionState first{};
first.gpr[1].low64 = 10u; first.gpr[2].low64 = 10u;
const auto a = dispatcher.run(base, first, 1u);
expect(a.cache_misses == 1u && a.next_pc == base + 12u,
       "BEQL warm branch must be taken");
expect(memory.write_u32(base, bnel), "BEQL-to-BNEL mutation must write");
b3r::recompiler::R5900IrExecutionState second{};
second.gpr[1].low64 = 10u; second.gpr[2].low64 = 10u;
const auto b = dispatcher.run(base, second, 1u);
expect(b.recompilations == 1u && b.next_pc == base + 8u && second.gpr[3].low64 == 0u,
       "BEQL-to-BNEL mutation must recompile with BNEL annulment");
```

- [ ] **Step 5: Add SQ-delay RED for BEQL and BNEL**

```cpp
const auto sq_delay = i_type(0x1fu, 2u, 3u, 0u);
```

For BEQL use unequal `r1/r2`; for BNEL use equal `r1/r2`. Both must assert:

```cpp
expect(result.reason == b3r::recompiler::R5900DispatchStopReason::LoweringFailure,
       "SQ in likely delay must fail during lowering");
expect(result.blocks_executed == 0u && result.instructions_executed == 0u,
       "SQ likely-delay rejection must commit no progress");
expect(result.message.find("BEQ/BNE/BEQL/BNEL") != std::string::npos,
       "SQ likely-delay message must identify branch family");
```

- [ ] **Step 6: Run dispatcher RED**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_branch_likely_windows_tests
ctest --test-dir build -C Release -R "^r5900_block_dispatcher_branch_likely_windows_tests$" --output-on-failure
```

Expected: BEQL/BNEL remain control-flow boundaries.

- [ ] **Step 7: Add supported-transfer recognition**

```cpp
const bool has_supported_beql =
    block.end_kind == analysis::R5900BlockEndKind::ConditionalBranch &&
    !block.instructions.empty() &&
    block.instructions.back().decoded.instruction == R5900Instruction::Beql;
const bool has_supported_bnel =
    block.end_kind == analysis::R5900BlockEndKind::ConditionalBranch &&
    !block.instructions.empty() &&
    block.instructions.back().decoded.instruction == R5900Instruction::Bnel;

const bool has_supported_transfer =
    has_supported_beq || has_supported_bne ||
    has_supported_beql || has_supported_bnel ||
    has_supported_j || has_supported_jal ||
    has_supported_jr || has_supported_jalr;
```

- [ ] **Step 8: Lower BEQL/BNEL to explicit terminators**

Include BEQL/BNEL in the direct-target gate. Then use:

```cpp
if (has_supported_beq || has_supported_bne ||
    has_supported_beql || has_supported_bnel) {
    ir_block.terminator.inputs = {
        dispatcher_gpr(transfer_site->decoded.rs),
        dispatcher_gpr(transfer_site->decoded.rt),
    };
    if (has_supported_beq) {
        ir_block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;
        ir_block.terminator.taken_pc = *target;
        ir_block.terminator.fallthrough_pc = transfer_site->pc + 8u;
    } else if (has_supported_bne) {
        ir_block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;
        ir_block.terminator.taken_pc = transfer_site->pc + 8u;
        ir_block.terminator.fallthrough_pc = *target;
    } else if (has_supported_beql) {
        ir_block.terminator.kind = R5900IrTerminatorKind::BranchEqualLikely64;
        ir_block.terminator.taken_pc = *target;
        ir_block.terminator.fallthrough_pc = transfer_site->pc + 8u;
    } else {
        ir_block.terminator.kind = R5900IrTerminatorKind::BranchNotEqualLikely64;
        ir_block.terminator.taken_pc = *target;
        ir_block.terminator.fallthrough_pc = transfer_site->pc + 8u;
    }
}
```

BNEL must not invert PCs.

- [ ] **Step 9: Extend SQ branch-family message**

```cpp
(has_supported_beq || has_supported_bne ||
 has_supported_beql || has_supported_bnel)
    ? "SQ in a BEQ/BNE/BEQL/BNEL delay slot is outside dispatcher v0 scope"
```

- [ ] **Step 10: Run GREEN, dispatcher regressions, and startup**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_branch_likely_windows_tests
ctest --test-dir build -C Release -R "^r5900_block_dispatcher_branch_likely_windows_tests$" --output-on-failure
ctest --test-dir build -C Release -R "^r5900_block_dispatcher_.*windows_tests$" --output-on-failure
.\build\Release\r5900_block_dispatcher_startup_windows_tests.exe
```

Expected: all dispatcher tests pass; startup remains 7/96 and stops with AnalysisFailure at `0x001001cc`.

- [ ] **Step 11: Commit**

```bash
git add CMakeLists.txt src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_branch_likely_windows_tests.cpp
git commit -m "feat: dispatch R5900 BEQL and BNEL"
```

---

### Task 5: Full verification, documentation, and integration readiness

**Files:**
- Modify: `README.md`
- Modify: `PROGRESS.md`
- Review all feature changes from base SHA.

- [ ] **Step 1: Run full 51-test Windows suite**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Required:

```text
100% tests passed, 0 tests failed out of 51
```

- [ ] **Step 2: Run 120 Hz evidence**

```powershell
.\build\Release\frame_pacer_windows_tests.exe
.\build\Release\Burnout3PacingProbe.exe --seconds 1
```

Required:

```text
MEAN_MS approximately 8.333
OVER_9MS 0
OVER_10MS 0
OVER_12MS 0
HIGH_RESOLUTION_TIMER YES
TARGET_HZ 120
FRAMES 120
```

- [ ] **Step 3: Run/read feature-HEAD Windows CI package gates**

Require successful steps:

```text
Configure
Build
Test
Frame pacing telemetry
Pacing probe smoke
Stage analyzer package
Validate analyzer package
Stage pacing probe package
Validate pacing probe package
```

Record exact run ID/job ID only after success.

- [ ] **Step 4: Update README with exact current scope**

Include:

```text
Validated native control transfers: BEQ, BNE, BEQL, BNEL, J, JAL, JR, JALR.
BEQL/BNEL implement architectural branch-likely annulment: delay executes only on the taken path.
BLEZL/BGTZL and REGIMM likely/link-likely variants remain unsupported.
External legal-ELF validation of this expanded path is pending.
The game does not boot yet.
```

- [ ] **Step 5: Update PROGRESS.md with exact milestone evidence**

```text
BEQL + BNEL branch-likely v0: CI_VALIDATED
IR: BranchEqualLikely64 + BranchNotEqualLikely64
Reference/native annulment differential coverage: PASS
Dispatcher/cache likely-branch coverage: PASS
CTest: 51/51
Synthetic startup: 7 blocks / 96 selected guest words / AnalysisFailure @ 0x001001cc
External legal ELF: NOT RUN for this expanded path
Game boot: NOT ACHIEVED
```

Append actual CI run/job IDs and exact pacing values from the successful run.

- [ ] **Step 6: Commit docs**

```bash
git add README.md PROGRESS.md
git commit -m "docs: record R5900 branch-likely validation"
```

- [ ] **Step 7: Review complete diff**

```bash
git diff --stat 2d8030558fd0e8059e0f89a1c41da16261bac962...HEAD
git diff 2d8030558fd0e8059e0f89a1c41da16261bac962...HEAD -- \
  src/recompiler/r5900_ir.h \
  src/recompiler/r5900_ir_validation.cpp \
  src/recompiler/r5900_ir_executor.cpp \
  src/recompiler/windows/r5900_x64_backend.cpp \
  src/recompiler/windows/r5900_block_dispatcher.cpp \
  tests/r5900_branch_likely_test_support.h \
  tests/r5900_ir_branch_likely_validation_tests.cpp \
  tests/r5900_ir_branch_likely_executor_tests.cpp \
  tests/r5900_x64_branch_likely_windows_tests.cpp \
  tests/r5900_block_dispatcher_branch_likely_windows_tests.cpp \
  CMakeLists.txt README.md PROGRESS.md
```

Release blockers:

```text
[ ] BEQ/BNE semantics unchanged.
[ ] BEQL predicate is equality; BNEL predicate is inequality.
[ ] Not-taken x64 path branches around all delay code.
[ ] Taken delay executes once and propagates helper failure.
[ ] Predicate is decided before delay mutation.
[ ] Runtime register values never enter fingerprint.
[ ] Delay guest word remains fingerprinted when annulled.
[ ] instructions_executed remains selected-word accounting.
[ ] SQ likely-delay rejection commits zero guest progress.
[ ] Startup remains 7/96 @ 0x001001cc.
[ ] No EXTERNALLY_VALIDATED or boot claim.
[ ] No temporary workflow/script remains.
```

If any box fails, fix it and rerun the relevant focused test before continuing.

- [ ] **Step 8: Run fresh final Windows CI on exact documentation HEAD**

Require `head_sha == final feature HEAD` and:

```text
51/51 tests
0 failures
approximately 8.333 ms mean
0 >9/10/12 ms
120/120 probe
both package validations PASS
```

- [ ] **Step 9: Verify ancestry and present integration choice**

Confirm feature is ahead of `feature/r5900-beq-delay-slot-v0` and `behind_by == 0`. Then present exactly:

```text
Implementation complete. What would you like to do?

1. Merge back to feature/r5900-beq-delay-slot-v0 locally
2. Push and create a Pull Request
3. Keep the branch as-is (I'll handle it later)

Which option?
```

Do not move the base branch before the user chooses option 1.
