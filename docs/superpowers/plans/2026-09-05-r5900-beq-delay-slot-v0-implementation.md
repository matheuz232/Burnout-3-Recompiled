# R5900 BEQ + Delay Slot v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the Windows native R5900 path to execute ordinary `BEQ` with its architectural delay slot, cover taken and not-taken outcomes natively, and advance the Burnout 3 startup boundary from `0x00100130` to unsupported `SQ` at `0x00100160` after 81 guest instructions across two completed blocks.

**Architecture:** Introduce block-level IR containing a straight-line body plus a typed terminator. `BranchEqual64` snapshots the low-64-bit equality result before executing exactly one delay-slot IR instruction, and both the reference executor and Windows x64 backend return the selected guest `next_pc`. The dispatcher compiles/caches the full body + BEQ + delay slot, uses the returned PC for the next block lookup, and leaves all other control-flow families unsupported.

**Tech Stack:** C++20, CMake 3.25+, Windows 10/11 x64 ABI, Visual Studio 2022/MSVC, existing R5900 decoder/IR/reference executor/x64 backend/basic-block analyzer/dispatcher, GitHub Actions `windows-2022`.

**Spec:** `docs/superpowers/specs/2026-09-05-r5900-beq-delay-slot-v0-design.md`

## Global Constraints

- Branch: `feature/r5900-beq-delay-slot-v0`, based on `main` commit `f9337b977655763076fd2729883b401bd37f3b4a`.
- Implement only ordinary `BEQ`; `BEQL`, BNE/BLEZ/BGTZ/REGIMM branches and J/JAL/JR/JALR remain unsupported.
- Evaluate the BEQ predicate from GPR low64 values before executing the delay slot.
- Execute exactly one architectural delay-slot instruction for supported BEQ.
- Do not add guest PC to `R5900IrExecutionState`.
- Do not implement `SQ`, guest-memory load/store semantics, BSS clearing loops, syscalls/HLE, direct block chaining, graphics, audio, input, menu, gameplay, or boot.
- Preserve GPR0 normalization and all existing startup integer/MMI/COP1 semantics.
- Cache identity must cover body words, BEQ word, and delay-slot word with exact comparison plus FNV-1a.
- A failed analysis/lowering/validation/compile stage must not execute any part of the candidate block or publish a replacement cache entry.
- The user-supplied `SLUS_210.50` remains external; never commit executable bytes, dumps, assets, or derived bulk byte arrays.
- Synthetic tests must generate ISA encodings from fields/helpers rather than copying a proprietary game byte sequence.
- TDD is mandatory: each production behavior starts with a failing test, the failure is verified, then the minimum production change is written.

---

### Task 1: Add block-level IR and authoritative block validation

**Files:**
- Modify: `src/recompiler/r5900_ir.h`
- Modify: `src/recompiler/r5900_ir_validation.h`
- Modify: `src/recompiler/r5900_ir_validation.cpp`
- Create: `tests/r5900_ir_block_validation_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: existing `R5900IrInstruction`, `R5900IrOperand`, `R5900IrValidationResult`, and `validate_r5900_ir_instruction(...)`.
- Produces:
  - `enum class R5900IrTerminatorKind { Fallthrough, BranchEqual64 };`
  - `struct R5900IrTerminator` with `guest_pc`, `guest_raw`, `kind`, `inputs`, `taken_pc`, `fallthrough_pc`, and `delay_slot`.
  - `struct R5900IrBlock { std::vector<R5900IrInstruction> body; R5900IrTerminator terminator; };`
  - `validate_r5900_ir_block(const R5900IrBlock&) -> R5900IrValidationResult`.

- [ ] **Step 1: Register a focused block-validation test target**

Add this inside `if(B3R_BUILD_TESTS)` beside the existing IR tests:

```cmake
add_executable(r5900_ir_block_validation_tests
  tests/r5900_ir_block_validation_tests.cpp
)
target_link_libraries(r5900_ir_block_validation_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_block_validation_tests COMMAND r5900_ir_block_validation_tests)
```

Create `tests/r5900_ir_block_validation_tests.cpp` with the normal repository `fail/expect` harness and these helpers:

```cpp
#include "recompiler/r5900_ir_validation.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {
using namespace b3r::recompiler;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_block_validation_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

R5900IrOperand gpr(std::uint8_t index) {
    R5900IrOperand operand{};
    operand.kind = R5900IrOperandKind::Gpr;
    operand.gpr_index = index;
    return operand;
}

R5900IrOperand immediate(std::int64_t value) {
    R5900IrOperand operand{};
    operand.kind = R5900IrOperandKind::Immediate;
    operand.immediate = value;
    return operand;
}

R5900IrInstruction nop(std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::Nop;
    return ir;
}

R5900IrTerminator beq(std::uint32_t pc,
                      R5900IrOperand lhs,
                      R5900IrOperand rhs,
                      std::uint32_t taken,
                      std::uint32_t fallthrough) {
    R5900IrTerminator term{};
    term.guest_pc = pc;
    term.kind = R5900IrTerminatorKind::BranchEqual64;
    term.inputs = {lhs, rhs};
    term.taken_pc = taken;
    term.fallthrough_pc = fallthrough;
    term.delay_slot = {nop(pc + 4u)};
    return term;
}
} // namespace
```

The first `main()` assertion must construct:

```cpp
R5900IrBlock block{};
block.body = {nop(0x00102000u)};
block.terminator = beq(0x00102004u,
                       gpr(1u),
                       gpr(2u),
                       0x00102020u,
                       0x0010200cu);
expect(validate_r5900_ir_block(block).ok(),
       "valid BEQ block must validate");
