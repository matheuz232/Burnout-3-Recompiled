# R5900 BEQ + Delay Slot v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute the first two real Burnout 3 startup `BEQ` instructions and both architectural delay slots natively, then stop at unsupported `SQ` at guest PC `0x00100160` after 81 guest instructions across two completed blocks.

**Architecture:** Add block-level IR (`body + terminator`) while preserving instruction-level IR. `BranchEqual64` compares GPR low64 values before the delay slot, both the reference executor and x64 backend return `next_pc`, and the dispatcher caches/fingerprints the complete body + BEQ + delay slot. Scalar `AND rd,rs,rt` reuses existing `And64`; no guest-memory or broader branch family is introduced.

**Tech Stack:** C++20, CMake 3.25+, Visual Studio 2022/MSVC, Windows x64 ABI, existing R5900 decoder/IR/reference executor/x64 backend/control-flow analyzer/dispatcher, GitHub Actions `windows-2022`.

**Spec:** `docs/superpowers/specs/2026-09-05-r5900-beq-delay-slot-v0-design.md`

## Global Constraints

- Work only on `feature/r5900-beq-delay-slot-v0`, based on `main` commit `f9337b977655763076fd2729883b401bd37f3b4a`.
- Support only ordinary `BEQ` in this milestone. `BEQL`, BNE/BLEZ/BGTZ/REGIMM branches and J/JAL/JR/JALR remain unsupported.
- Evaluate BEQ from GPR low64 values before executing the delay slot.
- Execute exactly one delay-slot instruction for supported BEQ.
- Do not add guest PC to `R5900IrExecutionState`.
- Do not implement `SQ`, guest-memory loads/stores, BSS loops, syscalls/HLE, direct block chaining, graphics, audio, input, menu, gameplay, or boot.
- Preserve GPR0 normalization and all existing integer/MMI/COP1 semantics.
- Cache identity for a branch-capable block must include body words, BEQ word, and delay-slot word through the existing exact-word comparison plus FNV-1a.
- Analysis/lowering/validation/compile failure must occur before any part of that candidate block executes or replaces a cache entry.
- Never commit `SLUS_210.50`, game assets, executable bytes, binary dumps, or bulk derived byte arrays.
- Synthetic tests must generate encodings from ISA fields/helpers.
- Use RED -> verify RED -> minimal GREEN -> verify GREEN -> commit for every production behavior.

---

### Task 1: Add block IR and block validation

**Files:**
- Modify: `src/recompiler/r5900_ir.h`
- Modify: `src/recompiler/r5900_ir_validation.h`
- Modify: `src/recompiler/r5900_ir_validation.cpp`
- Create: `tests/r5900_ir_block_validation_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `R5900IrInstruction`, `R5900IrOperand`, `validate_r5900_ir_instruction(...)`.
- Produces:

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

[[nodiscard]] R5900IrValidationResult
validate_r5900_ir_block(const R5900IrBlock& block);
```

- [ ] **Step 1: Register and write the failing block-validation test**

Add to `CMakeLists.txt` near the existing IR tests:

```cmake
add_executable(r5900_ir_block_validation_tests
  tests/r5900_ir_block_validation_tests.cpp
)
target_link_libraries(r5900_ir_block_validation_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_block_validation_tests COMMAND r5900_ir_block_validation_tests)
```

Create `tests/r5900_ir_block_validation_tests.cpp` with `fail/expect` plus:

```cpp
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
```

First test:

```cpp
R5900IrBlock block{};
block.body = {nop(0x00102000u)};
block.terminator.guest_pc = 0x00102004u;
block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;
block.terminator.inputs = {gpr(1u), gpr(2u)};
block.terminator.taken_pc = 0x00102020u;
block.terminator.fallthrough_pc = 0x0010200cu;
block.terminator.delay_slot = {nop(0x00102008u)};
expect(validate_r5900_ir_block(block).ok(), "valid BEQ block must validate");
```

- [ ] **Step 2: Verify RED**

```powershell
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug --target r5900_ir_block_validation_tests
```

Expected: compile failure for missing block IR types/function.

