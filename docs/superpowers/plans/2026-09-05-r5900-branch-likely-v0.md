# R5900 BEQL + BNEL Branch-Likely v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add native Windows x86-64 execution for R5900 `BEQL` and `BNEL` with architectural delay-slot annulment on the not-taken path, while preserving existing BEQ/BNE semantics, cache accounting, and 120 Hz validation.

**Architecture:** Add explicit `BranchEqualLikely64` and `BranchNotEqualLikely64` IR terminators rather than refactoring the already validated `BranchEqual64`. The reference executor and x64 backend evaluate the matching predicate before delay execution; taken likely branches execute the delay exactly once, while not-taken likely branches skip the delay completely. The dispatcher lowers decoded BEQL/BNEL directly to those terminators, keeps terminator+delay words in the cache fingerprint, and preserves the existing selected-guest-word `instructions_executed` convention.

**Tech Stack:** C++20, R5900 decoder/control-flow analyzer, typed R5900 IR, reference IR executor, handwritten Windows x86-64 emitter, Win64 ABI helper frame, PS2 memory map, CMake/CTest, GitHub Actions Windows CI.

**Spec:** `docs/superpowers/specs/2026-09-05-r5900-branch-likely-v0-design.md`

## Global Constraints

- Base branch is `feature/r5900-beq-delay-slot-v0` at `2d8030558fd0e8059e0f89a1c41da16261bac962`.
- Feature branch is `feature/r5900-branch-likely-v0`.
- Support only `BEQL` (`0x14`) and `BNEL` (`0x15`) in this milestone.
- `BLEZL`, `BGTZL`, `BLTZL`, `BGEZL`, and link-likely REGIMM variants remain unsupported.
- Taken BEQL/BNEL executes exactly one architectural delay instruction; not-taken BEQL/BNEL executes no delay instruction and produces no delay-side register, memory, helper-call, or fault effect.
- Branch predicates use GPR low64 values before delay execution.
- Ordinary `BranchEqual64` / BEQ / BNE semantics remain unchanged.
- Runtime GPR values never participate in the block cache fingerprint.
- The likely branch word and architectural delay word both remain in the cache fingerprint even when that execution annuls the delay.
- `R5900DispatchResult::instructions_executed` continues counting selected guest words represented by a successfully executed native block; it is not converted to a dynamic-retirement metric.
- `SQ` in BEQ/BNE/BEQL/BNEL dispatcher-managed delay slots remains outside v0 scope and must fail during lowering before guest execution.
- Synthetic startup remains `7 blocks / 96 selected guest words / AnalysisFailure at 0x001001cc`.
- Keep exact 120 Hz / approximately 8.333 ms pacing checks green.
- Do not claim `EXTERNALLY_VALIDATED`, game boot, graphics, audio, input, menus, or gameplay without direct evidence.

---

## File Structure

**Create:**

- `tests/r5900_branch_likely_test_support.h` — focused builders for equal-likely/not-equal-likely IR blocks, reusing the existing generic GPR/NOP/ADDIU/Store128 test helpers.
- `tests/r5900_ir_branch_likely_validation_tests.cpp` — structural IR validation matrix.
- `tests/r5900_ir_branch_likely_executor_tests.cpp` — reference semantics and annulment/fault ordering.
- `tests/r5900_x64_branch_likely_windows_tests.cpp` — reference-vs-native differential coverage, including helper-capable delay paths.
- `tests/r5900_block_dispatcher_branch_likely_windows_tests.cpp` — decoder/analyzer/dispatcher/cache/accounting/`SQ` integration.

**Modify:**

- `src/recompiler/r5900_ir.h` — add the two explicit likely terminator kinds.
- `src/recompiler/r5900_ir_validation.cpp` — validate both likely terminators with the existing binary-branch structural contract.
- `src/recompiler/r5900_ir_executor.cpp` — implement taken-only delay execution for equal/not-equal likely predicates.
- `src/recompiler/windows/r5900_x64_backend.cpp` — emit one native likely-branch path parameterized by equality polarity and skip delay code entirely on not-taken paths.
- `src/recompiler/windows/r5900_block_dispatcher.cpp` — recognize/lower BEQL/BNEL, preserve fingerprint/accounting, and extend branch-delay `SQ` diagnostics.
- `CMakeLists.txt` — register four dedicated tests, taking the full suite from 47 to 51 tests.
- `README.md` — document validated BEQL/BNEL scope and unchanged external-validation boundary.
- `PROGRESS.md` — record exact likely-branch coverage and latest green CI evidence.

No production change is required in `src/recompiler/r5900_decoder.cpp` or `src/analysis/r5900_control_flow.cpp`; both already decode/annotate likely branches correctly.

---

### Task 1: Explicit likely terminators and IR validation

**Files:**
- Create: `tests/r5900_branch_likely_test_support.h`
- Create: `tests/r5900_ir_branch_likely_validation_tests.cpp`
- Modify: `src/recompiler/r5900_ir.h`
- Modify: `src/recompiler/r5900_ir_validation.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: existing `R5900IrBlock`, `R5900IrTerminator`, `R5900IrOperand`, `validate_r5900_ir_block`, and helpers from `tests/r5900_direct_transfer_test_support.h`.
- Produces: `R5900IrTerminatorKind::BranchEqualLikely64`, `R5900IrTerminatorKind::BranchNotEqualLikely64`, plus `branch_equal_likely(...)` and `branch_not_equal_likely(...)` test builders used by Tasks 2 and 3.

- [ ] **Step 1: Add the focused test-support header**

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

At this point the header intentionally does not compile because the two enum values do not exist yet.

- [ ] **Step 2: Write the validation RED**

Create `tests/r5900_ir_branch_likely_validation_tests.cpp` with this harness and matrix:

```cpp
#include "r5900_branch_likely_test_support.h"
#include "recompiler/r5900_ir_validation.h"

#include <cstdlib>
#include <iostream>

namespace {
using namespace b3r::recompiler;
using namespace b3r::test_support;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_branch_likely_validation_tests: FAIL: "
              << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) fail(message);
}

void expect_malformed(const R5900IrBlock& block, const char* message) {
    expect(validate_r5900_ir_block(block).error ==
               R5900IrValidationError::MalformedInstruction,
           message);
}
} // namespace