```

- [ ] **Step 2: Run the new target and verify RED**

Run:

```powershell
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug --target r5900_ir_block_validation_tests
```

Expected: compilation fails because `R5900IrTerminator`, `R5900IrTerminatorKind`, `R5900IrBlock`, and `validate_r5900_ir_block` do not exist.

- [ ] **Step 3: Add the block IR data model only**

Append to `src/recompiler/r5900_ir.h` after `R5900IrInstruction`:

```cpp
enum class R5900IrTerminatorKind {
    Fallthrough = 0,
    BranchEqual64,
};

struct R5900IrTerminator {
    std::uint32_t guest_pc{};
    std::uint32_t guest_raw{};
    R5900IrTerminatorKind kind{R5900IrTerminatorKind::Fallthrough};
    std::vector<R5900IrOperand> inputs{};
    std::uint32_t taken_pc{};
    std::uint32_t fallthrough_pc{};
    std::vector<R5900IrInstruction> delay_slot{};
};

struct R5900IrBlock {
    std::vector<R5900IrInstruction> body{};
    R5900IrTerminator terminator{};
};
```

Declare in `src/recompiler/r5900_ir_validation.h`:

```cpp
[[nodiscard]] R5900IrValidationResult
validate_r5900_ir_block(const R5900IrBlock& block);
```

Do not implement the function yet.

- [ ] **Step 4: Run again and verify the RED moved to the missing validator implementation**

Run the same build command.

Expected: compile/link fails because `validate_r5900_ir_block` is declared but undefined.

- [ ] **Step 5: Implement the minimum valid-BEQ validator**

In `r5900_ir_validation.cpp`, implement block validation with these exact rules:

```cpp
R5900IrValidationResult validate_r5900_ir_block(const R5900IrBlock& block) {
    for (std::size_t i = 0; i < block.body.size(); ++i) {
        const auto result = validate_r5900_ir_instruction(block.body[i], i);
        if (!result.ok()) {
            return result;
        }
    }

    const auto& term = block.terminator;
    if ((term.guest_pc & 0x3u) != 0u ||
        (term.fallthrough_pc & 0x3u) != 0u ||
        (term.taken_pc & 0x3u) != 0u) {
        return {R5900IrValidationError::MalformedInstruction,
                "R5900 IR block terminator PCs must be 4-byte aligned"};
    }

    if (term.kind == R5900IrTerminatorKind::Fallthrough) {
        if (!term.inputs.empty() || !term.delay_slot.empty() || term.taken_pc != 0u) {
            return {R5900IrValidationError::MalformedInstruction,
                    "R5900 IR fallthrough terminator must not carry branch inputs/delay slot/taken target"};
        }
        return {};
    }

    if (term.kind != R5900IrTerminatorKind::BranchEqual64) {
        return {R5900IrValidationError::UnsupportedOpcode,
                "unsupported R5900 IR block terminator"};
    }

    if (term.inputs.size() != 2u ||
        term.inputs[0].kind != R5900IrOperandKind::Gpr ||
        term.inputs[1].kind != R5900IrOperandKind::Gpr) {
        return {R5900IrValidationError::MalformedInstruction,
                "BranchEqual64 requires exactly two GPR operands"};
    }
    if (term.inputs[0].gpr_index >= 32u || term.inputs[1].gpr_index >= 32u) {
        return {R5900IrValidationError::InvalidRegister,
                "BranchEqual64 GPR index is out of range"};
    }
    if (term.delay_slot.size() != 1u) {
        return {R5900IrValidationError::MalformedInstruction,
                "BranchEqual64 requires exactly one delay-slot IR instruction"};
    }

    const auto delay = validate_r5900_ir_instruction(term.delay_slot.front(), block.body.size());
    if (!delay.ok()) {
        return delay;
    }
    return {};
}
```

If the implementation uses the project's existing failure helper rather than aggregate returns, preserve these exact semantics and error categories.

- [ ] **Step 6: Expand the validation matrix before declaring GREEN**

In the same test file, add assertions for:

```cpp
{
    auto invalid = block;
    invalid.terminator.inputs = {gpr(1u)};
    expect(validate_r5900_ir_block(invalid).error ==
               R5900IrValidationError::MalformedInstruction,
           "BEQ with one input must be rejected");
}
{
    auto invalid = block;
    invalid.terminator.inputs[1] = immediate(0);
    expect(validate_r5900_ir_block(invalid).error ==
               R5900IrValidationError::MalformedInstruction,
           "BEQ immediate operand must be rejected");
}
{
    auto invalid = block;
    invalid.terminator.inputs[0] = gpr(32u);
    expect(validate_r5900_ir_block(invalid).error ==
               R5900IrValidationError::InvalidRegister,
           "BEQ GPR32 must be rejected");
}
{
    auto invalid = block;
    invalid.terminator.taken_pc |= 2u;
    expect(validate_r5900_ir_block(invalid).error ==
               R5900IrValidationError::MalformedInstruction,
           "unaligned taken target must be rejected");
}
{
    auto invalid = block;
    invalid.terminator.fallthrough_pc |= 2u;
    expect(validate_r5900_ir_block(invalid).error ==
               R5900IrValidationError::MalformedInstruction,
           "unaligned fallthrough target must be rejected");
}
{
    auto invalid = block;
    invalid.terminator.delay_slot.clear();
    expect(validate_r5900_ir_block(invalid).error ==
               R5900IrValidationError::MalformedInstruction,
           "missing delay slot must be rejected");
}
{
    auto invalid = block;
    invalid.terminator.delay_slot.push_back(nop(0x0010200cu));
    expect(validate_r5900_ir_block(invalid).error ==
               R5900IrValidationError::MalformedInstruction,
           "multiple delay-slot IR instructions must be rejected");
}
```

Also create a malformed delay-slot IR with an invalid opcode value:

```cpp
auto bad_delay = nop(0x00102008u);
bad_delay.opcode = static_cast<R5900IrOpcode>(0xffu);
auto invalid = block;
invalid.terminator.delay_slot = {bad_delay};
expect(validate_r5900_ir_block(invalid).error ==
           R5900IrValidationError::UnsupportedOpcode,
       "unsupported delay-slot IR must be rejected");
