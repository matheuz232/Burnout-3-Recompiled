# R5900 Indirect Jump/Call v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute architectural R5900 `JR` and `JALR` in the native Windows dispatcher, including target snapshot ordering, arbitrary JALR link destination, cache-safe dynamic targets, and an expanded startup path that reaches the next unsupported `BNE` boundary.

**Architecture:** Extend the typed block-terminator IR with `IndirectJump` and `IndirectCall`. The target source is `terminator.inputs[0]`; `IndirectCall` adds `link_gpr` and uses `link_pc = guest_pc + 8`. Reference and x64 execution snapshot the low 32 bits of the source GPR before link/delay effects. Native x64 stores the snapshot in the existing Win64 helper-frame local `[rsp+0x30]`, so helper-capable delay slots cannot clobber it. The dispatcher extends its existing supported-transfer pipeline to `JR/JALR` without putting runtime target values in the cache fingerprint.

**Tech Stack:** C++20, CMake 3.25+, MSVC/Visual Studio 2022 x64, Win64 ABI, GitHub Actions Windows CI, CTest.

**Spec:** `docs/superpowers/specs/2026-09-05-r5900-indirect-jump-call-v0-design.md`

## Global Constraints

- Windows-only native recompilation/port path; do not introduce a generic PS2 emulator.
- Base branch is `feature/r5900-beq-delay-slot-v0`; feature branch is `feature/r5900-indirect-jump-call-v0`.
- Preserve dependency direction: `b3r_runtime -> b3r_recompiler`; `b3r_recompiler_x64` links only `b3r_recompiler`; dispatcher owns the `Ps2MemoryMap` bridge.
- `JR/JALR` execute exactly one architectural delay slot.
- Indirect target is `uint32(state.gpr[rs].low64)` captured before delay execution and, for `JALR`, before the link write.
- `JALR` link is `uint32(guest_pc + 8)`, written to `GPR[rd].low64` only; `high64` is preserved; `rd == 0` performs no persistent link write.
- `JALR rd == rs` must jump using the pre-link value of `rs`.
- Dispatcher cache identity remains guest-code-only: body words + terminator word + delay-slot word. Runtime target GPR contents never enter the fingerprint.
- Supported indirect cached span ends at `terminator_pc + 8`.
- Dispatcher v0 rejects `SQ` in `JR/JALR` delay slots as `LoweringFailure`; lower-level IR/reference/x64 may execute `Store128` in delay slots for differential testing.
- Misaligned/unmapped/non-executable runtime targets are returned exactly by the completed indirect block and fail on the next dispatcher analysis iteration; the completed block remains counted and committed.
- Keep W^X, Win64 32-byte shadow space, stack alignment, volatile-register rules, and the generated function ABI unchanged.
- Preserve the 120 Hz / 8.333333 ms presentation target and existing pacing/package checks.
- Do not claim game boot or external legal-ELF validation unless the legal ELF is actually run.

---

## File Structure

**Production files modified**

- `src/recompiler/r5900_ir.h` — add `IndirectJump`, `IndirectCall`, and `link_gpr` to the terminator representation.
- `src/recompiler/r5900_ir_validation.cpp` — validate indirect terminators and tighten existing terminators to reject non-zero `link_gpr`.
- `src/recompiler/r5900_ir_executor.cpp` — implement reference `JR/JALR` ordering and dynamic next-PC semantics.
- `src/recompiler/windows/r5900_x64_backend.cpp` — emit indirect target snapshot/link/delay/native return using `[rsp+0x30]`.
- `src/recompiler/windows/r5900_block_dispatcher.cpp` — recognize/lower/cache/execute analyzer `IndirectJump/IndirectCall` only when final decoded instruction is `JR/JALR`.
- `CMakeLists.txt` — register four dedicated indirect-transfer test executables.
- `tests/r5900_direct_transfer_test_support.h` — extend the existing control-transfer test helpers with `indirect_jump()` and `indirect_call()`; keep shared `gpr`, `addiu`, `nop`, and `store128` helpers.
- `tests/r5900_block_dispatcher_startup_windows_tests.cpp` — expand synthetic startup through `JR` and aliasing `JALR`, ending at unsupported `BNE`.
- `README.md` — update milestone/status wording after full CI.
- `docs/PROGRESS.md` — record JR/JALR coverage, exact E2E accounting, CI evidence, and remaining boundary.

**New tests**

- `tests/r5900_ir_indirect_transfer_validation_tests.cpp` — typed IR contract and malformed-state coverage.
- `tests/r5900_ir_indirect_transfer_executor_tests.cpp` — reference JR/JALR semantics and memory-failure ordering.
- `tests/r5900_x64_indirect_transfer_windows_tests.cpp` — reference/native differential, aliasing, helper success/failure, next-PC equality.
- `tests/r5900_block_dispatcher_indirect_transfer_windows_tests.cpp` — dispatcher/cache behavior, target changes without recompilation, SQ-delay rejection, invalid-target follow-up failures.

---

### Task 1: Typed indirect terminators and validation

**Files:**
- Modify: `src/recompiler/r5900_ir.h`
- Modify: `src/recompiler/r5900_ir_validation.cpp`
- Modify: `tests/r5900_direct_transfer_test_support.h`
- Create: `tests/r5900_ir_indirect_transfer_validation_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: existing `R5900IrOperand`, `R5900IrTerminator`, `validate_r5900_ir_block()`, and the test helpers `gpr()`, `nop()`.
- Produces:
  - `R5900IrTerminatorKind::IndirectJump`
  - `R5900IrTerminatorKind::IndirectCall`
  - `R5900IrTerminator::link_gpr` as `std::uint8_t`
  - `b3r::test_support::indirect_jump(std::uint32_t pc, std::uint8_t rs, R5900IrInstruction delay)`
  - `b3r::test_support::indirect_call(std::uint32_t pc, std::uint8_t rs, std::uint8_t rd, R5900IrInstruction delay)`

- [ ] **Step 1: Register a dedicated validation test target and write the RED test**

Add to the portable test section of `CMakeLists.txt` immediately after the direct-transfer validation target:

```cmake
add_executable(r5900_ir_indirect_transfer_validation_tests
  tests/r5900_ir_indirect_transfer_validation_tests.cpp
)
target_link_libraries(r5900_ir_indirect_transfer_validation_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_indirect_transfer_validation_tests
  COMMAND r5900_ir_indirect_transfer_validation_tests)