- [ ] **Step 3: Add the data model and validator declaration**

Add exactly the interfaces listed above to `r5900_ir.h` and `r5900_ir_validation.h`. Do not add executor/backend behavior yet.

- [ ] **Step 4: Verify RED moved to missing validator implementation**

Rebuild target. Expected: unresolved `validate_r5900_ir_block`.

- [ ] **Step 5: Implement authoritative block validation**

`validate_r5900_ir_block` must:

```cpp
for (std::size_t i = 0; i < block.body.size(); ++i) {
    const auto result = validate_r5900_ir_instruction(block.body[i], i);
    if (!result.ok()) {
        return result;
    }
}
```

Then enforce:

```text
Fallthrough:
- no inputs
- no delay slot
- taken_pc == 0
- guest_pc/fallthrough_pc 4-byte aligned

BranchEqual64:
- exactly 2 operands
- both GPR
- both indices < 32
- guest_pc, taken_pc, fallthrough_pc 4-byte aligned
- exactly 1 delay-slot IR instruction
- delay-slot instruction itself validates
```

Use `MalformedInstruction` for structural/PC errors, `InvalidRegister` for register index errors, and `UnsupportedOpcode` for unknown terminator or delay-slot opcode.

- [ ] **Step 6: Expand the negative matrix**

Add tests that reject:

```cpp
// wrong input count
invalid.terminator.inputs = {gpr(1u)};

// wrong operand kind
invalid.terminator.inputs[1] = immediate(0);

// bad GPR
invalid.terminator.inputs[0] = gpr(32u);

// unaligned targets
invalid.terminator.taken_pc |= 2u;
invalid.terminator.fallthrough_pc |= 2u;

// missing / multiple delay slots
invalid.terminator.delay_slot.clear();
invalid.terminator.delay_slot = {nop(0x1008u), nop(0x100cu)};

// unsupported delay IR
auto bad_delay = nop(0x00102008u);
bad_delay.opcode = static_cast<R5900IrOpcode>(0xffu);
```

Also prove valid `BEQ r0,r0` passes.

- [ ] **Step 7: Verify GREEN**

```powershell
cmake --build --preset vs2022-debug --target r5900_ir_block_validation_tests r5900_ir_validation_tests
ctest --preset vs2022-debug -R "r5900_ir_(block_validation|validation)_tests" --output-on-failure
```

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt src/recompiler/r5900_ir.h src/recompiler/r5900_ir_validation.h src/recompiler/r5900_ir_validation.cpp tests/r5900_ir_block_validation_tests.cpp
git commit -m "feat: add R5900 block IR validation"
```

---

### Task 2: Add scalar `AND rd,rs,rt` using `And64`

**Files:**
- Modify: `src/recompiler/r5900_ir.cpp`
- Modify: `src/recompiler/r5900_ir_validation.cpp`
- Modify: `tests/r5900_ir_tests.cpp`
- Modify: `tests/r5900_ir_validation_tests.cpp`
- Modify: `tests/r5900_ir_executor_tests.cpp`
- Modify: `tests/r5900_x64_startup_integer_windows_tests.cpp`

**Interfaces:**
- Consumes: existing `R5900IrOpcode::And64`, reference executor, x64 And64 emitter.
- Produces: scalar `AND` lowering to low64-preserving `And64` with two GPR operands.

- [ ] **Step 1: Write lowering RED**

In `r5900_ir_tests.cpp` encode `AND r5,r3,r4` using the existing R-type helper and assert:

```cpp
const auto lowered = lower_r5900_instruction(decoded, 0x00100154u);
expect(lowered.ok(), "AND must lower");
expect(lowered.instructions.size() == 1u, "AND must lower to one IR instruction");
const auto& ir = lowered.instructions.front();
expect(ir.opcode == R5900IrOpcode::And64, "AND must use And64");
expect(ir.destination->kind == R5900IrDestinationKind::Gpr &&
       ir.destination->index == 5u,
       "AND destination must be rd");
expect(ir.write_mode == R5900IrGprWriteMode::Low64PreserveUpper64,
       "AND must preserve high64");