```

- [ ] **Step 7: Run focused test and full portable IR tests**

Run:

```powershell
cmake --build --preset vs2022-debug --target r5900_ir_block_validation_tests r5900_ir_validation_tests
ctest --preset vs2022-debug -R "r5900_ir_(block_validation|validation)_tests" --output-on-failure
```

Expected: both tests pass.

- [ ] **Step 8: Commit Task 1**

```bash
git add CMakeLists.txt src/recompiler/r5900_ir.h src/recompiler/r5900_ir_validation.h src/recompiler/r5900_ir_validation.cpp tests/r5900_ir_block_validation_tests.cpp
git commit -m "feat: add R5900 block IR validation"
```

---

### Task 2: Lower scalar `AND rd,rs,rt` through existing `And64`

**Files:**
- Modify: `src/recompiler/r5900_ir.cpp`
- Modify: `src/recompiler/r5900_ir_validation.cpp`
- Modify: `tests/r5900_ir_tests.cpp`
- Modify: `tests/r5900_ir_validation_tests.cpp`
- Modify: `tests/r5900_ir_executor_tests.cpp`
- Modify: `tests/r5900_x64_startup_integer_windows_tests.cpp`

**Interfaces:**
- Consumes: existing `R5900IrOpcode::And64`, GPR operands, `Low64PreserveUpper64`, reference executor and x64 emitter.
- Produces: `R5900Instruction::And` lowering to `And64 Gpr(rd) <- Gpr(rs) & Gpr(rt)` without adding a new IR opcode.

- [ ] **Step 1: Add the lowering RED**

In `tests/r5900_ir_tests.cpp`, use the existing R-type encoder/helper and assert:

```cpp
const auto decoded = decode_r5900(r_type(3u, 4u, 5u, 0u, 0x24u));
const auto lowered = lower_r5900_instruction(decoded, 0x00100154u);
expect(lowered.ok(), "AND must lower for BEQ startup continuation");
expect(lowered.instructions.size() == 1u, "AND must lower to one IR instruction");
const auto& ir = lowered.instructions.front();
expect(ir.opcode == R5900IrOpcode::And64, "AND must lower to And64");
expect(ir.destination.has_value() &&
           ir.destination->kind == R5900IrDestinationKind::Gpr &&
           ir.destination->index == 5u,
       "AND destination must be rd");
expect(ir.write_mode == R5900IrGprWriteMode::Low64PreserveUpper64,
       "AND must preserve destination high64");
expect(ir.inputs.size() == 2u &&
           ir.inputs[0].kind == R5900IrOperandKind::Gpr && ir.inputs[0].gpr_index == 3u &&
           ir.inputs[1].kind == R5900IrOperandKind::Gpr && ir.inputs[1].gpr_index == 4u,
       "AND operands must be rs and rt GPRs");
```

Also assert `AND rd=0` lowers to the existing provenance-preserving `Nop` behavior.

- [ ] **Step 2: Verify RED**

Run:

```powershell
cmake --build --preset vs2022-debug --target r5900_ir_tests
ctest --preset vs2022-debug -R r5900_ir_tests --output-on-failure
```

Expected: FAIL at `AND must lower for BEQ startup continuation` because scalar `And` currently has no lowering case.

- [ ] **Step 3: Add minimal scalar AND lowering**

In `lower_r5900_instruction(...)`, add:

```cpp
case R5900Instruction::And: {
    if (decoded.rd == 0u) {
        return discarded_gpr_zero_write(decoded, guest_pc);
    }

    auto ir = base_instruction(decoded, guest_pc, R5900IrOpcode::And64);
    set_low64_destination(ir, decoded.rd);
    ir.inputs.push_back(gpr(decoded.rs));
    ir.inputs.push_back(gpr(decoded.rt));
    result.instructions.push_back(ir);
    return result;
}
```

- [ ] **Step 4: Add validator RED for GPR+GPR And64**

In `tests/r5900_ir_validation_tests.cpp`, add:

```cpp
const auto valid_and_reg = make_ir(
    R5900IrOpcode::And64,
    {R5900IrDestinationKind::Gpr, 4u},
    R5900IrGprWriteMode::Low64PreserveUpper64,
    {gpr(3u), gpr(4u)},
    0x00100154u);
expect(validate_r5900_ir_instruction(valid_and_reg, 60u).ok(),
       "And64 GPR+GPR form must validate");