```

Extend `tests/r5900_direct_transfer_test_support.h` only after the RED compile has been observed; the initial test should refer to the not-yet-existing enum/field/helpers so it fails for the intended reason.

Create `tests/r5900_ir_indirect_transfer_validation_tests.cpp` with this test shape:

```cpp
#include "r5900_direct_transfer_test_support.h"
#include "recompiler/r5900_ir_validation.h"

#include <cstdlib>
#include <iostream>

namespace {
using namespace b3r::recompiler;
using namespace b3r::test_support;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_indirect_transfer_validation_tests: FAIL: "
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
    const auto jump = indirect_jump(0x00108000u, 5u, nop(0x00108004u));
    const auto call = indirect_call(0x00108200u, 7u, 9u, nop(0x00108204u));
    expect(validate_r5900_ir_block(jump).ok(), "valid IndirectJump must validate");
    expect(validate_r5900_ir_block(call).ok(), "valid IndirectCall must validate");

    {
        auto invalid = jump;
        invalid.terminator.inputs.clear();
        expect_malformed(invalid, "IndirectJump requires one target GPR");
    }
    {
        auto invalid = jump;
        invalid.terminator.inputs = {gpr(5u), gpr(6u)};
        expect_malformed(invalid, "IndirectJump rejects multiple target inputs");
    }
    {
        auto invalid = jump;
        invalid.terminator.inputs[0].kind = R5900IrOperandKind::Immediate;
        expect_malformed(invalid, "IndirectJump target must be a GPR");
    }
    {
        auto invalid = jump;
        invalid.terminator.inputs[0].gpr_index = 32u;
        expect(validate_r5900_ir_block(invalid).error ==
                   R5900IrValidationError::InvalidRegister,
               "IndirectJump rejects out-of-range target GPR");
    }
    {
        auto invalid = jump;
        invalid.terminator.target_pc = 0x00109000u;
        expect_malformed(invalid, "IndirectJump rejects direct target field");
    }
    {
        auto invalid = jump;
        invalid.terminator.taken_pc = 0x00109000u;
        expect_malformed(invalid, "IndirectJump rejects branch target field");
    }
    {
        auto invalid = jump;
        invalid.terminator.fallthrough_pc = 0x00108008u;
        expect_malformed(invalid, "IndirectJump rejects fallthrough field");
    }
    {
        auto invalid = jump;
        invalid.terminator.link_pc = 0x00108008u;
        expect_malformed(invalid, "IndirectJump rejects link PC");
    }
    {
        auto invalid = jump;
        invalid.terminator.link_gpr = 31u;
        expect_malformed(invalid, "IndirectJump rejects link GPR");
    }
    {
        auto invalid = call;
        invalid.terminator.link_pc += 4u;
        expect_malformed(invalid, "IndirectCall link must equal PC+8");
    }
    {
        auto invalid = call;
        invalid.terminator.link_gpr = 32u;
        expect(validate_r5900_ir_block(invalid).error ==
                   R5900IrValidationError::InvalidRegister,
               "IndirectCall rejects out-of-range link GPR");
    }
    {
        auto invalid = call;
        invalid.terminator.delay_slot.clear();
        expect_malformed(invalid, "IndirectCall requires exactly one delay instruction");
    }
    {
        auto invalid = jump;
        invalid.terminator.delay_slot.push_back(nop(0x00108008u));
        expect_malformed(invalid, "IndirectJump rejects multiple delay instructions");
    }
    {
        auto invalid = call;
        invalid.terminator.delay_slot.front().opcode =
            static_cast<R5900IrOpcode>(0xffu);
        expect(validate_r5900_ir_block(invalid).error ==
                   R5900IrValidationError::UnsupportedOpcode,
               "invalid delay opcode must propagate UnsupportedOpcode");
    }

    for (auto kind : {R5900IrTerminatorKind::Fallthrough,
                      R5900IrTerminatorKind::BranchEqual64,
                      R5900IrTerminatorKind::DirectJump,
                      R5900IrTerminatorKind::DirectCall}) {
        R5900IrBlock invalid{};
        invalid.terminator.guest_pc = 0x00108400u;
        invalid.terminator.kind = kind;
        invalid.terminator.link_gpr = 1u;
        if (kind == R5900IrTerminatorKind::Fallthrough) {
            invalid.terminator.fallthrough_pc = 0x00108404u;
        } else if (kind == R5900IrTerminatorKind::BranchEqual64) {
            invalid.terminator.inputs = {gpr(0u), gpr(0u)};
            invalid.terminator.taken_pc = 0x00108408u;
            invalid.terminator.fallthrough_pc = 0x00108408u;
            invalid.terminator.delay_slot = {nop(0x00108404u)};
        } else {
            invalid.terminator.target_pc = 0x00108500u;
            invalid.terminator.delay_slot = {nop(0x00108404u)};
            if (kind == R5900IrTerminatorKind::DirectCall) {
                invalid.terminator.link_pc = 0x00108408u;
            }
        }
        expect_malformed(invalid, "existing terminators must reject link_gpr");
    }

    std::cout << "r5900_ir_indirect_transfer_validation_tests: PASS\n";
    return EXIT_SUCCESS;
}
```

- [ ] **Step 2: Run the RED validation target**

Windows commands:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target r5900_ir_indirect_transfer_validation_tests
```

Expected RED: MSVC reports missing `IndirectJump`, `IndirectCall`, `link_gpr`, and/or the indirect test helpers. Do not accept an unrelated compile failure.

- [ ] **Step 3: Add the minimal typed IR and test helpers**

In `src/recompiler/r5900_ir.h`:

```cpp
enum class R5900IrTerminatorKind {
    Fallthrough = 0,
    BranchEqual64,
    DirectJump,
    DirectCall,
    IndirectJump,
    IndirectCall,
};

struct R5900IrTerminator {
    std::uint32_t guest_pc{};
    std::uint32_t guest_raw{};
    R5900IrTerminatorKind kind{R5900IrTerminatorKind::Fallthrough};
    std::vector<R5900IrOperand> inputs{};
    std::uint32_t taken_pc{};
    std::uint32_t fallthrough_pc{};
    std::uint32_t target_pc{};
    std::uint32_t link_pc{};
    std::uint8_t link_gpr{};
    std::vector<R5900IrInstruction> delay_slot{};
};
```

Append to `tests/r5900_direct_transfer_test_support.h`:

```cpp
inline R5900IrBlock indirect_jump(std::uint32_t pc,
                                  std::uint8_t rs,
                                  R5900IrInstruction delay) {
    R5900IrBlock block{};
    block.terminator.guest_pc = pc;
    block.terminator.kind = R5900IrTerminatorKind::IndirectJump;
    block.terminator.inputs = {gpr(rs)};
    block.terminator.delay_slot = {delay};
    return block;
}

inline R5900IrBlock indirect_call(std::uint32_t pc,
                                  std::uint8_t rs,
                                  std::uint8_t rd,
                                  R5900IrInstruction delay) {
    auto block = indirect_jump(pc, rs, delay);
    block.terminator.kind = R5900IrTerminatorKind::IndirectCall;
    block.terminator.link_pc = pc + 8u;
    block.terminator.link_gpr = rd;
    return block;
}
```

- [ ] **Step 4: Implement validator cases without changing execution**

Before the block terminator switch, keep existing guest-PC alignment validation. Tighten all existing cases by adding `terminator.link_gpr != 0u` to their malformed conditions.

Add these two cases:

```cpp
case R5900IrTerminatorKind::IndirectJump: {
    if (terminator.inputs.size() != 1u ||
        terminator.inputs[0].kind != R5900IrOperandKind::Gpr ||
        terminator.taken_pc != 0u ||
        terminator.fallthrough_pc != 0u ||
        terminator.target_pc != 0u ||
        terminator.link_pc != 0u ||
        terminator.link_gpr != 0u) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       terminator_index,
                       terminator.guest_pc,
                       "malformed indirect-jump terminator");
    }
    const auto input_validation =
        validate_operand(terminator.inputs[0], terminator_index, terminator.guest_pc);
    if (!input_validation.ok()) {
        return input_validation;
    }
    return validate_single_delay_slot(terminator, terminator_index);
}

case R5900IrTerminatorKind::IndirectCall: {
    if (terminator.inputs.size() != 1u ||
        terminator.inputs[0].kind != R5900IrOperandKind::Gpr ||
        terminator.taken_pc != 0u ||
        terminator.fallthrough_pc != 0u ||
        terminator.target_pc != 0u ||
        terminator.link_gpr >= 32u ||
        (terminator.link_pc & 0x3u) != 0u ||
        terminator.link_pc != static_cast<std::uint32_t>(terminator.guest_pc + 8u)) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       terminator_index,
                       terminator.guest_pc,
                       "malformed indirect-call terminator");
    }
    const auto input_validation =
        validate_operand(terminator.inputs[0], terminator_index, terminator.guest_pc);
    if (!input_validation.ok()) {
        return input_validation;
    }
    return validate_single_delay_slot(terminator, terminator_index);
}
```

For the `link_gpr >= 32` path, preserve the spec's `InvalidRegister` classification by splitting that check before the malformed-condition block:

```cpp
if (terminator.link_gpr >= 32u) {
    return failure(R5900IrValidationError::InvalidRegister,
                   terminator_index,
                   terminator.guest_pc,
                   "invalid indirect-call link GPR");
}
```

- [ ] **Step 5: Run focused and existing validation tests**

```powershell
cmake --build build --config Release --target `
  r5900_ir_indirect_transfer_validation_tests `
  r5900_ir_direct_transfer_validation_tests `
  r5900_ir_block_validation_tests
ctest --test-dir build -C Release --output-on-failure -R "r5900_ir_(indirect_transfer_validation|direct_transfer_validation|block_validation)_tests"
```

Expected: all selected validation tests PASS.

- [ ] **Step 6: Commit Task 1**

```bash
git add CMakeLists.txt src/recompiler/r5900_ir.h src/recompiler/r5900_ir_validation.cpp tests/r5900_direct_transfer_test_support.h tests/r5900_ir_indirect_transfer_validation_tests.cpp
git commit -m "feat: model R5900 indirect transfers"
```

---

### Task 2: Reference executor JR/JALR semantics

**Files:**
- Modify: `src/recompiler/r5900_ir_executor.cpp`
- Create: `tests/r5900_ir_indirect_transfer_executor_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 `IndirectJump`, `IndirectCall`, `link_gpr`, and `indirect_jump()/indirect_call()` helpers.
- Produces: reference execution contract used as oracle by Task 3 x64 differential tests.

- [ ] **Step 1: Register and write the RED executor test**

Add:

```cmake
add_executable(r5900_ir_indirect_transfer_executor_tests
  tests/r5900_ir_indirect_transfer_executor_tests.cpp
)
target_link_libraries(r5900_ir_indirect_transfer_executor_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_indirect_transfer_executor_tests
  COMMAND r5900_ir_indirect_transfer_executor_tests)
```

Create the test with cases equivalent to the following:

```cpp
#include "r5900_direct_transfer_test_support.h"
#include "recompiler/r5900_ir_executor.h"

#include <cstdlib>
#include <iostream>

namespace {
using namespace b3r::recompiler;
using namespace b3r::test_support;
[[noreturn]] void fail(const char* m) { std::cerr << "r5900_ir_indirect_transfer_executor_tests: FAIL: " << m << '\n'; std::exit(EXIT_FAILURE); }
void expect(bool c, const char* m) { if (!c) fail(m); }

struct MemoryProbe {
    bool succeed{true};
    std::uint32_t address{};
    std::uint64_t low{};
    std::uint64_t high{};
    std::size_t calls{};
};

bool write128(void* user,
              std::uint32_t address,
              std::uint64_t low,
              std::uint64_t high) noexcept {
    auto& probe = *static_cast<MemoryProbe*>(user);
    ++probe.calls;
    probe.address = address;
    probe.low = low;
    probe.high = high;
    return probe.succeed;
}
} // namespace

int main() {
    {
        R5900IrExecutionState state{};
        state.gpr[5] = {0x1234000012345678ull, 0xfeedfacefeedfaceull};
        state.gpr[31] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
        const auto block = indirect_jump(0x00109000u, 5u,
                                         addiu(5u, 0u, 9, 0x00109004u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok() && result.next_pc == 0x12345678u,
               "JR must return the snapshotted low32 target");
        expect(state.gpr[5].low64 == 9u,
               "JR delay may mutate source after target snapshot");
        expect(state.gpr[31].low64 == 0x1111222233334444ull &&
               state.gpr[31].high64 == 0xaaaabbbbccccddddull,
               "JR must not modify link registers");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[5] = {0x000000000010a000ull, 0x0123456789abcdefull};
        const auto block = indirect_call(0x00109200u, 5u, 5u,
                                         addiu(6u, 5u, 0, 0x00109204u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok() && result.next_pc == 0x0010a000u,
               "JALR rd==rs must jump using old source value");
        expect(state.gpr[5].low64 == 0x00109208u &&
               state.gpr[5].high64 == 0x0123456789abcdefull,
               "JALR must write PC+8 low64 and preserve high64");
        expect(state.gpr[6].low64 == 0x00109208u,
               "JALR delay must see new link");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[7].low64 = 0x0010b000u;
        state.gpr[0] = {0x1111u, 0x2222u};
        const auto block = indirect_call(0x00109400u, 7u, 0u,
                                         nop(0x00109404u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok() && result.next_pc == 0x0010b000u,
               "JALR rd==0 must still jump");
        expect(state.gpr[0].low64 == 0u && state.gpr[0].high64 == 0u,
               "JALR rd==0 must leave normalized GPR0");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[8].low64 = 0x0010c000u;
        state.gpr[9] = {0x7777u, 0x9999aaaabbbbccccull};
        const auto block = indirect_call(0x00109600u, 8u, 9u,
                                         addiu(9u, 0u, 3, 0x00109604u));
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok(), "JALR delay overwrite case must execute");
        expect(state.gpr[9].low64 == 3u &&
               state.gpr[9].high64 == 0x9999aaaabbbbccccull,
               "delay write must win after JALR link and preserve high64");
    }

    {
        R5900IrExecutionState state{};
        state.gpr[2].low64 = 0x00400000u;
        state.gpr[3] = {0x1111222233334444ull, 0x5555666677778888ull};
        state.gpr[5].low64 = 0x0010d000u;
        state.gpr[9] = {0xaaaaull, 0xbbbbccccddddeeeeull};
        MemoryProbe probe{};
        probe.succeed = false;
        auto block = indirect_call(0x00109800u, 5u, 9u,
                                   store128(2u, 3u, 0, 0x00109804u));
        R5900IrExecutionContext context{};
        context.state = &state;
        context.memory.user = &probe;
        context.memory.write128 = &write128;
        const auto result = execute_r5900_ir_block(block, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "JALR delay memory failure must propagate");
        expect(state.gpr[9].low64 == 0x00109808u &&
               state.gpr[9].high64 == 0xbbbbccccddddeeeeull,
               "JALR link remains committed before delay failure");
        expect(probe.calls == 1u && context.memory_fault.active &&
               context.memory_fault.guest_pc == 0x00109804u,
               "delay memory fault must retain exact guest PC");
    }

    std::cout << "r5900_ir_indirect_transfer_executor_tests: PASS\n";
    return EXIT_SUCCESS;
}
```

Also add one body-failure case by putting `Store128` in `block.body`, leaving the execution context memory callback absent, and asserting the destination link GPR and delay destination remain unchanged.

- [ ] **Step 2: Run RED**

```powershell
cmake --build build --config Release --target r5900_ir_indirect_transfer_executor_tests
ctest --test-dir build -C Release --output-on-failure -R r5900_ir_indirect_transfer_executor_tests
```

Expected RED: build succeeds, test fails because `execute_r5900_ir_block()` still returns `UnsupportedOpcode` for indirect terminators.

- [ ] **Step 3: Implement minimal reference executor cases**

In `execute_r5900_ir_block()` add:

```cpp
case R5900IrTerminatorKind::IndirectJump: {
    const auto target = static_cast<std::uint32_t>(
        state.gpr[block.terminator.inputs[0].gpr_index].low64);
    const auto delay_result =
        execute_ir_sequence(block.terminator.delay_slot, context);
    if (!delay_result.ok()) {
        return map_block_execution_failure(delay_result);
    }
    return {R5900IrExecutionError::None, {}, target};
}

case R5900IrTerminatorKind::IndirectCall: {
    const auto target = static_cast<std::uint32_t>(
        state.gpr[block.terminator.inputs[0].gpr_index].low64);
    const auto link_gpr = block.terminator.link_gpr;
    if (link_gpr != 0u) {
        state.gpr[link_gpr].low64 =
            static_cast<std::uint64_t>(block.terminator.link_pc);
    }
    normalize_zero(state);
    const auto delay_result =
        execute_ir_sequence(block.terminator.delay_slot, context);
    if (!delay_result.ok()) {
        return map_block_execution_failure(delay_result);
    }
    return {R5900IrExecutionError::None, {}, target};
}
```

Do not read the target register after link or delay execution.

- [ ] **Step 4: Run reference GREEN and regression pair**

```powershell
cmake --build build --config Release --target `
  r5900_ir_indirect_transfer_executor_tests `
  r5900_ir_direct_transfer_executor_tests
ctest --test-dir build -C Release --output-on-failure -R "r5900_ir_(indirect_transfer_executor|direct_transfer_executor)_tests"
```