int main() {
    const auto beql = branch_equal_likely(
        0x0010b000u, 4u, 5u, 0x0010b100u, nop(0x0010b004u));
    const auto bnel = branch_not_equal_likely(
        0x0010b200u, 6u, 7u, 0x0010b300u, nop(0x0010b204u));

    expect(validate_r5900_ir_block(beql).ok(),
           "valid BranchEqualLikely64 must validate");
    expect(validate_r5900_ir_block(bnel).ok(),
           "valid BranchNotEqualLikely64 must validate");

    {
        auto invalid = beql;
        invalid.terminator.inputs.clear();
        expect_malformed(invalid, "likely branch requires exactly two GPR inputs");
    }
    {
        auto invalid = bnel;
        invalid.terminator.inputs.push_back(gpr(8u));
        expect_malformed(invalid, "likely branch rejects more than two inputs");
    }
    {
        auto invalid = bnel;
        invalid.terminator.inputs[1].kind = R5900IrOperandKind::Immediate;
        expect_malformed(invalid, "likely branch rejects non-GPR input");
    }
    {
        auto invalid = beql;
        invalid.terminator.inputs[0].gpr_index = 32u;
        expect(validate_r5900_ir_block(invalid).error ==
                   R5900IrValidationError::InvalidRegister,
               "likely branch rejects out-of-range GPR");
    }
    {
        auto invalid = bnel;
        invalid.terminator.target_pc = 0x0010b400u;
        expect_malformed(invalid, "likely branch rejects target_pc");
    }
    {
        auto invalid = beql;
        invalid.terminator.link_pc = 0x0010b008u;
        expect_malformed(invalid, "likely branch rejects link_pc");
    }
    {
        auto invalid = bnel;
        invalid.terminator.link_gpr = 1u;
        expect_malformed(invalid, "likely branch rejects link_gpr");
    }
    {
        auto invalid = beql;
        invalid.terminator.taken_pc |= 2u;
        expect_malformed(invalid, "likely branch requires aligned taken_pc");
    }
    {
        auto invalid = bnel;
        invalid.terminator.fallthrough_pc |= 2u;
        expect_malformed(invalid, "likely branch requires aligned fallthrough_pc");
    }
    {
        auto invalid = beql;
        invalid.terminator.delay_slot.clear();
        expect_malformed(invalid, "likely branch requires one delay instruction");
    }
    {
        auto invalid = bnel;
        invalid.terminator.delay_slot.push_back(nop(0x0010b208u));
        expect_malformed(invalid, "likely branch rejects multiple delay instructions");
    }
    {
        auto invalid = beql;
        invalid.terminator.delay_slot.front().opcode =
            static_cast<R5900IrOpcode>(0xffu);
        expect(validate_r5900_ir_block(invalid).error ==
                   R5900IrValidationError::UnsupportedOpcode,
               "likely branch propagates invalid delay opcode");
    }

    std::cout << "r5900_ir_branch_likely_validation_tests: PASS\n";
    return EXIT_SUCCESS;
}
```

Register the target in `CMakeLists.txt` beside the other IR transfer-validation tests:

```cmake
add_executable(r5900_ir_branch_likely_validation_tests
  tests/r5900_ir_branch_likely_validation_tests.cpp
)
target_link_libraries(r5900_ir_branch_likely_validation_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_branch_likely_validation_tests
  COMMAND r5900_ir_branch_likely_validation_tests)
```

- [ ] **Step 3: Run the RED and verify the failure is the missing IR contract**

Run on Windows:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target r5900_ir_branch_likely_validation_tests
```

Expected: compilation fails because `BranchEqualLikely64` and `BranchNotEqualLikely64` do not yet exist. Do not accept unrelated compile errors.

- [ ] **Step 4: Add the two enum values**

In `src/recompiler/r5900_ir.h`, keep the existing ordinary branch kind unchanged and insert:

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

- [ ] **Step 5: Add strict validator cases**

In `validate_r5900_ir_block`, add a shared case body for the two new kinds without changing the existing `BranchEqual64` case:

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
        const auto operand_validation =
            validate_operand(operand, terminator_index, terminator.guest_pc);
        if (!operand_validation.ok()) {
            return operand_validation;
        }
    }
    return validate_single_delay_slot(terminator, terminator_index);
```

- [ ] **Step 6: Run focused validation GREEN**

```powershell
cmake --build build --config Release --target r5900_ir_branch_likely_validation_tests
ctest --test-dir build -C Release -R "^r5900_ir_branch_likely_validation_tests$" --output-on-failure
ctest --test-dir build -C Release -R "r5900_ir_(block_|direct_transfer_|indirect_transfer_)?validation_tests" --output-on-failure
```

Expected: all matching tests PASS.

- [ ] **Step 7: Commit Task 1**

```bash
git add CMakeLists.txt src/recompiler/r5900_ir.h src/recompiler/r5900_ir_validation.cpp tests/r5900_branch_likely_test_support.h tests/r5900_ir_branch_likely_validation_tests.cpp
git commit -m "feat: add R5900 likely branch IR"
```

---

### Task 2: Reference executor annulment semantics

**Files:**
- Create: `tests/r5900_ir_branch_likely_executor_tests.cpp`
- Modify: `src/recompiler/r5900_ir_executor.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1's two validated likely terminators and test builders.
- Produces: the reference oracle for BEQL/BNEL next-PC, state mutation, delay annulment, helper-call suppression, and failure ordering. Task 3 differential tests depend on this oracle.

- [ ] **Step 1: Register the target and create the concrete executor harness**

Add to `CMakeLists.txt`:

```cmake
add_executable(r5900_ir_branch_likely_executor_tests
  tests/r5900_ir_branch_likely_executor_tests.cpp
)
target_link_libraries(r5900_ir_branch_likely_executor_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_branch_likely_executor_tests
  COMMAND r5900_ir_branch_likely_executor_tests)
```

Start `tests/r5900_ir_branch_likely_executor_tests.cpp` with:

```cpp
#include "r5900_branch_likely_test_support.h"
#include "recompiler/r5900_ir_executor.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {
using namespace b3r::recompiler;
using namespace b3r::test_support;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_branch_likely_executor_tests: FAIL: "
              << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) fail(message);
}

struct MemoryProbe {
    bool succeed{true};
    std::size_t calls{};
    std::uint32_t address{};
    std::uint64_t low64{};
    std::uint64_t high64{};
};

bool write128(void* user,
              std::uint32_t address,
              std::uint64_t low64,
              std::uint64_t high64) noexcept {
    auto& probe = *static_cast<MemoryProbe*>(user);
    ++probe.calls;
    probe.address = address;
    probe.low64 = low64;
    probe.high64 = high64;
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
} // namespace
```

- [ ] **Step 2: Write all four predicate-outcome RED cases explicitly**

Add to `main()`:

```cpp
{
    const auto block = branch_equal_likely(
        0x0010c000u, 4u, 5u, 0x0010c100u,
        addiu(8u, 8u, 1, 0x0010c004u));

    R5900IrExecutionState state{};
    state.gpr[4].low64 = 7u;
    state.gpr[5].low64 = 7u;
    const auto result = execute_r5900_ir_block(block, state);
    expect(result.ok() && result.next_pc == 0x0010c100u &&
               state.gpr[8].low64 == 1u,
           "BEQL taken must execute delay exactly once");
}
{
    const auto block = branch_equal_likely(
        0x0010c000u, 4u, 5u, 0x0010c100u,
        addiu(8u, 8u, 1, 0x0010c004u));

    R5900IrExecutionState state{};
    state.gpr[4].low64 = 7u;
    state.gpr[5].low64 = 8u;
    const auto result = execute_r5900_ir_block(block, state);
    expect(result.ok() && result.next_pc == 0x0010c008u &&
               state.gpr[8].low64 == 0u,
           "BEQL not-taken must annul delay");
}
{
    const auto block = branch_not_equal_likely(
        0x0010c200u, 4u, 5u, 0x0010c300u,
        addiu(8u, 8u, 1, 0x0010c204u));

    R5900IrExecutionState state{};
    state.gpr[4].low64 = 7u;
    state.gpr[5].low64 = 8u;
    const auto result = execute_r5900_ir_block(block, state);
    expect(result.ok() && result.next_pc == 0x0010c300u &&
               state.gpr[8].low64 == 1u,
           "BNEL taken must execute delay exactly once");
}
{
    const auto block = branch_not_equal_likely(
        0x0010c200u, 4u, 5u, 0x0010c300u,
        addiu(8u, 8u, 1, 0x0010c204u));

    R5900IrExecutionState state{};
    state.gpr[4].low64 = 7u;
    state.gpr[5].low64 = 7u;
    const auto result = execute_r5900_ir_block(block, state);
    expect(result.ok() && result.next_pc == 0x0010c208u &&
               state.gpr[8].low64 == 0u,
           "BNEL not-taken must annul delay");
}
```

- [ ] **Step 3: Add predicate-order and memory-annulment RED cases**

Predicate-before-delay mutation:

```cpp
{
    const auto block = branch_equal_likely(
        0x0010c400u, 4u, 5u, 0x0010c500u,
        addiu(4u, 0u, 0, 0x0010c404u));
    R5900IrExecutionState state{};
    state.gpr[4].low64 = 9u;
    state.gpr[5].low64 = 9u;
    const auto result = execute_r5900_ir_block(block, state);
    expect(result.ok() && result.next_pc == 0x0010c500u &&
               state.gpr[4].low64 == 0u,
           "likely predicate must be snapshotted before delay mutation");
}
```

Annulled failing Store128:

```cpp
{
    const auto block = branch_equal_likely(
        0x0010c600u, 4u, 5u, 0x0010c700u,
        store128(2u, 3u, 0, 0x0010c604u));
    R5900IrExecutionState state{};
    state.gpr[4].low64 = 1u;
    state.gpr[5].low64 = 2u;
    state.gpr[2].low64 = 0x00004017u;
    state.gpr[3] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
    MemoryProbe probe{};
    probe.succeed = false;
    auto context = context_for(state, probe);
    const auto result = execute_r5900_ir_block(block, context);
    expect(result.ok() && result.next_pc == 0x0010c608u,
           "not-taken BEQL must succeed despite failing delay helper");
    expect(probe.calls == 0u && !context.memory_fault.active,
           "annulled likely delay must not call memory helper or create fault");
}
```

Taken failing Store128:

```cpp
{
    const auto block = branch_not_equal_likely(
        0x0010c800u, 4u, 5u, 0x0010c900u,
        store128(2u, 3u, 0, 0x0010c804u));
    R5900IrExecutionState state{};
    state.gpr[4].low64 = 1u;
    state.gpr[5].low64 = 2u;
    state.gpr[2].low64 = 0x00005017u;
    state.gpr[3] = {0x0123456789abcdefull, 0xfedcba9876543210ull};
    MemoryProbe probe{};
    probe.succeed = false;
    auto context = context_for(state, probe);
    const auto result = execute_r5900_ir_block(block, context);
    expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
           "taken BNEL delay failure must propagate");
    expect(probe.calls == 1u && context.memory_fault.active &&
               context.memory_fault.guest_pc == 0x0010c804u &&
               context.memory_fault.width_bytes == 16u,
           "taken likely delay must report Store128 memory fault");
}
```

End `main()` with a PASS line and `EXIT_SUCCESS`.

- [ ] **Step 4: Run RED**

```powershell
cmake --build build --config Release --target r5900_ir_branch_likely_executor_tests
ctest --test-dir build -C Release -R "^r5900_ir_branch_likely_executor_tests$" --output-on-failure
```

Expected: runtime FAIL because `execute_r5900_ir_block()` does not yet implement the new terminators. The validation layer must succeed first.

- [ ] **Step 5: Implement the minimal reference semantics**