```

Keep the existing `And64 GPR+Immediate` test valid.

Run the validation test and expect it to fail because the second operand is currently required to be an immediate.

- [ ] **Step 5: Generalize only the And64 second operand rule**

Change the `And64` validator so:

```text
input[0] = GPR only
input[1] = GPR or Immediate
```

If input[1] is GPR, validate index `< 32`. Preserve destination GPR, `Low64PreserveUpper64`, and exact two-input requirements.

- [ ] **Step 6: Add reference and x64 differential coverage**

In `tests/r5900_ir_executor_tests.cpp`, execute:

```cpp
R5900IrExecutionState state{};
state.gpr[3] = {0x00ff00ff00ff00ffull, 0x1111111111111111ull};
state.gpr[4] = {0x0f0f0f0f0f0f0f0full, 0x2222222222222222ull};
state.gpr[5] = {0u, 0xaaaaaaaaaaaaaaaaull};
```

with `And64 GPR5 <- GPR3,GPR4` and assert:

```cpp
state.gpr[5].low64 == 0x000f000f000f000full
state.gpr[5].high64 == 0xaaaaaaaaaaaaaaaaull
```

In `tests/r5900_x64_startup_integer_windows_tests.cpp`, add the same instruction to the existing differential `program`, then let `expect_states_equal` prove native/reference equality.

- [ ] **Step 7: Run focused + x64 tests**

```powershell
cmake --build --preset vs2022-debug --target r5900_ir_tests r5900_ir_validation_tests r5900_ir_executor_tests r5900_x64_startup_integer_windows_tests
ctest --preset vs2022-debug -R "r5900_(ir_tests|ir_validation_tests|ir_executor_tests|x64_startup_integer_windows_tests)" --output-on-failure
```

Expected: all pass.

- [ ] **Step 8: Commit Task 2**

```bash
git add src/recompiler/r5900_ir.cpp src/recompiler/r5900_ir_validation.cpp tests/r5900_ir_tests.cpp tests/r5900_ir_validation_tests.cpp tests/r5900_ir_executor_tests.cpp tests/r5900_x64_startup_integer_windows_tests.cpp
git commit -m "feat: lower scalar R5900 AND"
```

---

### Task 3: Add the block-level reference executor and prove delay-slot ordering

**Files:**
- Modify: `src/recompiler/r5900_ir_executor.h`
- Modify: `src/recompiler/r5900_ir_executor.cpp`
- Create: `tests/r5900_ir_block_executor_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `R5900IrBlock`, `validate_r5900_ir_block(...)`, existing `execute_r5900_ir(...)` instruction-vector semantics.
- Produces:

```cpp
struct R5900IrBlockExecutionResult {
    R5900IrExecutionError error{R5900IrExecutionError::None};
    std::string message{};
    std::uint32_t next_pc{};

    [[nodiscard]] bool ok() const noexcept {
        return error == R5900IrExecutionError::None;
    }
};

[[nodiscard]] R5900IrBlockExecutionResult
execute_r5900_ir_block(const R5900IrBlock& block,
                       R5900IrExecutionState& state);
```

- [ ] **Step 1: Add the block executor target and RED test**

Register:

```cmake
add_executable(r5900_ir_block_executor_tests
  tests/r5900_ir_block_executor_tests.cpp
)
target_link_libraries(r5900_ir_block_executor_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_block_executor_tests COMMAND r5900_ir_block_executor_tests)
```

Create the test with helpers `gpr`, `immediate`, `make_addiu_ir`, and `make_beq_block`. The critical taken-before-delay case must be:

```cpp
R5900IrExecutionState state{};
state.gpr[1].low64 = 5u;
state.gpr[2].low64 = 5u;

R5900IrBlock block{};
block.terminator.guest_pc = 0x00103000u;
block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;
block.terminator.inputs = {gpr(1u), gpr(2u)};
block.terminator.taken_pc = 0x00103040u;
block.terminator.fallthrough_pc = 0x00103008u;
block.terminator.delay_slot = {
    make_addiu_ir(1u, 1u, 1, 0x00103004u),
};

const auto result = execute_r5900_ir_block(block, state);
expect(result.ok(), "taken BEQ block must execute");
expect(result.next_pc == 0x00103040u,
       "BEQ decision must be captured before delay slot modifies rs");
expect(state.gpr[1].low64 == 6u,
       "taken BEQ delay slot must execute exactly once");
```

`make_addiu_ir(rt, rs, imm, pc)` must construct `AddWordSignExtend`, destination `rt`, `Low64PreserveUpper64`, inputs `{gpr(rs), immediate(imm)}`.

- [ ] **Step 2: Verify RED**

Build target. Expected: compile fails because `R5900IrBlockExecutionResult` and `execute_r5900_ir_block` do not exist.

- [ ] **Step 3: Add the block execution contract to the header**

Add the exact struct/signature from **Interfaces** to `r5900_ir_executor.h`.

- [ ] **Step 4: Implement fail-before-mutation validation and branch execution**

In `r5900_ir_executor.cpp`:

1. call `validate_r5900_ir_block(block)` before mutating `state`;
2. map `R5900IrValidationError` to the existing `R5900IrExecutionError` categories using the same mapping as instruction execution;
3. execute `block.body` through `execute_r5900_ir`;
4. for `Fallthrough`, return `block.terminator.fallthrough_pc`;
5. for `BranchEqual64`, compute:

```cpp
const bool taken =
    state.gpr[block.terminator.inputs[0].gpr_index].low64 ==
    state.gpr[block.terminator.inputs[1].gpr_index].low64;
```

6. execute `block.terminator.delay_slot` through `execute_r5900_ir`;
7. return the already-computed outcome:

```cpp
result.next_pc = taken ? block.terminator.taken_pc
                       : block.terminator.fallthrough_pc;
```

Do not re-read branch operands after delay execution.

- [ ] **Step 5: Add not-taken, high64, r0, and rt-mutating cases**

Add four tests:

1. `r1.low64=5`, `r2.low64=6`, delay increments `r1`; expect fallthrough.
2. `r1.low64 == r2.low64`, but high64 differ; expect taken.
3. `r0 == r0`; expect taken and GPR0 normalized to zero after a delay-slot instruction that tries to write r0.
4. equality true initially, delay slot increments `rt`; expect taken and incremented `rt`.

For the malformed atomicity case, initialize a sentinel state, set `block.terminator.delay_slot.front().opcode = static_cast<R5900IrOpcode>(0xffu)`, execute, and assert the whole state remains byte-for-byte equal to the sentinel because validation occurs before body mutation.

- [ ] **Step 6: Run focused tests**

```powershell
cmake --build --preset vs2022-debug --target r5900_ir_block_executor_tests r5900_ir_executor_tests
ctest --preset vs2022-debug -R "r5900_ir_(block_executor|executor)_tests" --output-on-failure
```