expect(ir.inputs.size() == 2u &&
       ir.inputs[0].kind == R5900IrOperandKind::Gpr && ir.inputs[0].gpr_index == 3u &&
       ir.inputs[1].kind == R5900IrOperandKind::Gpr && ir.inputs[1].gpr_index == 4u,
       "AND sources must be rs/rt");
```

Also assert `rd == 0` becomes the existing provenance-preserving `Nop`.

- [ ] **Step 2: Verify RED**

```powershell
cmake --build --preset vs2022-debug --target r5900_ir_tests
ctest --preset vs2022-debug -R r5900_ir_tests --output-on-failure
```

Expected: fail because scalar `And` is not lowered.

- [ ] **Step 3: Implement minimum lowering**

Add to `lower_r5900_instruction`:

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

- [ ] **Step 4: Write validator RED for And64 GPR+GPR**

Add:

```cpp
const auto valid_and_reg = make_ir(
    R5900IrOpcode::And64,
    {R5900IrDestinationKind::Gpr, 4u},
    R5900IrGprWriteMode::Low64PreserveUpper64,
    {gpr(3u), gpr(4u)},
    0x00100154u);
expect(validate_r5900_ir_instruction(valid_and_reg, 60u).ok(),
       "And64 GPR+GPR must validate");
```

Verify it fails under the current immediate-only second operand rule.

- [ ] **Step 5: Generalize only the second And64 operand**

Validator contract becomes:

```text
input[0] = GPR
input[1] = GPR or Immediate
```

Validate GPR index `<32`; preserve all destination/write-mode/count rules.

- [ ] **Step 6: Add executor and x64 differential coverage**

Reference state:

```cpp
state.gpr[3] = {0x00ff00ff00ff00ffull, 0x1111111111111111ull};
state.gpr[4] = {0x0f0f0f0f0f0f0f0full, 0x2222222222222222ull};
state.gpr[5] = {0u, 0xaaaaaaaaaaaaaaaaull};
```

Expected after `GPR5 = GPR3 & GPR4`:

```cpp
state.gpr[5].low64 == 0x000f000f000f000full
state.gpr[5].high64 == 0xaaaaaaaaaaaaaaaaull
```

Add the same IR instruction to the existing x64 integer differential program so `expect_states_equal` covers native/reference equality.

- [ ] **Step 7: Verify GREEN**

```powershell
cmake --build --preset vs2022-debug --target r5900_ir_tests r5900_ir_validation_tests r5900_ir_executor_tests r5900_x64_startup_integer_windows_tests
ctest --preset vs2022-debug -R "r5900_(ir_tests|ir_validation_tests|ir_executor_tests|x64_startup_integer_windows_tests)" --output-on-failure
```

- [ ] **Step 8: Commit**

```bash
git add src/recompiler/r5900_ir.cpp src/recompiler/r5900_ir_validation.cpp tests/r5900_ir_tests.cpp tests/r5900_ir_validation_tests.cpp tests/r5900_ir_executor_tests.cpp tests/r5900_x64_startup_integer_windows_tests.cpp
git commit -m "feat: lower scalar R5900 AND"
```

---

### Task 3: Add block-level reference execution

**Files:**
- Modify: `src/recompiler/r5900_ir_executor.h`
- Modify: `src/recompiler/r5900_ir_executor.cpp`
- Create: `tests/r5900_ir_block_executor_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
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

- [ ] **Step 1: Register the test target and write ordering RED**

Add:

```cmake
add_executable(r5900_ir_block_executor_tests
  tests/r5900_ir_block_executor_tests.cpp
)
target_link_libraries(r5900_ir_block_executor_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_block_executor_tests COMMAND r5900_ir_block_executor_tests)
```

Create helpers `gpr`, `immediate`, and:

```cpp
R5900IrInstruction addiu(std::uint8_t rt,
                         std::uint8_t rs,
                         std::int16_t imm,
                         std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::AddWordSignExtend;
    ir.destination = R5900IrDestination{R5900IrDestinationKind::Gpr, rt};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
    ir.inputs = {gpr(rs), immediate(imm)};
    return ir;
}
```