In `src/recompiler/r5900_ir_executor.cpp` add:

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
        return {R5900IrExecutionError::None,
                {},
                block.terminator.fallthrough_pc};
    }

    const auto delay_result =
        execute_ir_sequence(block.terminator.delay_slot, context);
    if (!delay_result.ok()) {
        return map_block_execution_failure(delay_result);
    }

    return {R5900IrExecutionError::None,
            {},
            block.terminator.taken_pc};
}
```

Do not touch the existing `BranchEqual64` case.

- [ ] **Step 6: Run executor GREEN and regressions**

```powershell
cmake --build build --config Release --target r5900_ir_branch_likely_executor_tests
ctest --test-dir build -C Release -R "^r5900_ir_branch_likely_executor_tests$" --output-on-failure
ctest --test-dir build -C Release -R "r5900_ir_.*executor_tests" --output-on-failure
```

Expected: all matching tests PASS.

- [ ] **Step 7: Commit Task 2**

```bash
git add CMakeLists.txt src/recompiler/r5900_ir_executor.cpp tests/r5900_ir_branch_likely_executor_tests.cpp
git commit -m "feat: execute R5900 branch-likely IR"
```

---

### Task 3: Windows x64 differential likely-branch backend

**Files:**
- Create: `tests/r5900_x64_branch_likely_windows_tests.cpp`
- Modify: `src/recompiler/windows/r5900_x64_backend.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 2 reference behavior via `execute_r5900_ir_block`.
- Produces: native compiled blocks for both likely terminators with reference-equivalent state, memory callback behavior, faults, and next PC.

- [ ] **Step 1: Register the Windows differential target**

Inside `if(WIN32)` in `CMakeLists.txt`:

```cmake
add_executable(r5900_x64_branch_likely_windows_tests
  tests/r5900_x64_branch_likely_windows_tests.cpp
)
target_link_libraries(r5900_x64_branch_likely_windows_tests PRIVATE
  b3r_recompiler_x64
)
add_test(NAME r5900_x64_branch_likely_windows_tests
  COMMAND r5900_x64_branch_likely_windows_tests)
```

- [ ] **Step 2: Create the native differential harness**

Create `tests/r5900_x64_branch_likely_windows_tests.cpp` with the same includes as below and these concrete helpers:

```cpp
#include "r5900_branch_likely_test_support.h"
#include "recompiler/r5900_ir_executor.h"
#include "recompiler/windows/r5900_x64_backend.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {
using namespace b3r::recompiler;
using namespace b3r::test_support;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_x64_branch_likely_windows_tests: FAIL: "
              << message << '\n';
    std::exit(EXIT_FAILURE);
}
void expect(bool condition, const char* message) {
    if (!condition) fail(message);
}

struct MemoryProbe {
    bool succeed{true};
    std::size_t calls{};
    std::uint32_t address{};
    std::uint64_t low64{};
    std::uint64_t high64{};
};

bool write128(void* user,
              std::uint32_t address,
              std::uint64_t low64,
              std::uint64_t high64) noexcept {
    auto& probe = *static_cast<MemoryProbe*>(user);
    ++probe.calls;
    probe.address = address;
    probe.low64 = low64;
    probe.high64 = high64;
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

void expect_states_equal(const R5900IrExecutionState& lhs,
                         const R5900IrExecutionState& rhs,
                         const char* message) {
    for (std::size_t i = 0; i < lhs.gpr.size(); ++i) {
        if (lhs.gpr[i].low64 != rhs.gpr[i].low64 ||
            lhs.gpr[i].high64 != rhs.gpr[i].high64) fail(message);
    }
    if (lhs.hi != rhs.hi || lhs.lo != rhs.lo ||
        lhs.hi1 != rhs.hi1 || lhs.lo1 != rhs.lo1 ||
        lhs.sa != rhs.sa || lhs.fcr31 != rhs.fcr31 ||
        lhs.fp_acc != rhs.fp_acc) fail(message);
    for (std::size_t i = 0; i < lhs.fpr.size(); ++i) {
        if (lhs.fpr[i] != rhs.fpr[i]) fail(message);
    }
}

void expect_faults_equal(const R5900IrMemoryFault& lhs,
                         const R5900IrMemoryFault& rhs,
                         const char* message) {
    expect(lhs.active == rhs.active && lhs.access == rhs.access &&
               lhs.guest_pc == rhs.guest_pc && lhs.address == rhs.address &&
               lhs.width_bytes == rhs.width_bytes,
           message);
}

void run_differential(const R5900IrBlock& block,
                      const R5900IrExecutionState& initial,
                      bool memory_succeeds,
                      const char* message) {
    auto reference_state = initial;
    auto native_state = initial;
    MemoryProbe reference_probe{};
    MemoryProbe native_probe{};
    reference_probe.succeed = memory_succeeds;
    native_probe.succeed = memory_succeeds;
    auto reference_context = context_for(reference_state, reference_probe);
    auto native_context = context_for(native_state, native_probe);

    const auto reference_result = execute_r5900_ir_block(block, reference_context);
    auto compiled = compile_r5900_ir_x64(block);
    expect(compiled.ok() && compiled.block.has_value(),
           "branch-likely block must compile natively");
    const auto native_result = compiled.block->execute(native_context);

    expect(reference_result.error == native_result.error,
           "reference/native branch-likely error mismatch");
    if (reference_result.ok()) {
        expect(reference_result.next_pc == native_result.next_pc,
               "reference/native branch-likely next-PC mismatch");
    }
    expect_states_equal(reference_state, native_state, message);
    expect(reference_probe.calls == native_probe.calls &&
               reference_probe.address == native_probe.address &&
               reference_probe.low64 == native_probe.low64 &&
               reference_probe.high64 == native_probe.high64,
           message);
    expect_faults_equal(reference_context.memory_fault,
                        native_context.memory_fault,
                        message);
}
} // namespace
```

- [ ] **Step 3: Add concrete differential fixtures**

Use these cases in `main()`:

```cpp
{
    R5900IrExecutionState state{};
    state.gpr[4].low64 = 7u;
    state.gpr[5].low64 = 7u;
    run_differential(
        branch_equal_likely(0x0010d000u, 4u, 5u, 0x0010d100u,
                            addiu(8u, 8u, 1, 0x0010d004u)),
        state, true, "BEQL taken differential mismatch");
}
{
    R5900IrExecutionState state{};
    state.gpr[4].low64 = 7u;
    state.gpr[5].low64 = 8u;
    run_differential(
        branch_equal_likely(0x0010d200u, 4u, 5u, 0x0010d300u,
                            addiu(8u, 8u, 1, 0x0010d204u)),
        state, true, "BEQL annulled differential mismatch");
}
{
    R5900IrExecutionState state{};
    state.gpr[4].low64 = 7u;
    state.gpr[5].low64 = 8u;
    run_differential(
        branch_not_equal_likely(0x0010d400u, 4u, 5u, 0x0010d500u,
                                addiu(8u, 8u, 1, 0x0010d404u)),
        state, true, "BNEL taken differential mismatch");
}
{
    R5900IrExecutionState state{};
    state.gpr[4].low64 = 7u;
    state.gpr[5].low64 = 7u;
    run_differential(
        branch_not_equal_likely(0x0010d600u, 4u, 5u, 0x0010d700u,
                                addiu(8u, 8u, 1, 0x0010d604u)),
        state, true, "BNEL annulled differential mismatch");
}
{
    R5900IrExecutionState state{};
    state.gpr[4].low64 = 9u;
    state.gpr[5].low64 = 9u;
    auto block = branch_equal_likely(
        0x0010d800u, 4u, 5u, 0x0010d900u,
        addiu(4u, 0u, 0, 0x0010d804u));
    block.body = {addiu(24u, 0u, 0x55, 0x0010d7fcu)};
    run_differential(block, state, true,
                     "BEQL body/predicate-before-delay mismatch");
}
```