Expected: both pass.

- [ ] **Step 7: Commit Task 3**

```bash
git add CMakeLists.txt src/recompiler/r5900_ir_executor.h src/recompiler/r5900_ir_executor.cpp tests/r5900_ir_block_executor_tests.cpp
git commit -m "feat: execute R5900 block terminators"
```

---

### Task 4: Change the x64 block ABI to return next PC and emit native BEQ paths

**Files:**
- Modify: `src/recompiler/windows/r5900_x64_backend.h`
- Modify: `src/recompiler/windows/r5900_x64_backend.cpp`
- Modify: `tests/r5900_x64_backend_windows_tests.cpp`
- Modify: `tests/r5900_x64_startup_integer_windows_tests.cpp`
- Modify: `tests/r5900_x64_startup_cop1_windows_tests.cpp`
- Create: `tests/r5900_x64_beq_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `R5900IrBlock`, block validator, block reference executor, existing Windows machine-code emitter.
- Produces:

```cpp
[[nodiscard]] R5900X64CompileResult
compile_r5900_ir_x64(const R5900IrBlock& block);

std::uint32_t R5900X64CompiledBlock::execute(
    R5900IrExecutionState& state) const noexcept;
```

The existing `compile_r5900_ir_x64(const std::vector<R5900IrInstruction>&)` remains and internally constructs a `Fallthrough` block. Empty vector returns next PC `0`; non-empty returns `instructions.back().guest_pc + 4u`.

- [ ] **Step 1: Add a RED for the return-value ABI while preserving old vector compilation**

Change an existing backend test from:

```cpp
compiled.block->execute(actual);
```

to:

```cpp
const auto next_pc = compiled.block->execute(actual);
expect(next_pc == program.back().guest_pc + 4u,
       "vector compatibility compile must return fallthrough PC");
```

For the existing empty-program test, assert `execute(state) == 0u`.

- [ ] **Step 2: Verify RED**

Build `r5900_x64_backend_windows_tests`.

Expected: compile fails because `execute(...)` returns `void`.

- [ ] **Step 3: Change generated function ABI and vector compatibility path**

In the header, change `execute` to return `std::uint32_t` and add the block overload declaration.

In the implementation, change:

```cpp
using GeneratedFunction = void (*)(R5900IrExecutionState*);
```

to:

```cpp
using GeneratedFunction = std::uint32_t (*)(R5900IrExecutionState*);
```

Return `function(&state)`.

Refactor the existing vector compiler into:

```cpp
R5900X64CompileResult compile_r5900_ir_x64(
    const std::vector<R5900IrInstruction>& instructions) {
    R5900IrBlock block{};
    block.body = instructions;
    block.terminator.kind = R5900IrTerminatorKind::Fallthrough;
    block.terminator.fallthrough_pc =
        instructions.empty() ? 0u : instructions.back().guest_pc + 4u;
    return compile_r5900_ir_x64(block);
}
```

The new block compiler must validate the full block first.

- [ ] **Step 4: Refactor single-instruction emission into a reusable helper**

Move the existing opcode switch into a private helper with a concrete contract:

```cpp
std::optional<R5900X64CompileResult>
emit_ir_instruction(std::vector<std::uint8_t>& bytes,
                    const R5900IrInstruction& instruction,
                    std::size_t instruction_index);
```

Return `std::nullopt` on successful emission; return a populated compile failure for an unsupported opcode. Use it for body emission and for each delay-slot copy. Do not change opcode semantics during this refactor.

- [ ] **Step 5: Add the native BEQ differential RED**

Register:

```cmake
add_executable(r5900_x64_beq_windows_tests
  tests/r5900_x64_beq_windows_tests.cpp
)
target_link_libraries(r5900_x64_beq_windows_tests PRIVATE b3r_recompiler_x64)
add_test(NAME r5900_x64_beq_windows_tests COMMAND r5900_x64_beq_windows_tests)
```

The new test must include `expect_states_equal` identical in coverage to `r5900_x64_startup_integer_windows_tests.cpp`, plus helpers to make GPR operands, ADDIU delay IR, and a BEQ block.

First scenario:

```cpp
R5900IrExecutionState initial{};
initial.gpr[1] = {5u, 0x1111111111111111ull};
initial.gpr[2] = {5u, 0x2222222222222222ull};

const auto block = make_beq_block(
    0x00105000u,
    1u,
    2u,
    0x00105040u,
    0x00105008u,
    make_addiu_ir(1u, 1u, 1, 0x00105004u));

auto expected = initial;
auto actual = initial;
const auto reference = execute_r5900_ir_block(block, expected);
expect(reference.ok(), "reference BEQ block must execute");

auto compiled = compile_r5900_ir_x64(block);
expect(compiled.ok(), "x64 backend must compile BEQ block");
const auto native_next = compiled.block->execute(actual);
expect(native_next == reference.next_pc, "native taken next_pc mismatch");
expect_states_equal(expected, actual);
```

- [ ] **Step 6: Verify BEQ RED**

Run the new test. Expected: compile result fails/returns `UnsupportedOpcode` because block terminators are not yet emitted.

- [ ] **Step 7: Emit native BranchEqual64 with predicate-before-delay ordering**

Add private emit helpers:

```cpp
void emit_cmp_rax_rdx(std::vector<std::uint8_t>& bytes) {
    bytes.push_back(0x48u);
    bytes.push_back(0x39u);
    bytes.push_back(0xd0u); // cmp rax, rdx
}

std::size_t emit_jne_rel32_placeholder(std::vector<std::uint8_t>& bytes) {
    bytes.push_back(0x0fu);
    bytes.push_back(0x85u);
    const auto displacement_offset = bytes.size();
    emit_u32(bytes, 0u);
    return displacement_offset;
}