Critical test:

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
block.terminator.delay_slot = {addiu(1u, 1u, 1, 0x00103004u)};

const auto result = execute_r5900_ir_block(block, state);
expect(result.ok(), "taken BEQ block must execute");
expect(result.next_pc == 0x00103040u,
       "predicate must be captured before delay modifies rs");
expect(state.gpr[1].low64 == 6u,
       "delay slot must execute exactly once");
```

- [ ] **Step 2: Verify RED**

Expected: compile failure because block execution API is missing.

- [ ] **Step 3: Add API and minimal implementation**

Implementation order:

```text
1. validate_r5900_ir_block(block)
2. if invalid: map validation error and return without mutating state
3. execute block.body through execute_r5900_ir
4. Fallthrough => next_pc = terminator.fallthrough_pc
5. BranchEqual64 => snapshot low64 equality
6. execute terminator.delay_slot through execute_r5900_ir
7. return taken_pc/fallthrough_pc from the saved predicate
```

Do not re-read branch source registers after delay execution.

- [ ] **Step 4: Add full semantics matrix**

Add tests for:

```text
- not taken
- low64 equal / high64 different => taken
- r0 == r0 => taken
- delay modifies rs after initially true predicate => still taken
- delay modifies rt after initially true predicate => still taken
```

Add atomic validation test: clone an initialized sentinel state, put `static_cast<R5900IrOpcode>(0xffu)` in the delay IR, execute, expect error, then compare every state field explicitly.

Define and use this helper in the test:

```cpp
void expect_states_equal(const R5900IrExecutionState& expected,
                         const R5900IrExecutionState& actual) {
    for (std::size_t i = 0; i < expected.gpr.size(); ++i) {
        expect(expected.gpr[i].low64 == actual.gpr[i].low64, "GPR low64 mismatch");
        expect(expected.gpr[i].high64 == actual.gpr[i].high64, "GPR high64 mismatch");
    }
    expect(expected.hi == actual.hi, "HI mismatch");
    expect(expected.lo == actual.lo, "LO mismatch");
    expect(expected.hi1 == actual.hi1, "HI1 mismatch");
    expect(expected.lo1 == actual.lo1, "LO1 mismatch");
    expect(expected.sa == actual.sa, "SA mismatch");
    for (std::size_t i = 0; i < expected.fpr.size(); ++i) {
        expect(expected.fpr[i] == actual.fpr[i], "FPR mismatch");
    }
    expect(expected.fcr31 == actual.fcr31, "FCR31 mismatch");
    expect(expected.fp_acc == actual.fp_acc, "FP accumulator mismatch");
}
```

- [ ] **Step 5: Verify GREEN**

```powershell
cmake --build --preset vs2022-debug --target r5900_ir_block_executor_tests r5900_ir_executor_tests
ctest --preset vs2022-debug -R "r5900_ir_(block_executor|executor)_tests" --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/recompiler/r5900_ir_executor.h src/recompiler/r5900_ir_executor.cpp tests/r5900_ir_block_executor_tests.cpp
git commit -m "feat: execute R5900 block terminators"
```

---

### Task 4: Return `next_pc` from x64 blocks and emit native BEQ

**Files:**
- Modify: `src/recompiler/windows/r5900_x64_backend.h`
- Modify: `src/recompiler/windows/r5900_x64_backend.cpp`
- Modify: `tests/r5900_x64_backend_windows_tests.cpp`
- Modify: `tests/r5900_x64_startup_integer_windows_tests.cpp`
- Modify: `tests/r5900_x64_startup_cop1_windows_tests.cpp`
- Create: `tests/r5900_x64_beq_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
[[nodiscard]] R5900X64CompileResult
compile_r5900_ir_x64(const R5900IrBlock& block);

std::uint32_t R5900X64CompiledBlock::execute(
    R5900IrExecutionState& state) const noexcept;
```

Existing `compile_r5900_ir_x64(vector<IR>)` remains source-compatible and returns fallthrough `last.guest_pc + 4`; empty vector returns `0`.

- [ ] **Step 1: Write ABI RED**

Change an existing backend test to:

```cpp
const auto next_pc = compiled.block->execute(actual);
expect(next_pc == program.back().guest_pc + 4u,
       "vector compile must return fallthrough PC");