Expected: both PASS.

- [ ] **Step 5: Commit Task 2**

```bash
git add CMakeLists.txt src/recompiler/r5900_ir_executor.cpp tests/r5900_ir_indirect_transfer_executor_tests.cpp
git commit -m "feat: execute R5900 indirect transfers"
```

---

### Task 3: Native x64 indirect-transfer differential

**Files:**
- Modify: `src/recompiler/windows/r5900_x64_backend.cpp`
- Create: `tests/r5900_x64_indirect_transfer_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: reference semantics from Task 2 and existing x64 ABI `std::uint32_t (*)(R5900IrExecutionState*, R5900IrExecutionContext*)`.
- Produces: native `compile_r5900_ir_x64()` support for both indirect terminators, with target snapshot in `[rsp+0x30]`.

- [ ] **Step 1: Register the Windows-only differential target and write RED tests**

Inside `if(WIN32)` tests, after `r5900_x64_direct_transfer_windows_tests`:

```cmake
add_executable(r5900_x64_indirect_transfer_windows_tests
  tests/r5900_x64_indirect_transfer_windows_tests.cpp
)
target_link_libraries(r5900_x64_indirect_transfer_windows_tests PRIVATE
  b3r_recompiler_x64
)
add_test(NAME r5900_x64_indirect_transfer_windows_tests
  COMMAND r5900_x64_indirect_transfer_windows_tests)
```

The new test must construct identical reference/native initial states and compare:

1. JR low32 target while source high64 is non-zero.
2. JR delay mutates source after snapshot.
3. JALR ordinary `rd != rs`.
4. JALR alias `rd == rs`.
5. JALR `rd == 0`.
6. non-zero destination `high64` preservation.
7. delay reads new link.
8. delay overwrites link destination afterward.
9. representative body before indirect terminator.
10. `Store128` delay success.
11. `Store128` delay failure with link committed.

Use a local `MemoryProbe` callback identical in semantics to Task 2 and a helper such as:

```cpp
void expect_same_state(const R5900IrExecutionState& reference,
                       const R5900IrExecutionState& native,
                       const char* message) {
    expect(std::memcmp(&reference, &native, sizeof(reference)) == 0, message);
}
```

For success cases:

```cpp
auto compiled = compile_r5900_ir_x64(block);
expect(compiled.ok(), "indirect transfer must compile natively");
const auto reference_result = execute_r5900_ir_block(block, reference_context);
const auto native_result = compiled.block->execute(native_context);
expect(reference_result.error == native_result.error,
       "reference/native error must match");
expect(reference_result.next_pc == native_result.next_pc,
       "reference/native next PC must match");
expect_same_state(reference_state, native_state,
                  "reference/native CPU state must match");
```

For `Store128` failure, compare `MemoryAccessFailure`, CPU state, link commitment, and fault `{access, guest_pc, address, width_bytes}`; do not compare `next_pc` because both failure APIs return zero there.

- [ ] **Step 2: Run RED**

```powershell
cmake --build build --config Release --target r5900_x64_indirect_transfer_windows_tests
ctest --test-dir build -C Release --output-on-failure -R r5900_x64_indirect_transfer_windows_tests
```

Expected RED: first compile attempt fails with backend unsupported terminator. Build itself must succeed.

- [ ] **Step 3: Add stack-local snapshot emit helpers**

In `r5900_x64_backend.cpp` near other scalar emit helpers:

```cpp
void emit_store_eax_to_rsp_30(std::vector<std::uint8_t>& bytes) {
    // mov dword ptr [rsp+0x30], eax
    bytes.insert(bytes.end(), {0x89u, 0x44u, 0x24u, 0x30u});
}

void emit_load_eax_from_rsp_30(std::vector<std::uint8_t>& bytes) {
    // mov eax, dword ptr [rsp+0x30]
    bytes.insert(bytes.end(), {0x8bu, 0x44u, 0x24u, 0x30u});
}
```

Do not change the helper-frame size: the existing `sub rsp, 0x38` already reserves `[rsp+0x30..0x37]` outside the 32-byte shadow area and the saved state/context slots.

- [ ] **Step 4: Implement a dedicated indirect-transfer emitter**

Add:

```cpp
PendingX64Code compile_indirect_transfer_code(const R5900IrBlock& block) {
    constexpr bool helper_frame = true;

    std::vector<std::uint8_t> bytes;
    bytes.reserve(160u +
        block.body.size() * 128u +
        block.terminator.delay_slot.size() * 256u);

    emit_helper_frame_prologue(bytes);
    emit_zero_gpr0(bytes);

    const auto body_emitted = emit_ir_sequence(
        bytes, block.body, 0u, helper_frame);
    if (!body_emitted.ok()) {
        return pending_failure(body_emitted.error, body_emitted.message);
    }

    emit_load_eax_from_state(
        bytes, gpr_low64_offset(block.terminator.inputs[0].gpr_index));
    emit_store_eax_to_rsp_30(bytes);

    if (block.terminator.kind == R5900IrTerminatorKind::IndirectCall &&
        block.terminator.link_gpr != 0u) {
        emit_mov_eax_imm32(bytes, block.terminator.link_pc);
        emit_store_rax_to_state(
            bytes, gpr_low64_offset(block.terminator.link_gpr));
    }

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
    emit_load_eax_from_rsp_30(bytes);
    emit_helper_frame_epilogue(bytes);
    bytes.push_back(0xc3u);
    return publish_code(bytes);
}
```

The `Store128` emitter's existing failure path already restores `rsp` and returns immediately with `context.memory_fault.active`; do not add a second epilogue on that path.

- [ ] **Step 5: Route indirect terminators through the new emitter**

In the compile switch:

```cpp
case R5900IrTerminatorKind::IndirectJump:
case R5900IrTerminatorKind::IndirectCall:
    pending = compile_indirect_transfer_code(block);
    break;