void patch_rel32(std::vector<std::uint8_t>& bytes,
                 std::size_t displacement_offset,
                 std::size_t target_offset) {
    const auto next_instruction = displacement_offset + 4u;
    const auto displacement = static_cast<std::int64_t>(target_offset) -
                              static_cast<std::int64_t>(next_instruction);
    const auto rel32 = static_cast<std::int32_t>(displacement);
    bytes[displacement_offset + 0u] = static_cast<std::uint8_t>(rel32 & 0xff);
    bytes[displacement_offset + 1u] = static_cast<std::uint8_t>((rel32 >> 8) & 0xff);
    bytes[displacement_offset + 2u] = static_cast<std::uint8_t>((rel32 >> 16) & 0xff);
    bytes[displacement_offset + 3u] = static_cast<std::uint8_t>((rel32 >> 24) & 0xff);
}
```

For `BranchEqual64` emit in this order:

```cpp
emit_load_rax_from_state(bytes, gpr_low64_offset(rs));
emit_load_rdx_from_state(bytes, gpr_low64_offset(rt));
emit_cmp_rax_rdx(bytes);
const auto jne_patch = emit_jne_rel32_placeholder(bytes);

// taken path
emit_ir_instruction(bytes, delay_slot, body_count);
emit_zero_gpr0(bytes);
emit_mov_eax_imm32(bytes, block.terminator.taken_pc);
bytes.push_back(0xc3u);

const auto not_taken_offset = bytes.size();
patch_rel32(bytes, jne_patch, not_taken_offset);