```

For empty IR assert `execute(state) == 0u`.

- [ ] **Step 2: Verify RED**

Expected: compile failure because `execute` returns `void`.

- [ ] **Step 3: Change generated ABI and vector wrapper**

Generated function type:

```cpp
using GeneratedFunction = std::uint32_t (*)(R5900IrExecutionState*);
```

`execute` returns `function(&state)`.

Vector overload constructs:

```cpp
R5900IrBlock block{};
block.body = instructions;
block.terminator.kind = R5900IrTerminatorKind::Fallthrough;
block.terminator.fallthrough_pc =
    instructions.empty() ? 0u : instructions.back().guest_pc + 4u;
return compile_r5900_ir_x64(block);
```

- [ ] **Step 4: Refactor instruction emission into one helper**

Create:

```cpp
std::optional<R5900X64CompileResult>
emit_ir_instruction(std::vector<std::uint8_t>& bytes,
                    const R5900IrInstruction& instruction,
                    std::size_t instruction_index);
```

It returns `std::nullopt` on success and a compile failure on unsupported emission. Reuse it for body and delay-slot copies. Every caller must check the returned optional and immediately propagate a populated failure; do not assume prior IR validation makes backend emission infallible.

- [ ] **Step 5: Register native BEQ differential test and verify RED**

Add:

```cmake
add_executable(r5900_x64_beq_windows_tests
  tests/r5900_x64_beq_windows_tests.cpp
)
target_link_libraries(r5900_x64_beq_windows_tests PRIVATE b3r_recompiler_x64)
add_test(NAME r5900_x64_beq_windows_tests COMMAND r5900_x64_beq_windows_tests)
```

Use a full-state `expect_states_equal` helper identical in field coverage to Task 3. Build a BEQ block with equal `r1/r2` and delay `ADDIU r1,r1,1`. Execute reference and native and compare both state and `next_pc`.

Expected RED: block compile returns unsupported terminator / test fails before BEQ emission exists.

- [ ] **Step 6: Emit BranchEqual64**

Add helpers:

```cpp
void emit_cmp_rax_rdx(std::vector<std::uint8_t>& bytes) {
    bytes.push_back(0x48u);
    bytes.push_back(0x39u);
    bytes.push_back(0xd0u); // cmp rax, rdx
}

std::size_t emit_jne_rel32_placeholder(std::vector<std::uint8_t>& bytes) {
    bytes.push_back(0x0fu);
    bytes.push_back(0x85u);
    const auto offset = bytes.size();
    emit_u32(bytes, 0u);
    return offset;
}
```

Patch the signed rel32 from the four-byte displacement field to the not-taken label.

Emit BEQ in this semantic order:

```text
load rs.low64 -> RAX
load rt.low64 -> RDX
cmp RAX,RDX
jne not_taken

taken:
  emit one delay IR instruction
  normalize GPR0
  mov EAX,taken_pc
  ret

not_taken:
  emit the same delay IR instruction
  normalize GPR0
  mov EAX,fallthrough_pc
  ret