```

Do not merge this with `compile_direct_transfer_code()`; the indirect path has a required runtime snapshot and mandatory frame even without memory helpers.

- [ ] **Step 6: Run differential GREEN plus direct/backend regressions**

```powershell
cmake --build build --config Release --target `
  r5900_x64_indirect_transfer_windows_tests `
  r5900_x64_direct_transfer_windows_tests `
  r5900_x64_backend_windows_tests `
  r5900_x64_store128_windows_tests
ctest --test-dir build -C Release --output-on-failure -R "r5900_x64_(indirect_transfer|direct_transfer|backend|store128)_windows_tests"
```

Expected: all PASS.

- [ ] **Step 7: Commit Task 3**

```bash
git add CMakeLists.txt src/recompiler/windows/r5900_x64_backend.cpp tests/r5900_x64_indirect_transfer_windows_tests.cpp
git commit -m "feat: emit native R5900 indirect transfers"
```

---

### Task 4: Dispatcher JR/JALR lowering, cache, and invalid targets

**Files:**
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Create: `tests/r5900_block_dispatcher_indirect_transfer_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: analyzer end kinds `IndirectJump`/`IndirectCall`, decoded `Jr`/`Jalr`, Task 1 IR kinds, Task 3 x64 compiler.
- Produces: dispatcher execution and cache support for final decoded `JR/JALR` only.

- [ ] **Step 1: Register dispatcher target and write RED cases**

Add after the direct-transfer dispatcher target:

```cmake
add_executable(r5900_block_dispatcher_indirect_transfer_windows_tests
  tests/r5900_block_dispatcher_indirect_transfer_windows_tests.cpp
)
target_link_libraries(r5900_block_dispatcher_indirect_transfer_windows_tests PRIVATE
  b3r_recompiler_dispatcher_x64
)
add_test(NAME r5900_block_dispatcher_indirect_transfer_windows_tests
  COMMAND r5900_block_dispatcher_indirect_transfer_windows_tests)
```

Build test fixtures using the same small synthetic ELF/memory-map helpers already present in `r5900_block_dispatcher_direct_transfer_windows_tests.cpp`. Include exact cases:

- `JR r5` at block entry with NOP delay, mapped target containing one eligible instruction then unsupported control flow.
- body `ADDIU` followed by `JR`.
- `JALR r9,r5` with delay reading `r9`.
- alias `JALR r5,r5` proving target uses old r5 and link state is `PC+8`.
- cache-hit test: execute the same `JR r5` code twice, first with `r5 = target_a`, second with `r5 = target_b`; second execution must increase `cache_hits`, not `recompilations`, and reach target_b.
- mutate terminator word and prove recompile.
- mutate delay word and prove recompile.
- `SQ` delay after JR -> `LoweringFailure` at delay PC.
- `SQ` delay after JALR -> `LoweringFailure` at delay PC.
- misaligned dynamic target: indirect block executes/counts, next analysis fails at returned target.
- unmapped dynamic target: same accounting contract.

For cache span, after the first compiled run inspect the public dispatcher cache metadata if already exposed; if no direct entry accessor exists, assert behavior through mutation at `terminator_pc`/`delay_pc` plus exact `guest_instruction_count`. Do not add a production cache-inspection API solely for the test; if the existing public `cache_size()` is insufficient, cover `end_pc_exclusive` through the same private-test pattern already used by the direct-transfer tests rather than expanding runtime API surface.

- [ ] **Step 2: Run RED**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_indirect_transfer_windows_tests
ctest --test-dir build -C Release --output-on-failure -R r5900_block_dispatcher_indirect_transfer_windows_tests
```

Expected RED: JR/JALR are still reported as `ControlFlow` boundaries with no indirect block execution.

- [ ] **Step 3: Extend supported-transfer recognition**

Near existing `has_supported_beq/j/jal`:

```cpp
const bool has_supported_jr =
    block.end_kind == analysis::R5900BlockEndKind::IndirectJump &&
    !block.instructions.empty() &&
    block.instructions.back().decoded.instruction == R5900Instruction::Jr;
const bool has_supported_jalr =
    block.end_kind == analysis::R5900BlockEndKind::IndirectCall &&
    !block.instructions.empty() &&
    block.instructions.back().decoded.instruction == R5900Instruction::Jalr;
const bool has_supported_transfer =
    has_supported_beq || has_supported_j || has_supported_jal ||
    has_supported_jr || has_supported_jalr;
```

This automatically keeps the final supported transfer out of `body_sites`, includes transfer+delay in `guest_words`, and prevents the old empty-body ControlFlow stop.

- [ ] **Step 4: Split direct-target lowering from indirect lowering**

Do **not** call `decoded.direct_target()` unconditionally for `has_supported_transfer`. Restructure terminator creation as:

```cpp
ir_block.terminator.guest_pc = transfer_site->pc;
ir_block.terminator.guest_raw = transfer_site->decoded.raw;

if (has_supported_beq || has_supported_j || has_supported_jal) {
    const auto target = transfer_site->decoded.direct_target(transfer_site->pc);
    if (!target.has_value()) {
        // existing AnalysisFailure path
    }

    if (has_supported_beq) {
        ir_block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;
        ir_block.terminator.inputs = {
            dispatcher_gpr(transfer_site->decoded.rs),
            dispatcher_gpr(transfer_site->decoded.rt),
        };
        ir_block.terminator.taken_pc = *target;
        ir_block.terminator.fallthrough_pc = transfer_site->pc + 8u;
    } else {
        ir_block.terminator.kind = has_supported_j
            ? R5900IrTerminatorKind::DirectJump
            : R5900IrTerminatorKind::DirectCall;
        ir_block.terminator.target_pc = *target;
        if (has_supported_jal) {
            ir_block.terminator.link_pc = transfer_site->pc + 8u;
        }
    }
} else if (has_supported_jr) {
    ir_block.terminator.kind = R5900IrTerminatorKind::IndirectJump;
    ir_block.terminator.inputs = {
        dispatcher_gpr(transfer_site->decoded.rs),
    };
} else {
    ir_block.terminator.kind = R5900IrTerminatorKind::IndirectCall;
    ir_block.terminator.inputs = {
        dispatcher_gpr(transfer_site->decoded.rs),
    };
    ir_block.terminator.link_gpr = transfer_site->decoded.rd;
    ir_block.terminator.link_pc = transfer_site->pc + 8u;
}
```

Do not copy or recompute the direct J/JAL target formula in dispatcher code.

- [ ] **Step 5: Generalize SQ delay rejection and preserve cache span**

Keep the existing `delay.decoded.instruction == Sq` guard before delay lowering. Replace the J/JAL-only wording with one message that covers every supported transfer, for example:

```cpp
"SQ in a supported control-transfer delay slot is outside dispatcher v0 scope"
```

Keep:

```cpp
replacement.end_pc_exclusive = has_supported_transfer
    ? transfer_site->pc + 8u
    : current_pc + static_cast<std::uint32_t>(body_sites.size() * 4u);