Then add explicit helper-capable fixtures rather than relying only on aggregate equality:

```cpp
{
    R5900IrExecutionState reference_state{};
    reference_state.gpr[4].low64 = 1u;
    reference_state.gpr[5].low64 = 2u; // BEQL not taken
    reference_state.gpr[2].low64 = 0x00006017u;
    reference_state.gpr[3] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
    auto native_state = reference_state;

    MemoryProbe reference_probe{};
    MemoryProbe native_probe{};
    reference_probe.succeed = false;
    native_probe.succeed = false;
    auto reference_context = context_for(reference_state, reference_probe);
    auto native_context = context_for(native_state, native_probe);
    const auto block = branch_equal_likely(
        0x0010da00u, 4u, 5u, 0x0010db00u,
        store128(2u, 3u, 0, 0x0010da04u));

    const auto reference_result = execute_r5900_ir_block(block, reference_context);
    auto compiled = compile_r5900_ir_x64(block);
    expect(compiled.ok() && compiled.block.has_value(),
           "annulled helper-capable BEQL must compile");
    const auto native_result = compiled.block->execute(native_context);
    expect(reference_result.ok() && native_result.ok() &&
               reference_result.next_pc == 0x0010da08u &&
               native_result.next_pc == 0x0010da08u,
           "annulled helper-capable BEQL must return fallthrough");
    expect(reference_probe.calls == 0u && native_probe.calls == 0u &&
               !reference_context.memory_fault.active &&
               !native_context.memory_fault.active,
           "annulled helper-capable BEQL must bypass helper code");
}
{
    R5900IrExecutionState state{};
    state.gpr[4].low64 = 1u;
    state.gpr[5].low64 = 2u; // BNEL taken
    state.gpr[2].low64 = 0x00007017u;
    state.gpr[3] = {0x0123456789abcdefull, 0xfedcba9876543210ull};
    run_differential(
        branch_not_equal_likely(0x0010dc00u, 4u, 5u, 0x0010dd00u,
                                store128(2u, 3u, 0, 0x0010dc04u)),
        state, false, "taken failing BNEL helper differential mismatch");
}
```

Add a second annulled helper fixture for BNEL equal inputs and a taken helper-success fixture for BEQL equal inputs; assert probe call counts `0` and `1` respectively before the PASS line.

- [ ] **Step 4: Run backend RED**

```powershell
cmake --build build --config Release --target r5900_x64_branch_likely_windows_tests
ctest --test-dir build -C Release -R "^r5900_x64_branch_likely_windows_tests$" --output-on-failure
```

Expected: FAIL at native compilation with `R5900 x64 backend does not support this block terminator`.

- [ ] **Step 5: Add a common likely-branch emitter**

In `src/recompiler/windows/r5900_x64_backend.cpp`, add:

```cpp
enum class LikelyBranchPolarity {
    Equal,
    NotEqual,
};
```

Add `compile_likely_branch_code(const R5900IrBlock&, LikelyBranchPolarity)` beside `compile_branch_equal_code`:

```cpp
PendingX64Code compile_likely_branch_code(
    const R5900IrBlock& block,
    LikelyBranchPolarity polarity) {
    const bool helper_frame =
        sequence_needs_helper(block.body) ||
        sequence_needs_helper(block.terminator.delay_slot);

    std::vector<std::uint8_t> bytes;
    bytes.reserve(160u +
                  block.body.size() * 128u +
                  block.terminator.delay_slot.size() * 256u);

    if (helper_frame) {
        emit_helper_frame_prologue(bytes);
    }
    emit_zero_gpr0(bytes);

    const auto body_emitted = emit_ir_sequence(
        bytes, block.body, 0u, helper_frame);
    if (!body_emitted.ok()) {
        return pending_failure(body_emitted.error, body_emitted.message);
    }

    emit_load_rax_from_state(
        bytes,
        gpr_low64_offset(block.terminator.inputs[0].gpr_index));
    emit_load_rdx_from_state(
        bytes,
        gpr_low64_offset(block.terminator.inputs[1].gpr_index));
    bytes.insert(bytes.end(), {0x48u, 0x39u, 0xd0u}); // cmp rax, rdx

    // Jump directly to not-taken when the architectural likely predicate fails.
    bytes.insert(bytes.end(), {
        0x0fu,
        polarity == LikelyBranchPolarity::Equal ? 0x85u : 0x84u,
    });
    const auto not_taken_rel32_offset = bytes.size();
    emit_u32(bytes, 0u);

    emit_zero_gpr0(bytes);
    const auto delay_emitted = emit_ir_sequence(
        bytes,
        block.terminator.delay_slot,
        block.body.size() + 1u,
        helper_frame);
    if (!delay_emitted.ok()) {
        return pending_failure(delay_emitted.error, delay_emitted.message);
    }
    emit_zero_gpr0(bytes);
    emit_mov_eax_imm32(bytes, block.terminator.taken_pc);
    if (helper_frame) {
        emit_helper_frame_epilogue(bytes);
    }
    bytes.push_back(0xc3u);

    const auto not_taken_offset = bytes.size();
    const auto branch_end = not_taken_rel32_offset + sizeof(std::uint32_t);
    const auto displacement =
        static_cast<std::int64_t>(not_taken_offset) -
        static_cast<std::int64_t>(branch_end);
    if (displacement < std::numeric_limits<std::int32_t>::min() ||
        displacement > std::numeric_limits<std::int32_t>::max()) {
        return pending_failure(
            R5900X64CompileError::UnsupportedOpcode,
            "R5900 x64 likely-branch native path exceeds rel32 range");
    }
    patch_u32(bytes,
              not_taken_rel32_offset,
              static_cast<std::uint32_t>(
                  static_cast<std::int32_t>(displacement)));

    emit_zero_gpr0(bytes);
    emit_mov_eax_imm32(bytes, block.terminator.fallthrough_pc);
    if (helper_frame) {
        emit_helper_frame_epilogue(bytes);
    }
    bytes.push_back(0xc3u);

    return publish_code(bytes);
}
```