```

Check and propagate `emit_ir_instruction(...)` failure for both duplicated delay copies before publishing executable memory.

For `Fallthrough`, emit body, normalize GPR0, return `fallthrough_pc` in EAX, `ret`.

- [ ] **Step 7: Add differential matrix**

Cover:

```text
- taken
- not taken
- r0 == r0
- low64 equal while high64 differs
- delay modifies rs
- delay modifies rt
```

For every case compare full modeled state and `next_pc` against `execute_r5900_ir_block`.

- [ ] **Step 8: Verify all x64 regressions**

```powershell
cmake --build --preset vs2022-debug --target r5900_x64_backend_windows_tests r5900_x64_startup_integer_windows_tests r5900_x64_startup_cop1_windows_tests r5900_x64_beq_windows_tests
ctest --preset vs2022-debug -R "r5900_x64_.*windows_tests" --output-on-failure
```

- [ ] **Step 9: Commit**

```bash
git add CMakeLists.txt src/recompiler/windows/r5900_x64_backend.h src/recompiler/windows/r5900_x64_backend.cpp tests/r5900_x64_backend_windows_tests.cpp tests/r5900_x64_startup_integer_windows_tests.cpp tests/r5900_x64_startup_cop1_windows_tests.cpp tests/r5900_x64_beq_windows_tests.cpp
git commit -m "feat: emit native R5900 BEQ blocks"
```

---

### Task 5: Execute and cache BEQ blocks in the dispatcher

**Files:**
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Modify: `src/recompiler/windows/r5900_block_dispatcher.h`
- Modify: `tests/r5900_block_dispatcher_windows_tests.cpp`

**Interfaces:**
- Consumes: analyzer terminator/delay-slot metadata, `R5900IrBlock`, x64 block compiler returning `next_pc`.
- Produces: ordinary BEQ execution while all other control flow remains a stop boundary.

- [ ] **Step 1: Write dispatcher RED for a BEQ-only block**

Create synthetic words:

```cpp
i_type(0x04u, 1u, 2u, 1u), // BEQ r1,r2,+1
i_type(0x09u, 3u, 3u, 1u), // delay: ADDIU r3,r3,1
0u,                         // target
```

Set `r1 == r2`, run one block, and assert:

```cpp
result.reason == R5900DispatchStopReason::BlockBudgetExhausted
result.blocks_executed == 1u
result.instructions_executed == 2u
result.next_pc == base + 12u
state.gpr[3].low64 == 1u
```

This specifically proves a supported BEQ with an empty straight-line body is executable.

- [ ] **Step 2: Verify RED**

Expected: dispatcher returns `ControlFlow` before executing BEQ/delay.

- [ ] **Step 3: Build a branch-capable candidate**

Refactor the loop so it separates:

```text
body_sites = supported instructions before terminator
terminator = ordinary BEQ if block.end_kind == ConditionalBranch and decoded instruction == Beq
```

Rules:

```text
BEQ => supported terminator
other branch/jump => ControlFlow stop
System => Trap
unsupported non-control instruction => UnsupportedInstruction
```

Do not apply the old `prefix.empty()` stop to a supported BEQ-only block.

- [ ] **Step 4: Lower candidate atomically**

Create `R5900IrBlock ir_block` only after analysis succeeds. Lower every body site. For BEQ:

```cpp
ir_block.terminator.guest_pc = terminator.pc;
ir_block.terminator.guest_raw = terminator.decoded.raw;
ir_block.terminator.kind = R5900IrTerminatorKind::BranchEqual64;
ir_block.terminator.inputs = {dispatcher_gpr(terminator.decoded.rs),
                              dispatcher_gpr(terminator.decoded.rt)};