```

No dynamic GPR state is added to `fingerprint_guest_words()` or `CachedBlock` identity fields.

- [ ] **Step 6: Verify invalid target accounting explicitly**

For a one-block `JR + NOP` at `start_pc` with `r5 = 0x...02` misaligned, run with enough block budget for the next iteration and assert:

```cpp
expect(result.reason == R5900DispatchStopReason::AnalysisFailure,
       "misaligned indirect target must fail on following analysis");
expect(result.blocks_executed == 1u,
       "completed JR block must remain counted");
expect(result.instructions_executed == 2u,
       "JR and its delay must remain counted");
expect(result.next_pc == static_cast<std::uint32_t>(state.gpr[5].low64),
       "analysis failure must report exact dynamic target");
```

Repeat for an aligned unmapped target.

- [ ] **Step 7: Run dispatcher GREEN plus direct/SQ regressions**

```powershell
cmake --build build --config Release --target `
  r5900_block_dispatcher_indirect_transfer_windows_tests `
  r5900_block_dispatcher_direct_transfer_windows_tests `
  r5900_block_dispatcher_store128_windows_tests `
  r5900_block_dispatcher_windows_tests
ctest --test-dir build -C Release --output-on-failure -R "r5900_block_dispatcher_(indirect_transfer|direct_transfer|store128|windows)_tests"
```

Expected: all selected dispatcher tests PASS.

- [ ] **Step 8: Commit Task 4**

```bash
git add CMakeLists.txt src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_indirect_transfer_windows_tests.cpp
git commit -m "feat: dispatch R5900 indirect transfers"
```

---

### Task 5: Expand synthetic startup through JR and aliasing JALR

**Files:**
- Modify: `tests/r5900_block_dispatcher_startup_windows_tests.cpp`

**Interfaces:**
- Consumes: Task 4 dispatcher support.
- Produces: exact E2E contract `ControlFlow`, `next_pc=0x001001c4`, `blocks=7`, `instructions=94` while preserving startup SQ state.

- [ ] **Step 1: Convert the current startup assertions into the new RED expectation before changing the fixture**

Change only the expected terminal accounting first:

```cpp
expect(result.reason == R5900DispatchStopReason::ControlFlow,
       "synthetic startup must stop at unsupported BNE boundary");
expect(result.next_pc == 0x001001c4u,
       "synthetic startup must stop at BNE PC");
expect(result.blocks_executed == 7u,
       "synthetic startup must execute seven native blocks");
expect(result.instructions_executed == 94u,
       "synthetic startup must execute ninety-four guest instructions");
```

Run the startup target and confirm it fails because the old fixture still terminates at JR / has the old layout.

- [ ] **Step 2: Run RED startup**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_startup_windows_tests
ctest --test-dir build -C Release --output-on-failure -R r5900_block_dispatcher_startup_windows_tests
```

Expected RED: mismatch with the new exact endpoint/accounting, not a build failure.

- [ ] **Step 3: Replace the post-JAL synthetic layout with the approved addresses**

Keep all startup words through `JAL 0x001001a0` and its delay. Arrange exact words from `0x00100188` onward:

```text
0x00100188  LUI   r5, 0x0010
0x0010018c  ORI   r5, r5, 0x01c0
0x00100190  JALR  r5, r5
0x00100194  ADDIU r6, r5, 0
0x00100198  poison write
0x0010019c  poison/guard write
0x001001a0  ADDIU r24, r0, 0x0055
0x001001a4  JR    r31
0x001001a8  ADDIU r29, r0, 0x0077
0x001001ac  poison write
0x001001b0  poison write
0x001001b4  poison write
0x001001b8  poison write
0x001001bc  poison/guard write
0x001001c0  ADDIU r7, r0, 0x0066
0x001001c4  BNE   r0, r0, <any aligned immediate>
0x001001c8  NOP
```

Use existing helpers:

```cpp
words.push_back(i_type(0x0fu, 0u, 5u, 0x0010u));       // LUI r5, 0x0010
words.push_back(i_type(0x0du, 5u, 5u, 0x01c0u));       // ORI r5, r5, 0x01c0
words.push_back(r_type(5u, 0u, 5u, 0u, 0x09u));        // JALR r5, r5
words.push_back(i_type(0x09u, 5u, 6u, 0u));            // ADDIU r6, r5, 0
```

For BNE use opcode `0x05`; it must remain unsupported by dispatcher v0. Keep its delay mapped but unexecuted.

Assign poison writes to dedicated registers that are not r5/r6/r7/r22/r23/r24/r29/r31 and assert those registers remain zero. Do not place poison instructions at `0x001001a0..0x001001a8` because that region is the direct callee and JR delay.

- [ ] **Step 4: Add exact architectural state assertions**

Require:

```cpp
expect(state.gpr[22].low64 == 0x33u, "J delay result mismatch");
expect(state.gpr[23].low64 == 0x00100188u, "JAL delay must observe direct link");
expect(state.gpr[24].low64 == 0x55u, "direct callee entry mismatch");
expect(state.gpr[29].low64 == 0x77u, "JR delay must execute");
expect(state.gpr[31].low64 == 0x00100188u, "JR must preserve direct return address");
expect(state.gpr[5].low64 == 0x00100198u, "JALR alias link mismatch");
expect(state.gpr[6].low64 == 0x00100198u, "JALR delay must observe link");
expect(state.gpr[7].low64 == 0x66u, "indirect callee entry mismatch");
```