Condition bytes are exact:

- equal-likely uses `jne rel32` (`0x0f 0x85`) to bypass delay when `lhs != rhs`;
- not-equal-likely uses `je rel32` (`0x0f 0x84`) to bypass delay when `lhs == rhs`.

- [ ] **Step 6: Wire the compile switch**

Extend `compile_r5900_ir_x64(const R5900IrBlock&)`:

```cpp
case R5900IrTerminatorKind::BranchEqualLikely64:
    pending = compile_likely_branch_code(block, LikelyBranchPolarity::Equal);
    break;
case R5900IrTerminatorKind::BranchNotEqualLikely64:
    pending = compile_likely_branch_code(block, LikelyBranchPolarity::NotEqual);
    break;
```

Leave `BranchEqual64 -> compile_branch_equal_code(block)` untouched.

- [ ] **Step 7: Run x64 GREEN and backend regressions**

```powershell
cmake --build build --config Release --target r5900_x64_branch_likely_windows_tests
ctest --test-dir build -C Release -R "^r5900_x64_branch_likely_windows_tests$" --output-on-failure
ctest --test-dir build -C Release -R "^r5900_x64_.*windows_tests$" --output-on-failure
```

Expected: all matching tests PASS.

- [ ] **Step 8: Commit Task 3**

```bash
git add CMakeLists.txt src/recompiler/windows/r5900_x64_backend.cpp tests/r5900_x64_branch_likely_windows_tests.cpp
git commit -m "feat: compile R5900 likely branches on x64"
```

---

### Task 4: Dispatcher, cache, accounting, and SQ-delay integration

**Files:**
- Create: `tests/r5900_block_dispatcher_branch_likely_windows_tests.cpp`
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Modify: `CMakeLists.txt`
- Regression targets: `r5900_block_dispatcher_windows_tests`, `r5900_block_dispatcher_store128_windows_tests`, `r5900_block_dispatcher_startup_windows_tests`

**Interfaces:**
- Consumes: decoder/analyzer `ConditionalBranch` + decoded `Beql`/`Bnel`, Task 3 native terminators, existing dispatcher cache/fingerprint.
- Produces: end-to-end native dispatcher support for BEQL/BNEL while preserving selected-guest-word accounting and startup 7/96 behavior.

- [ ] **Step 1: Register a dedicated dispatcher likely-branch target and create local helpers**

Inside `if(WIN32)`:

```cmake
add_executable(r5900_block_dispatcher_branch_likely_windows_tests
  tests/r5900_block_dispatcher_branch_likely_windows_tests.cpp
)
target_link_libraries(r5900_block_dispatcher_branch_likely_windows_tests PRIVATE
  b3r_recompiler_dispatcher_x64
)
add_test(NAME r5900_block_dispatcher_branch_likely_windows_tests
  COMMAND r5900_block_dispatcher_branch_likely_windows_tests)
```

Create the test file with these deterministic encoders:

```cpp
constexpr std::uint32_t base = 0x00100000u;

std::uint32_t i_type(std::uint8_t opcode,
                     std::uint8_t rs,
                     std::uint8_t rt,
                     std::uint16_t immediate) {
    return (static_cast<std::uint32_t>(opcode) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           immediate;
}
```

Use the same `Ps2MemoryMap` executable-region fixture already used by dispatcher tests: map the supplied words at `base` with execute/read/write flags so analyzer fetches and delay-slot reads succeed.

- [ ] **Step 2: Write BEQL/BNEL entry RED cases**

Encodings:

```cpp
const auto beql = i_type(0x14u, 1u, 2u, 2u);
const auto bnel = i_type(0x15u, 1u, 2u, 2u);
const auto delay = i_type(0x09u, 3u, 3u, 1u);
```

BEQL taken:

```cpp
auto memory = make_memory({beql, delay, 0u, 0u}, base);
R5900BlockDispatcher dispatcher(memory);
R5900IrExecutionState state{};
state.gpr[1].low64 = 7u;
state.gpr[2].low64 = 7u;
const auto result = dispatcher.run(base, state, 1u);
expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted &&
           result.next_pc == base + 12u &&
           result.blocks_executed == 1u &&
           result.instructions_executed == 2u &&
           state.gpr[3].low64 == 1u,
       "taken BEQL must execute terminator+delay as one selected block");
```

BEQL not taken uses unequal inputs and must assert `next_pc == base + 8u`, `r3 == 0`, while still asserting `instructions_executed == 2u`.

BNEL taken uses unequal inputs and must assert target + delay effect; BNEL not taken uses equal inputs and must assert fallthrough + no delay effect. Both still count two selected guest words.

- [ ] **Step 3: Add body and cache RED cases with exact outcomes**

Body fixture:

```cpp
const auto body = i_type(0x09u, 0u, 10u, 0x55u);
const auto beql = i_type(0x14u, 1u, 2u, 2u);
const auto delay = i_type(0x09u, 11u, 11u, 1u);
auto memory = make_memory({body, beql, delay, 0u, 0u}, base);
R5900BlockDispatcher dispatcher(memory);
```

Run unequal inputs and assert:

```text
reason = BlockBudgetExhausted
next_pc = base + 12   // terminator is at base+4, so fallthrough is +12
blocks_executed = 1
instructions_executed = 3
r10 = 0x55
r11 = 0
```

Cache predicate flip for BEQL:

```cpp
const auto beql = i_type(0x14u, 1u, 2u, 2u);
const auto delay = i_type(0x09u, 3u, 3u, 1u);
auto memory = make_memory({beql, delay, 0u, 0u}, base);
R5900BlockDispatcher dispatcher(memory);

R5900IrExecutionState first_state{};
first_state.gpr[1].low64 = 4u;
first_state.gpr[2].low64 = 4u;
const auto first = dispatcher.run(base, first_state, 1u);
expect(first.cache_misses == 1u && first.cache_hits == 0u &&
           first.recompilations == 0u && first_state.gpr[3].low64 == 1u,
       "first BEQL execution must compile and take branch");

R5900IrExecutionState second_state{};
second_state.gpr[1].low64 = 4u;
second_state.gpr[2].low64 = 5u;
const auto second = dispatcher.run(base, second_state, 1u);
expect(second.cache_hits == 1u && second.cache_misses == 0u &&
           second.recompilations == 0u && second_state.gpr[3].low64 == 0u &&
           second.next_pc == base + 8u && dispatcher.cache_size() == 1u,
       "cached BEQL must change runtime outcome without recompilation");
```

Cache predicate flip for BNEL:

```cpp
const auto bnel = i_type(0x15u, 1u, 2u, 2u);
auto memory = make_memory({bnel, delay, 0u, 0u}, base);
R5900BlockDispatcher dispatcher(memory);

R5900IrExecutionState first_state{};
first_state.gpr[1].low64 = 9u;
first_state.gpr[2].low64 = 9u;
const auto first = dispatcher.run(base, first_state, 1u);
expect(first.cache_misses == 1u && first_state.gpr[3].low64 == 0u &&
           first.next_pc == base + 8u,
       "first BNEL equal execution must annul delay");

R5900IrExecutionState second_state{};
second_state.gpr[1].low64 = 9u;
second_state.gpr[2].low64 = 8u;
const auto second = dispatcher.run(base, second_state, 1u);
expect(second.cache_hits == 1u && second.recompilations == 0u &&
           second_state.gpr[3].low64 == 1u &&
           second.next_pc == base + 12u,
       "cached BNEL must become taken without recompilation");
```

- [ ] **Step 4: Add exact mutation RED cases**

Delay mutation:

```cpp
const auto beql = i_type(0x14u, 1u, 2u, 2u);
const auto delay_one = i_type(0x09u, 3u, 3u, 1u);
const auto delay_two = i_type(0x09u, 3u, 3u, 2u);
auto memory = make_memory({beql, delay_one, 0u, 0u}, base);
R5900BlockDispatcher dispatcher(memory);

R5900IrExecutionState first_state{};
first_state.gpr[1].low64 = 6u;
first_state.gpr[2].low64 = 6u;
const auto first = dispatcher.run(base, first_state, 1u);
expect(first.cache_misses == 1u && first_state.gpr[3].low64 == 1u,
       "original BEQL delay must compile and execute");

expect(memory.write_u32(base + 4u, delay_two),
       "likely delay mutation must succeed");
R5900IrExecutionState second_state{};
second_state.gpr[1].low64 = 6u;
second_state.gpr[2].low64 = 6u;
const auto second = dispatcher.run(base, second_state, 1u);
expect(second.recompilations == 1u && second.cache_hits == 0u &&
           second.cache_misses == 0u && second_state.gpr[3].low64 == 2u,
       "changed likely delay word must replace cached native block");
```

BEQL-to-BNEL mutation:

```cpp
const auto beql = i_type(0x14u, 1u, 2u, 2u);
const auto bnel = i_type(0x15u, 1u, 2u, 2u);
auto memory = make_memory({beql, delay_one, 0u, 0u}, base);
R5900BlockDispatcher dispatcher(memory);

R5900IrExecutionState first_state{};
first_state.gpr[1].low64 = 10u;
first_state.gpr[2].low64 = 10u;
const auto first = dispatcher.run(base, first_state, 1u);
expect(first.cache_misses == 1u && first.next_pc == base + 12u,
       "BEQL warm-up must take equal branch");

expect(memory.write_u32(base, bnel),
       "BEQL-to-BNEL mutation must succeed");
R5900IrExecutionState second_state{};
second_state.gpr[1].low64 = 10u;
second_state.gpr[2].low64 = 10u;
const auto second = dispatcher.run(base, second_state, 1u);
expect(second.recompilations == 1u && second.cache_hits == 0u &&
           second.next_pc == base + 8u && second_state.gpr[3].low64 == 0u,
       "BEQL-to-BNEL mutation must recompile and annul equal-path delay");
```

- [ ] **Step 5: Add SQ-delay RED for both likely opcodes**

Use:

```cpp
const auto sq_delay = i_type(0x1fu, 2u, 3u, 0u);
```

For BEQL, choose unequal inputs so runtime would be not-taken. For BNEL, choose equal inputs so runtime would be not-taken. In both cases assert:

```cpp
expect(result.reason == R5900DispatchStopReason::LoweringFailure,
       "SQ in likely branch delay must fail during lowering");
expect(result.blocks_executed == 0u && result.instructions_executed == 0u,
       "likely SQ-delay rejection must not commit guest progress");
expect(result.message.find("BEQ/BNE/BEQL/BNEL") != std::string::npos,
       "likely SQ-delay diagnostic must identify supported branch family");
```

This proves lowering eligibility is independent of runtime annulment.

- [ ] **Step 6: Run dispatcher RED**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_branch_likely_windows_tests
ctest --test-dir build -C Release -R "^r5900_block_dispatcher_branch_likely_windows_tests$" --output-on-failure
```

Expected: FAIL because BEQL/BNEL are still treated as control-flow boundaries by the dispatcher.

- [ ] **Step 7: Recognize likely transfers in the dispatcher**

Add booleans beside BEQ/BNE:

```cpp
const bool has_supported_beql =
    block.end_kind == analysis::R5900BlockEndKind::ConditionalBranch &&
    !block.instructions.empty() &&
    block.instructions.back().decoded.instruction == R5900Instruction::Beql;
const bool has_supported_bnel =
    block.end_kind == analysis::R5900BlockEndKind::ConditionalBranch &&
    !block.instructions.empty() &&
    block.instructions.back().decoded.instruction == R5900Instruction::Bnel;
```

Change `has_supported_transfer` to:

```cpp
const bool has_supported_transfer =
    has_supported_beq || has_supported_bne ||
    has_supported_beql || has_supported_bnel ||
    has_supported_j || has_supported_jal ||
    has_supported_jr || has_supported_jalr;