ir_block.terminator.taken_pc = *terminator.decoded.direct_target(terminator.pc);
ir_block.terminator.fallthrough_pc = terminator.pc + 8u;
```

Add private `dispatcher_gpr(index)` in the `.cpp` only.

Lower `block.delay_slot` and require exactly one resulting IR instruction. If delay lowering fails, return `LoweringFailure`, `next_pc = delay_slot.pc`, zero completed-block/instruction accounting for this candidate, and do not publish cache replacement.

- [ ] **Step 5: Fingerprint the full branch block**

For BEQ, collect guest words in order:

```text
body words
BEQ word
delay-slot word
```

Set `guest_instruction_count` to that count and `end_pc_exclusive = terminator.pc + 8`.

Straight-line candidate behavior remains compatible with current cache format.

- [ ] **Step 6: Consume native `next_pc`**

On cache hit/miss/recompile, execute and capture:

```cpp
const auto next_pc = cached_block.native_block.execute(state);
++result.blocks_executed;
result.instructions_executed += cached_block.guest_instruction_count;
current_pc = next_pc;
result.next_pc = next_pc;
```

Do not evaluate branch condition in C++.

- [ ] **Step 7: Add stale-cache and dynamic-outcome tests**

Independently prove recompilation when changing:

```text
- one body word
- the BEQ word while it remains supported
- the delay-slot word while it remains supported
```

Then run the exact same cached BEQ code twice with only GPR values changed: first equal, then unequal. Assert second execution is a cache hit, no recompilation occurs, and returned `next_pc` differs.

- [ ] **Step 8: Add failure atomicity test**

Use a supported body followed by BEQ whose delay slot is generated `XORI` (unsupported by current lowering). Initialize a nontrivial sentinel state and cache size.

Add this full-state helper to `r5900_block_dispatcher_windows_tests.cpp` if it does not already exist:

```cpp
void expect_states_equal(const R5900IrExecutionState& expected,
                         const R5900IrExecutionState& actual) {
    for (std::size_t i = 0; i < expected.gpr.size(); ++i) {
        expect(expected.gpr[i].low64 == actual.gpr[i].low64, "GPR low64 mismatch");
        expect(expected.gpr[i].high64 == actual.gpr[i].high64, "GPR high64 mismatch");
    }
    expect(expected.hi == actual.hi, "HI mismatch");
    expect(expected.lo == actual.lo, "LO mismatch");
    expect(expected.hi1 == actual.hi1, "HI1 mismatch");
    expect(expected.lo1 == actual.lo1, "LO1 mismatch");
    expect(expected.sa == actual.sa, "SA mismatch");
    for (std::size_t i = 0; i < expected.fpr.size(); ++i) {
        expect(expected.fpr[i] == actual.fpr[i], "FPR mismatch");
    }
    expect(expected.fcr31 == actual.fcr31, "FCR31 mismatch");
    expect(expected.fp_acc == actual.fp_acc, "FP accumulator mismatch");
}
```

Assert:

```cpp
result.reason == R5900DispatchStopReason::LoweringFailure
result.blocks_executed == 0u
result.instructions_executed == 0u
expect_states_equal(initial_state, state)
dispatcher.cache_size() == initial_cache_size
```

- [ ] **Step 9: Verify GREEN**

```powershell
cmake --build --preset vs2022-debug --target r5900_block_dispatcher_windows_tests
ctest --preset vs2022-debug -R r5900_block_dispatcher_windows_tests --output-on-failure
```

- [ ] **Step 10: Commit**

```bash
git add src/recompiler/windows/r5900_block_dispatcher.h src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "feat: dispatch native R5900 BEQ blocks"
```

---

### Task 6: Prove the 81-instruction startup continuation and document it

**Files:**
- Modify: `tests/r5900_block_dispatcher_startup_windows_tests.cpp`
- Modify: `README.md`
- Modify: `docs/PROGRESS.md`

**Interfaces:**
- Consumes: BEQ-capable dispatcher and existing optional external-ELF harness.
- Produces: synthetic E2E proof to `SQ 0x00100160`; external harness updated to the same boundary.

- [ ] **Step 1: Extend synthetic startup fixture**

Keep the existing generated 74-instruction prefix and assert it ends at `0x00100130`.

Append:

```cpp
words.push_back(i_type(0x04u, 0u, 0u, 6u));      // 0x00100130: always taken -> 0x0010014C
words.push_back(i_type(0x09u, 0u, 20u, 0x11u)); // 0x00100134: first visible delay effect
```

Fill skipped addresses `0x00100138..0x00100148` with five generated XORI instructions so accidental fallthrough fails loudly:

```cpp
for (std::uint8_t i = 0u; i < 5u; ++i) {
    words.push_back(i_type(0x0eu, 0u,
                           static_cast<std::uint8_t>(10u + i),
                           1u));
}
```

At `0x0010014C`, append:

```cpp
words.push_back(i_type(0x0fu, 0u, 4u, 0xffffu)); // LUI r4,0xffff
words.push_back(i_type(0x0du, 4u, 4u, 0xfff0u)); // ORI r4,r4,0xfff0
words.push_back(r_type(3u, 4u, 4u, 0u, 0x24u));  // AND r4,r3,r4
words.push_back(i_type(0x04u, 2u, 4u, 7u));      // BEQ r2,r4,+7; expected not taken
words.push_back(i_type(0x09u, 0u, 21u, 0x22u));  // second visible delay effect
words.push_back(i_type(0x1fu, 2u, 0u, 0u));      // SQ blocker at 0x00100160
```

Run dispatcher with `max_blocks = 3` and assert:

```cpp
expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction,
       "synthetic startup must stop at SQ");