// not-taken path
emit_ir_instruction(bytes, delay_slot, body_count);
emit_zero_gpr0(bytes);
emit_mov_eax_imm32(bytes, block.terminator.fallthrough_pc);
bytes.push_back(0xc3u);
```

For `Fallthrough`, emit body, normalize GPR0, `mov eax, fallthrough_pc`, `ret`.

Do not execute the delay slot before the compare. Do not store a branch flag in `R5900IrExecutionState`.

- [ ] **Step 8: Expand differential scenarios**

In `r5900_x64_beq_windows_tests.cpp`, add:

- not taken (`low64` differ);
- equal `low64`, different `high64` => taken;
- `r0 == r0` => taken;
- delay modifies `rs` after an initially-true predicate => still taken;
- delay modifies `rt` after an initially-true predicate => still taken.

Every scenario compares full architectural state and `next_pc` against `execute_r5900_ir_block`.

- [ ] **Step 9: Run all x64 regressions**

```powershell
cmake --build --preset vs2022-debug --target r5900_x64_backend_windows_tests r5900_x64_startup_integer_windows_tests r5900_x64_startup_cop1_windows_tests r5900_x64_beq_windows_tests
ctest --preset vs2022-debug -R "r5900_x64_.*windows_tests" --output-on-failure
```

Expected: all pass.

- [ ] **Step 10: Commit Task 4**

```bash
git add CMakeLists.txt src/recompiler/windows/r5900_x64_backend.h src/recompiler/windows/r5900_x64_backend.cpp tests/r5900_x64_backend_windows_tests.cpp tests/r5900_x64_startup_integer_windows_tests.cpp tests/r5900_x64_startup_cop1_windows_tests.cpp tests/r5900_x64_beq_windows_tests.cpp
git commit -m "feat: emit native R5900 BEQ blocks"
```

---

### Task 5: Make the dispatcher execute/cache BEQ blocks and preserve stale-code semantics

**Files:**
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Modify: `src/recompiler/windows/r5900_block_dispatcher.h`
- Modify: `tests/r5900_block_dispatcher_windows_tests.cpp`

**Interfaces:**
- Consumes: analyzer `R5900BasicBlock` with `instructions`, `delay_slot`, `end_kind`, and edges; block IR; x64 block compiler returning `next_pc`.
- Produces: dispatcher support for ordinary BEQ while all other branch/jump families remain `ControlFlow` stops.

- [ ] **Step 1: Add a dispatcher RED for a branch-only block**

In `tests/r5900_block_dispatcher_windows_tests.cpp`, add a synthetic executable region whose first instruction is:

```cpp
i_type(0x04u, 1u, 2u, 1u) // BEQ r1,r2,+1
```

followed by:

```cpp
i_type(0x09u, 3u, 3u, 1u) // ADDIU r3,r3,1 delay slot
0u                         // taken target/fallthrough fixture instruction
```

Initialize `r1 == r2`, run with `max_blocks = 1`, and assert:

```cpp
result.reason == R5900DispatchStopReason::BlockBudgetExhausted
result.blocks_executed == 1u
result.instructions_executed == 2u
result.next_pc == base + 12u
state.gpr[3].low64 == 1u
```

This specifically proves an empty straight-line body does not cause the old `prefix.empty()` early return when the terminator is a supported BEQ.

- [ ] **Step 2: Verify RED**

Run only `r5900_block_dispatcher_windows_tests`.

Expected: FAIL because the dispatcher currently stops before control flow without executing BEQ/delay slot.

- [ ] **Step 3: Build a branch-capable candidate instead of a prefix-only candidate**

Refactor the dispatcher loop to classify the analyzed block:

1. collect eligible instructions before the control terminator into `body_sites`;
2. if the terminator is ordinary `BEQ`, require `block.delay_slot.has_value()` and treat it as a supported terminator;
3. if the terminator is any other branch/jump, keep `ControlFlow` stop behavior;
4. if a System instruction is reached before a supported terminator, keep `Trap`;
5. if an unsupported non-control instruction is reached, keep `UnsupportedInstruction`.

A supported BEQ block is allowed even when `body_sites` is empty.

- [ ] **Step 4: Lower the complete candidate atomically**

Before executing any native code:

```cpp
R5900IrBlock ir_block{};
```

Lower each `body_site` and append all produced instructions to `ir_block.body`.

For BEQ:

```cpp
ir_block.terminator.guest_pc = terminator.pc;
ir_block.terminator.guest_raw = terminator.decoded.raw;
ir_block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;
ir_block.terminator.inputs = {gpr(terminator.decoded.rs), gpr(terminator.decoded.rt)};
ir_block.terminator.taken_pc = *terminator.decoded.direct_target(terminator.pc);
ir_block.terminator.fallthrough_pc = terminator.pc + 8u;
```

Lower `*block.delay_slot` and require exactly one resulting IR instruction for this milestone; assign it to `terminator.delay_slot`. If delay lowering fails, return `LoweringFailure` with `next_pc = delay_slot.pc` before cache replacement or native execution.

For a straight-line non-branch candidate, construct a `Fallthrough` terminator with `fallthrough_pc = end of body`.

Use a small private helper in the dispatcher source to construct GPR operands for the terminator; do not expose a new public helper.

- [ ] **Step 5: Expand guest-word fingerprint coverage**

For BEQ candidates, `guest_words` must be:

```text
all body words in guest order
BEQ word
delay-slot word
```

Set `guest_instruction_count` to the same count. Set `end_pc_exclusive` to `terminator.pc + 8u` for branch blocks.

For straight-line candidates, preserve current word collection/count behavior.

- [ ] **Step 6: Use returned native PC and exact guest count for accounting**

Replace the old unconditional:

```cpp
native_block.execute(state);
current_pc += prefix.size() * 4u;
result.instructions_executed += prefix.size();
```

with:

```cpp
const auto next_pc = native_block.execute(state);
++result.blocks_executed;
result.instructions_executed += cached_block.guest_instruction_count;
current_pc = next_pc;
result.next_pc = next_pc;
```

Apply the same logic on cache hit, cache miss, and recompilation paths. Do not recompute branch outcome in C++.

- [ ] **Step 7: Add cache mutation RED/GREEN cases**

In the dispatcher test, create one BEQ block with a supported body and NOP delay slot. Execute once to populate cache, then prove independently:

1. mutate a body word -> `recompilations == 1`;
2. mutate the BEQ word while keeping it a supported BEQ -> `recompilations == 1`;
3. mutate the delay-slot word to another supported instruction -> `recompilations == 1`.

Then clear/rebuild the fixture, execute with GPRs equal, execute again with GPRs unequal without changing code, and assert:

```cpp
second.cache_hits == 1u
second.recompilations == 0u
first.next_pc != second.next_pc
```

This proves runtime branch outcome is dynamic and not part of the cache key.

- [ ] **Step 8: Add failure atomicity case for unsupported delay slot**

Use `XORI` as the delay slot of an otherwise supported BEQ. Put a supported body instruction before it that would visibly mutate state if executed. Assert:

```cpp
reason == R5900DispatchStopReason::LoweringFailure
blocks_executed == 0u
instructions_executed == 0u
state == initial_state
cache_size unchanged
```

- [ ] **Step 9: Run dispatcher regression suite**

```powershell
cmake --build --preset vs2022-debug --target r5900_block_dispatcher_windows_tests
ctest --preset vs2022-debug -R r5900_block_dispatcher_windows_tests --output-on-failure
```

Expected: pass, including all old cache/budget/stale/unsupported regressions.

- [ ] **Step 10: Commit Task 5**

```bash
git add src/recompiler/windows/r5900_block_dispatcher.h src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "feat: dispatch native R5900 BEQ blocks"
```

---

### Task 6: Prove the 81-instruction startup path, extend external validation, and document the milestone

**Files:**
- Modify: `tests/r5900_block_dispatcher_startup_windows_tests.cpp`
- Modify: `README.md`
- Modify: `docs/PROGRESS.md`

**Interfaces:**
- Consumes: completed BEQ-capable dispatcher and existing optional external-ELF argument path.
- Produces: synthetic E2E proof of two completed branch blocks and an external harness expecting the real boundary at `SQ 0x00100160`.

- [ ] **Step 1: Replace the old synthetic one-boundary expectation with a two-branch RED fixture**

Keep all encodings generated through existing helpers. Build a word vector whose addresses are explicitly checked.

Start with the existing 74-instruction synthetic body so:

```cpp
expect(base + static_cast<std::uint32_t>(words.size() * 4u) == 0x00100130u,
       "first synthetic BEQ PC mismatch");
```

Append first BEQ and delay:

```cpp
words.push_back(i_type(0x04u, 0u, 0u, 6u));      // 0x00100130, always taken to 0x0010014C
words.push_back(i_type(0x09u, 0u, 20u, 0x11u)); // 0x00100134, visible delay side effect
```

Append five skipped filler instructions for addresses `0x00100138` through `0x00100148`. Use generated `XORI` encodings so accidental fallthrough would fail loudly:

```cpp
for (std::uint8_t i = 0u; i < 5u; ++i) {
    words.push_back(i_type(0x0eu, 0u, static_cast<std::uint8_t>(10u + i), 1u));
}
```

Assert current PC is `0x0010014C`, then append:

```cpp
words.push_back(i_type(0x0fu, 0u, 4u, 0xffffu));            // LUI r4,0xffff
words.push_back(i_type(0x0du, 4u, 4u, 0xfff0u));            // ORI r4,r4,0xfff0
words.push_back(r_type(3u, 4u, 4u, 0u, 0x24u));             // AND r4,r3,r4
words.push_back(i_type(0x04u, 2u, 4u, 7u));                 // BEQ r2,r4,+7, expected not taken
words.push_back(i_type(0x09u, 0u, 21u, 0x22u));             // visible second delay side effect
words.push_back(i_type(0x1fu, 2u, 0u, 0u));                 // SQ blocker at 0x00100160
```

The synthetic first 74 instructions must still establish `r2 = 0x004e2680` and `r3 = 0x01ecea00`. The second-body mask computes `r4 = 0x01ecea00`, so `r2 != r4` and the second BEQ is not taken.

Run dispatcher with `max_blocks = 3` and change expectations to:

```cpp
expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction,
       "synthetic startup must stop at SQ");