```

- [ ] **Step 8: Lower direct likely branches without PC inversion**

Extend the direct-target gate:

```cpp
if (has_supported_beq || has_supported_bne ||
    has_supported_beql || has_supported_bnel ||
    has_supported_j || has_supported_jal) {
    const auto target =
        transfer_site->decoded.direct_target(transfer_site->pc);
    if (!target.has_value()) {
        result.reason = R5900DispatchStopReason::AnalysisFailure;
        result.next_pc = transfer_site->pc;
        result.message = format_stage_error(
            "analysis",
            transfer_site->pc,
            "decoded supported direct control transfer unexpectedly lacks target");
        return result;
    }
```

Inside the branch lowering section:

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

Do not represent BNEL by swapping PCs.

- [ ] **Step 9: Extend the SQ branch-family diagnostic**

Change the branch-family condition to include all four branches:

```cpp
(has_supported_beq || has_supported_bne ||
 has_supported_beql || has_supported_bnel)
    ? "SQ in a BEQ/BNE/BEQL/BNEL delay slot is outside dispatcher v0 scope"
```

Keep J/JAL and JR/JALR messages unchanged.

- [ ] **Step 10: Run dispatcher/cache GREEN and startup regression**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_branch_likely_windows_tests
ctest --test-dir build -C Release -R "^r5900_block_dispatcher_branch_likely_windows_tests$" --output-on-failure
ctest --test-dir build -C Release -R "^r5900_block_dispatcher_.*windows_tests$" --output-on-failure
.\build\Release\r5900_block_dispatcher_startup_windows_tests.exe
```

Expected:

```text
all dispatcher CTests PASS
startup test PASS
startup contract remains 7 blocks / 96 selected guest words / AnalysisFailure @ 0x001001cc
```

- [ ] **Step 11: Commit Task 4**

```bash
git add CMakeLists.txt src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_branch_likely_windows_tests.cpp
git commit -m "feat: dispatch R5900 BEQL and BNEL"
```

---

### Task 5: Documentation, full verification, review, and integration readiness

**Files:**
- Modify: `README.md`
- Modify: `PROGRESS.md`
- Review: all files changed since `2d8030558fd0e8059e0f89a1c41da16261bac962`

**Interfaces:**
- Consumes: Tasks 1-4 green implementation.
- Produces: exact project status, fresh full-suite/120 Hz/package evidence, and a feature branch ready for the user's integration decision.

- [ ] **Step 1: Run the complete Windows build and 51-test suite before documentation claims**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Expected:

```text
100% tests passed, 0 tests failed out of 51
```

Do not update docs to `CI_VALIDATED` if the count differs because a target was omitted or any test fails.

- [ ] **Step 2: Run pacing evidence**

```powershell
.\build\Release\frame_pacer_windows_tests.exe
.\build\Release\Burnout3PacingProbe.exe --seconds 1
```

Required evidence:

```text
MEAN_MS approximately 8.333
OVER_9MS 0
OVER_10MS 0
OVER_12MS 0
HIGH_RESOLUTION_TIMER YES
TARGET_HZ 120
FRAMES 120
```

- [ ] **Step 3: Validate package staging through the feature-HEAD Windows CI**

Trigger/read `.github/workflows/windows-ci.yml` for the exact feature HEAD and require these steps to complete successfully:

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

Record the fresh run ID/job ID in `PROGRESS.md` only after that run completes successfully.

- [ ] **Step 4: Update README current capabilities with exact text**

In the native dispatcher/control-flow section, state:

```text
Validated native control transfers: BEQ, BNE, BEQL, BNEL, J, JAL, JR, JALR.
BEQL/BNEL implement architectural branch-likely annulment: the delay slot executes only on the taken path; not-taken paths skip all delay-side state and memory effects.
BLEZL/BGTZL and REGIMM likely/link-likely variants are not yet implemented.
External legal-ELF validation of this expanded path is still pending.
The game does not boot yet.
```

Preserve any existing historical evidence blocks rather than rewriting them as current claims.

- [ ] **Step 5: Update PROGRESS.md with exact evidence fields**

Add/update the current milestone block to contain:

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

Append the actual fresh Windows CI run/job IDs and exact pacing telemetry produced in Steps 2-3.

- [ ] **Step 6: Commit documentation**

```bash
git add README.md PROGRESS.md
git commit -m "docs: record R5900 branch-likely validation"
```

- [ ] **Step 7: Self-review the complete feature diff**

Run:

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

Verify every checklist item:

```text
[ ] BEQ/BNE BranchEqual64 code paths are unchanged semantically.
[ ] BEQL uses equality; BNEL uses inequality.
[ ] Not-taken likely native path branches around all delay code.
[ ] Taken likely delay executes once and propagates helper failure.
[ ] Predicate is resolved before delay mutation.
[ ] Runtime predicate values do not enter cache fingerprint.
[ ] Delay word remains in fingerprint even on annulled execution.
[ ] instructions_executed remains selected-guest-word accounting.
[ ] SQ likely-delay rejection occurs before guest progress.
[ ] Startup remains 7/96 @ 0x001001cc.
[ ] README/PROGRESS contain no EXTERNALLY_VALIDATED or boot claim.
[ ] No temporary one-shot workflow/script remains.
```

Any failed item is a release blocker; correct it and rerun its focused tests before proceeding.

- [ ] **Step 8: Run a fresh final Windows CI on the exact documentation HEAD**

Require a new successful workflow run whose `head_sha` equals the final feature HEAD. Re-read its CTest and pacing logs; do not reuse an earlier code-only run as final evidence.

Required final state:

```text
51/51 tests
0 failures
approximately 8.333 ms mean pacing
0 >9/10/12 ms
120/120 pacing probe
analyzer package validation PASS
pacing-probe package validation PASS
```

- [ ] **Step 9: Verify feature/base ancestry and present integration options**

Confirm the feature is ahead of `feature/r5900-beq-delay-slot-v0` and not behind it. Then use the finishing-branch workflow and present exactly:

```text
Implementation complete. What would you like to do?

1. Merge back to feature/r5900-beq-delay-slot-v0 locally
2. Push and create a Pull Request
3. Keep the branch as-is (I'll handle it later)

Which option?
```

Do not move the base branch until the user chooses option 1.