expect(result.next_pc == 0x00100160u, "SQ boundary mismatch");
expect(result.blocks_executed == 2u, "must complete two BEQ blocks");
expect(result.instructions_executed == 81u, "must execute exactly 81 instructions");
expect(state.gpr[20].low64 == 0x11u, "first delay slot must execute");
expect(state.gpr[21].low64 == 0x22u, "second delay slot must execute");
expect(state.gpr[4].low64 == 0x0000000001ecea00ull,
       "second block AND result mismatch");
```

This is an integration proof for production behavior already driven RED in Tasks 1-5; do not deliberately break working production code just to manufacture another RED here.

- [ ] **Step 2: Update the external Windows harness**

Keep ELF loading/mapping unchanged. Run:

```cpp
const auto result = dispatcher.run(parsed.image->entry_point(), state, 3u);
```

Expect:

```cpp
result.reason == R5900DispatchStopReason::UnsupportedInstruction
result.next_pc == 0x00100160u
result.blocks_executed == 2u
result.instructions_executed == 81u
state.gpr[2].low64 == 0x00000000004e2680ull
state.gpr[3].low64 == 0x0000000001ecea00ull
state.gpr[4].low64 == 0x0000000001ecea00ull
```

Preserve existing HI/LO/HI1/LO1/SA/FPR/FCR31/FP_ACC checks. Print:

```text
REAL_ELF_STARTUP_VALIDATED start=0x00100008 stop=0x00100160 instructions=81 blocks=2
```

Do not add the external ELF to CTest or CI.

- [ ] **Step 3: Run full local Windows test suite**

```powershell
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 4: Run external harness only when the legal ELF is available on Windows x64**

```powershell
.\build\vs2022-debug\Debug\r5900_block_dispatcher_startup_windows_tests.exe "D:\Games\Burnout3\SLUS_210.50"
```

Expected external line:

```text
REAL_ELF_STARTUP_VALIDATED start=0x00100008 stop=0x00100160 instructions=81 blocks=2
```

If this run cannot be performed, status remains `CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION`; never claim `EXTERNALLY_VALIDATED` from static inspection alone.

- [ ] **Step 5: Update README**

State precisely:

```text
ordinary native BEQ and architectural delay-slot execution now exist for the first startup continuation; synthetic native execution completes two branch blocks / 81 guest instructions and reaches unsupported SQ at 0x00100160.
```

Retain explicit limitations: guest memory, `SQ`, broader control flow, syscalls, game initialization, graphics, audio, input, menu/gameplay are not implemented; do not say the game boots.

- [ ] **Step 6: Update `docs/PROGRESS.md`**

Add/update rows equivalent to:

```text
R5900 BEQ + delay slot v0 | CI_VALIDATED | BranchEqual64 returns native next_pc, snapshots predicate before one delay slot, and passes reference/x64 differential tests for taken/not-taken/high64/r0/source-mutating cases.

R5900 startup execution | CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION | Synthetic path completes 2 blocks / 81 instructions and stops at SQ 0x00100160. External harness accepts a legal local ELF; EXTERNALLY_VALIDATED only after it actually passes on that file.
```

Record the final GitHub Actions run ID only after that exact head SHA succeeds.

- [ ] **Step 7: Commit docs/E2E**

```bash
git add tests/r5900_block_dispatcher_startup_windows_tests.cpp README.md docs/PROGRESS.md
git commit -m "docs: record R5900 BEQ startup continuation"
```

- [ ] **Step 8: Final verification before integration**

Verify the final branch head through Windows GitHub Actions: configure, build, all tests, frame-pacing telemetry, pacing-probe smoke, analyzer package validation, and pacing-probe package validation must all succeed.

Then compare:

```bash
git diff --stat main...feature/r5900-beq-delay-slot-v0
```

Confirm no proprietary ELF/game data, binary dump, or unrelated subsystem change is present. Do not merge `main` until the user chooses an integration option after verification.