expect(result.next_pc == 0x00100160u,
       "synthetic startup SQ boundary mismatch");
expect(result.blocks_executed == 2u,
       "synthetic startup must complete two BEQ blocks");
expect(result.instructions_executed == 81u,
       "synthetic startup must execute exactly 81 guest instructions");
expect(state.gpr[20].low64 == 0x11u,
       "first BEQ delay slot must execute");
expect(state.gpr[21].low64 == 0x22u,
       "second BEQ delay slot must execute");
expect(state.gpr[4].low64 == 0x0000000001ecea00ull,
       "second startup block AND result mismatch");
```

- [ ] **Step 2: Verify E2E RED before changing the external harness**

Run:

```powershell
cmake --build --preset vs2022-debug --target r5900_block_dispatcher_startup_windows_tests
ctest --preset vs2022-debug -R r5900_block_dispatcher_startup_windows_tests --output-on-failure
```

Expected before Tasks 1-5 are complete: failure at the old control-flow boundary. When executing this task after Tasks 1-5, this step should already be GREEN; document that the earlier Task 5 RED/individual branch tests were the causal RED for production behavior, and do not deliberately break production to recreate a failure.

- [ ] **Step 3: Extend `validate_external_startup` to the real `0x00100160` boundary**

Keep file loading/parsing/mapping unchanged. Change dispatcher run to:

```cpp
const auto result = dispatcher.run(parsed.image->entry_point(), state, 3u);
```

Replace old boundary assertions with:

```cpp
expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction,
       "real startup dispatch must stop at unsupported SQ");
expect(result.next_pc == 0x00100160u,
       "real startup SQ boundary mismatch");
expect(result.blocks_executed == 2u && result.instructions_executed == 81u,
       "real startup dispatcher must execute exactly 81 instructions across two blocks");
expect(state.gpr[2].low64 == 0x00000000004e2680ull,
       "real startup r2 result mismatch");
expect(state.gpr[3].low64 == 0x0000000001ecea00ull,
       "real startup r3 result mismatch");
expect(state.gpr[4].low64 == 0x0000000001ecea00ull,
       "real startup second-block AND result mismatch");
```

Keep the existing HI/LO/SA/FPR/FCR31/FP_ACC assertions. Update the success line to:

```cpp
std::cout << "REAL_ELF_STARTUP_VALIDATED start=0x00100008 stop=0x00100160 instructions=81 blocks=2\n";
```

No real ELF is added to CTest or CI.

- [ ] **Step 4: Run the full Windows CI-equivalent test suite**

```powershell
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug --output-on-failure
```

Expected: all tests pass. Also run the repository's existing pacing/package workflow through GitHub Actions after commit; hosted pacing remains infrastructure evidence only, not game simulation timing evidence.

- [ ] **Step 5: If a legal external ELF is available on a Windows x64 host, run the optional harness**

Command:

```powershell
.\build\vs2022-debug\Debug\r5900_block_dispatcher_startup_windows_tests.exe "D:\Games\Burnout3\SLUS_210.50"
```

Expected final external line:

```text
REAL_ELF_STARTUP_VALIDATED start=0x00100008 stop=0x00100160 instructions=81 blocks=2
```

If this external run cannot be performed, document status as `CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION`; do not claim `EXTERNALLY_VALIDATED`.

- [ ] **Step 6: Update README accurately**

Change the current milestone text to state that ordinary native `BEQ` and architectural delay slots are implemented for the first startup continuation, and that the synthetic native path reaches unsupported `SQ` at `0x00100160` after 81 instructions / 2 blocks.

Explicitly retain:

```text
guest-memory loads/stores, SQ, broader control flow, syscalls, game initialization, graphics, audio, input, menus, and gameplay remain unimplemented.
```

Do not say the game boots.

- [ ] **Step 7: Update `docs/PROGRESS.md` with evidence and next gate**

Add/update rows so they state:

```text
R5900 BEQ + delay slot v0 | CI_VALIDATED | Native BranchEqual64 returns next_pc, executes exactly one delay slot after predicate capture, and passes reference/x64 differential tests for taken/not-taken/high64/r0/source-mutating cases.

R5900 startup execution | CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION | Synthetic path completes 2 blocks / 81 instructions and stops at SQ 0x00100160. External Windows harness accepts a legal local ELF; EXTERNALLY_VALIDATED only after that harness actually passes on the user-supplied file.
```

Record the final GitHub Actions run ID only after it completes successfully.

- [ ] **Step 8: Commit Task 6**

```bash
git add tests/r5900_block_dispatcher_startup_windows_tests.cpp README.md docs/PROGRESS.md
git commit -m "docs: record R5900 BEQ startup continuation"
```

- [ ] **Step 9: Final verification before integration**

Run/verify all of the following on the final branch head:

```powershell
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug --output-on-failure
```

Then verify the Windows GitHub Actions workflow is green for the same head SHA, including configure, build, tests, frame-pacing telemetry, pacing-probe smoke, analyzer package validation, and pacing-probe package validation.

Inspect `git diff main...feature/r5900-beq-delay-slot-v0 --stat` and confirm no proprietary file, ELF byte dump, binary asset, or unrelated subsystem change is present.

Do not merge to `main` until the user selects an integration option after final verification.