Keep the existing 16-zero-byte assertion at guest `0x004e2680` and surrounding-byte integrity check for SQ. Keep the external legal-ELF harness conservative; do not make it assert the synthetic BNE endpoint.

- [ ] **Step 5: Run E2E GREEN and focused control-flow suite**

```powershell
cmake --build build --config Release --target `
  r5900_block_dispatcher_startup_windows_tests `
  r5900_block_dispatcher_indirect_transfer_windows_tests `
  r5900_block_dispatcher_direct_transfer_windows_tests
ctest --test-dir build -C Release --output-on-failure -R "r5900_block_dispatcher_(startup|indirect_transfer|direct_transfer)_windows_tests"
```

Expected: startup reports exactly 7 blocks / 94 instructions and all selected tests PASS.

- [ ] **Step 6: Commit Task 5**

```bash
git add tests/r5900_block_dispatcher_startup_windows_tests.cpp
git commit -m "test: advance startup through R5900 indirect transfers"
```

---

### Task 6: Documentation, full CI, review, and integration readiness

**Files:**
- Modify: `README.md`
- Modify: `docs/PROGRESS.md`
- Verify: `.github/workflows/windows-ci.yml`
- Verify all files changed since base `27d553fcb0959407af3e6b13503c652458f7d8f1`

**Interfaces:**
- Consumes: Tasks 1–5 complete and green.
- Produces: milestone status `CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION`, fresh exact-SHA CI evidence, and a branch safe to fast-forward back to `feature/r5900-beq-delay-slot-v0` after user approval.

- [ ] **Step 1: Update README milestone wording**

State that the native startup infrastructure now supports:

```text
BEQ + delay slot
SQ guest-memory write
J/JAL + delay slot
JR/JALR + delay slot
```

State that the synthetic startup now reaches an unsupported `BNE` boundary after 7 native blocks / 94 guest instructions. Explicitly say the game does not boot and external legal-ELF validation has not been performed in this environment.

- [ ] **Step 2: Update PROGRESS with exact contracts**

Record:

- `IndirectJump` / `IndirectCall` IR support.
- JR low32 dynamic target snapshot before delay.
- JALR target snapshot before link, including `rd == rs`.
- arbitrary link GPR including `rd == 0`.
- low64-only link and high64 preservation.
- x64 target snapshot in `[rsp+0x30]` under the existing `0x38` Win64 frame.
- cache target-value independence.
- invalid dynamic target failure on following analysis.
- dispatcher SQ-delay rejection retained.
- synthetic exact endpoint: `BNE 0x001001c4`, 7 blocks, 94 instructions.
- next architectural boundary: BNE; legal-ELF post-SQ path still needs external inspection.

- [ ] **Step 3: Run full local Windows validation if a Windows workspace is available**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\frame_pacer_windows_tests.exe
.\build\Release\Burnout3PacingProbe.exe --seconds 1
```

Expected after four new focused targets: **47/47 CTest** (baseline 43 + 4 new tests), 0 failures. Pacing must retain `TARGET_HZ 120`, mean near 8.333 ms, high-resolution timer YES, and no >9/10/12 ms regressions under the existing CI acceptance behavior.

- [ ] **Step 4: Commit documentation**

```bash
git add README.md docs/PROGRESS.md
git commit -m "docs: record R5900 indirect transfer milestone"
```

- [ ] **Step 5: Push/verify the feature branch and require fresh GitHub Actions Windows CI for the exact HEAD**

The accepted CI job must run on `feature/r5900-indirect-jump-call-v0` at the exact final SHA and show:

```text
Configure: success
Build: success
Test: success
Frame pacing telemetry: success
Pacing probe smoke: success
Stage/validate analyzer package: success
Stage/validate pacing probe package: success
```

From job logs, explicitly verify:

```text
100% tests passed, 0 tests failed out of 47
r5900_ir_indirect_transfer_validation_tests ... Passed
r5900_ir_indirect_transfer_executor_tests ... Passed
r5900_x64_indirect_transfer_windows_tests ... Passed
r5900_block_dispatcher_indirect_transfer_windows_tests ... Passed
r5900_block_dispatcher_startup_windows_tests ... Passed
```

Also record the frame-pacing telemetry and 120-frame probe from that same exact-SHA run.

- [ ] **Step 6: Review the full feature diff against the base SHA**

Review from:

```text
base = 27d553fcb0959407af3e6b13503c652458f7d8f1
head = feature/r5900-indirect-jump-call-v0 final SHA
```

Specifically inspect:

- no direct-target calculation is duplicated for JR/JALR;
- target snapshot precedes JALR link;
- native snapshot local is outside Win64 shadow space;
- all native failure paths restore RSP exactly once;
- `rd == 0` never writes persistent GPR0 link state;
- `rd == rs` uses old source target;
- `high64` is never overwritten by link;
- cache fingerprint contains code only;
- runtime target changes do not trigger recompilation;
- `end_pc_exclusive` remains terminator+8;
- invalid targets are not pre-repaired;
- SQ delay restriction remains dispatcher-only;
- startup poison regions do not overlap the direct callee;
- docs contain no claim of game boot or external validation;
- no temporary workflow/script/patch files remain.

Fix any issue found, then repeat fresh exact-SHA CI before claiming completion.

- [ ] **Step 7: Completion checkpoint**

Only declare `R5900 JR/JALR v0` complete when:

```text
status = CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION
synthetic startup = 7 blocks / 94 instructions
synthetic stop = BNE at 0x001001c4
full CTest = 47/47 PASS
pacing/package checks = PASS
legal external ELF = NOT YET VALIDATED unless actually run
```

Do not fast-forward `feature/r5900-beq-delay-slot-v0` until the user explicitly selects integration after the verified feature-branch completion.