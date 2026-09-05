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
- `tests/r5900_direct_transfer_test_support.h` — extend shared transfer test helpers with `indirect_jump()` and `indirect_call()`.
- `tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp` — replace obsolete JR sentinel boundaries with unsupported BNE boundaries and remove the old assertion that JR/JALR are unsupported.
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
- Consumes: existing `R5900IrOperand`, `R5900IrTerminator`, `validate_r5900_ir_block()`, and test helpers `gpr()` / `nop()`.
- Produces:
  - `R5900IrTerminatorKind::IndirectJump`
  - `R5900IrTerminatorKind::IndirectCall`
  - `R5900IrTerminator::link_gpr` as `std::uint8_t`
  - `b3r::test_support::indirect_jump(std::uint32_t, std::uint8_t, R5900IrInstruction)`
  - `b3r::test_support::indirect_call(std::uint32_t, std::uint8_t, std::uint8_t, R5900IrInstruction)`

- [ ] **Step 1: Register the dedicated validation target and write RED**

Add to the portable test section of `CMakeLists.txt` after the direct-transfer validation target:

```cmake
add_executable(r5900_ir_indirect_transfer_validation_tests
  tests/r5900_ir_indirect_transfer_validation_tests.cpp
)
target_link_libraries(r5900_ir_indirect_transfer_validation_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_indirect_transfer_validation_tests
  COMMAND r5900_ir_indirect_transfer_validation_tests)
```

Create `tests/r5900_ir_indirect_transfer_validation_tests.cpp`. Its `main()` must cover this exact matrix:

```cpp
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
    expect_malformed(invalid, "IndirectJump rejects target_pc");
}
{
    auto invalid = jump;
    invalid.terminator.taken_pc = 0x00109000u;
    expect_malformed(invalid, "IndirectJump rejects taken_pc");
}
{
    auto invalid = jump;
    invalid.terminator.fallthrough_pc = 0x00108008u;
    expect_malformed(invalid, "IndirectJump rejects fallthrough_pc");
}
{
    auto invalid = jump;
    invalid.terminator.link_pc = 0x00108008u;
    expect_malformed(invalid, "IndirectJump rejects link_pc");
}
{
    auto invalid = jump;
    invalid.terminator.link_gpr = 31u;
    expect_malformed(invalid, "IndirectJump rejects link_gpr");
}
{
    auto invalid = call;
    invalid.terminator.link_pc += 4u;
    expect_malformed(invalid, "IndirectCall link must equal PC+8");
}
{
    auto invalid = call;
    invalid.terminator.link_pc |= 2u;
    expect_malformed(invalid, "IndirectCall link must be aligned");
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
```

Also construct valid `Fallthrough`, `BranchEqual64`, `DirectJump`, and `DirectCall` blocks, set `terminator.link_gpr = 1`, and require `MalformedInstruction` for each so the new field cannot leak into existing terminator kinds.

Use the same local `fail()`, `expect()`, and `expect_malformed()` style as `tests/r5900_ir_direct_transfer_validation_tests.cpp`.

- [ ] **Step 2: Run RED before adding production symbols**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target r5900_ir_indirect_transfer_validation_tests
```

Expected RED: missing `IndirectJump`, `IndirectCall`, `link_gpr`, and/or the indirect helper functions. Reject unrelated failures.

- [ ] **Step 3: Add minimal IR and test helpers**

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

- [ ] **Step 4: Implement exact validator contracts**

Tighten existing `Fallthrough`, `BranchEqual64`, `DirectJump`, and `DirectCall` malformed conditions with:

```cpp
terminator.link_gpr != 0u
```

Add:

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
    const auto input =
        validate_operand(terminator.inputs[0], terminator_index, terminator.guest_pc);
    if (!input.ok()) return input;
    return validate_single_delay_slot(terminator, terminator_index);
}

case R5900IrTerminatorKind::IndirectCall: {
    if (terminator.link_gpr >= 32u) {
        return failure(R5900IrValidationError::InvalidRegister,
                       terminator_index,
                       terminator.guest_pc,
                       "invalid indirect-call link GPR");
    }
    if (terminator.inputs.size() != 1u ||
        terminator.inputs[0].kind != R5900IrOperandKind::Gpr ||
        terminator.taken_pc != 0u ||
        terminator.fallthrough_pc != 0u ||
        terminator.target_pc != 0u ||
        (terminator.link_pc & 0x3u) != 0u ||
        terminator.link_pc != static_cast<std::uint32_t>(terminator.guest_pc + 8u)) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       terminator_index,
                       terminator.guest_pc,
                       "malformed indirect-call terminator");
    }
    const auto input =
        validate_operand(terminator.inputs[0], terminator_index, terminator.guest_pc);
    if (!input.ok()) return input;
    return validate_single_delay_slot(terminator, terminator_index);
}
```

- [ ] **Step 5: Run focused GREEN and validation regressions**

```powershell
cmake --build build --config Release --target `
  r5900_ir_indirect_transfer_validation_tests `
  r5900_ir_direct_transfer_validation_tests `
  r5900_ir_block_validation_tests
ctest --test-dir build -C Release --output-on-failure -R "r5900_ir_(indirect_transfer_validation|direct_transfer_validation|block_validation)_tests"
```

Expected: all selected tests PASS.

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
- Consumes: Task 1 typed terminators/helpers.
- Produces: reference behavior used as the Task 3 differential oracle.

- [ ] **Step 1: Register executor test and write RED**

Add:

```cmake
add_executable(r5900_ir_indirect_transfer_executor_tests
  tests/r5900_ir_indirect_transfer_executor_tests.cpp
)
target_link_libraries(r5900_ir_indirect_transfer_executor_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_indirect_transfer_executor_tests
  COMMAND r5900_ir_indirect_transfer_executor_tests)
```

The test file must define a local `MemoryProbe`, `write128()` callback, `fail()`, and `expect()`. Cover these exact cases:

```cpp
// JR snapshots low32 before delay mutates source and does not touch r31.
state.gpr[5] = {0x1234000012345678ull, 0xfeedfacefeedfaceull};
state.gpr[31] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
auto block = indirect_jump(0x00109000u, 5u,
                           addiu(5u, 0u, 9, 0x00109004u));
auto result = execute_r5900_ir_block(block, state);
expect(result.ok() && result.next_pc == 0x12345678u,
       "JR must return snapshotted low32 target");
expect(state.gpr[5].low64 == 9u,
       "JR delay may mutate source after snapshot");

// JALR rd==rs snapshots old target, writes PC+8, preserves high64,
// and exposes the new link to the delay slot.
state.gpr[5] = {0x000000000010a000ull, 0x0123456789abcdefull};
block = indirect_call(0x00109200u, 5u, 5u,
                      addiu(6u, 5u, 0, 0x00109204u));
result = execute_r5900_ir_block(block, state);
expect(result.ok() && result.next_pc == 0x0010a000u,
       "JALR rd==rs must jump using old source");
expect(state.gpr[5].low64 == 0x00109208u &&
       state.gpr[5].high64 == 0x0123456789abcdefull,
       "JALR must write PC+8 low64 and preserve high64");
expect(state.gpr[6].low64 == 0x00109208u,
       "JALR delay must see new link");

// JALR rd==0 still jumps and leaves normalized GPR0.
state = {};
state.gpr[7].low64 = 0x0010b000u;
state.gpr[0] = {0x1111u, 0x2222u};
block = indirect_call(0x00109400u, 7u, 0u, nop(0x00109404u));
result = execute_r5900_ir_block(block, state);
expect(result.ok() && result.next_pc == 0x0010b000u,
       "JALR rd==0 must still jump");
expect(state.gpr[0].low64 == 0u && state.gpr[0].high64 == 0u,
       "JALR rd==0 must leave GPR0 normalized");

// Delay write to link destination wins after link.
state = {};
state.gpr[8].low64 = 0x0010c000u;
state.gpr[9] = {0x7777u, 0x9999aaaabbbbccccull};
block = indirect_call(0x00109600u, 8u, 9u,
                      addiu(9u, 0u, 3, 0x00109604u));
result = execute_r5900_ir_block(block, state);
expect(result.ok() && state.gpr[9].low64 == 3u &&
       state.gpr[9].high64 == 0x9999aaaabbbbccccull,
       "delay write must win after JALR link");
```

Add a lower-level delay-memory-failure case using:

```cpp
MemoryProbe probe{};
probe.succeed = false;
R5900IrExecutionContext context{};
context.state = &state;
context.memory.user = &probe;
context.memory.write128 = &write128;
block = indirect_call(0x00109800u, 5u, 9u,
                      store128(2u, 3u, 0, 0x00109804u));
result = execute_r5900_ir_block(block, context);
expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
       "JALR delay memory failure must propagate");
expect(state.gpr[9].low64 == 0x00109808u,
       "JALR link remains committed before delay failure");
expect(context.memory_fault.active &&
       context.memory_fault.guest_pc == 0x00109804u,
       "delay fault PC mismatch");
```

Add a body-failure case with `Store128` in `block.body`, no memory callback, and assert link GPR plus delay destination remain unchanged.

- [ ] **Step 2: Run RED**

```powershell
cmake --build build --config Release --target r5900_ir_indirect_transfer_executor_tests
ctest --test-dir build -C Release --output-on-failure -R r5900_ir_indirect_transfer_executor_tests
```

Expected RED: test executes but returns `UnsupportedOpcode` for indirect terminators.

- [ ] **Step 3: Implement reference cases**

Add to the block terminator switch:

```cpp
case R5900IrTerminatorKind::IndirectJump: {
    const auto target = static_cast<std::uint32_t>(
        state.gpr[block.terminator.inputs[0].gpr_index].low64);
    const auto delay_result =
        execute_ir_sequence(block.terminator.delay_slot, context);
    if (!delay_result.ok()) return map_block_execution_failure(delay_result);
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
    if (!delay_result.ok()) return map_block_execution_failure(delay_result);
    return {R5900IrExecutionError::None, {}, target};
}
```

Never read `inputs[0]` again after the snapshot.

- [ ] **Step 4: Run GREEN and direct-transfer regression**

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
- Consumes: Task 2 reference semantics and existing generated-function ABI `std::uint32_t (*)(R5900IrExecutionState*, R5900IrExecutionContext*)`.
- Produces: native support with target snapshot in `[rsp+0x30]`.

- [ ] **Step 1: Register Windows-only target and write RED differential tests**

Inside `if(WIN32)` tests after the direct-transfer x64 target:

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

Reuse the proven differential structure from `tests/r5900_x64_direct_transfer_windows_tests.cpp`: copy its `MemoryProbe`, `write128()`, `context_for()`, `expect_states_equal()`, `expect_faults_equal()`, and `sentinel_state()` helpers verbatim. Do **not** use `memcmp` over `R5900IrExecutionState`; compare fields exactly as `expect_states_equal()` already does.

Add a local `run_differential()` that compiles the block, runs the reference and native contexts, and compares error, next PC, CPU state, memory probe fields, and memory-fault fields.

Cover:

1. JR low32 target with non-zero source high64.
2. JR delay mutates target source after snapshot.
3. JALR ordinary `rd != rs`.
4. JALR alias `rd == rs`.
5. JALR `rd == 0`.
6. non-zero link-destination high64 preservation.
7. delay reads new link.
8. delay overwrites link destination.
9. representative eligible body before terminator.
10. `Store128` delay success.
11. `Store128` delay failure with link committed and matching fault.

For the alias case seed:

```cpp
auto state = sentinel_state();
state.gpr[5] = {0x000000000010a000ull, 0x0123456789abcdefull};
const auto block = indirect_call(0x0010a200u, 5u, 5u,
                                 addiu(6u, 5u, 0, 0x0010a204u));
run_differential(block, state, true,
                 "native/reference JALR rd==rs mismatch");
```

For failing delay, require both paths return `MemoryAccessFailure`, both have link `PC+8`, both preserve high64, and both faults report the delay guest PC.

- [ ] **Step 2: Run RED**

```powershell
cmake --build build --config Release --target r5900_x64_indirect_transfer_windows_tests
ctest --test-dir build -C Release --output-on-failure -R r5900_x64_indirect_transfer_windows_tests
```

Expected RED: first indirect block fails native compilation with unsupported terminator; test compilation itself succeeds.

- [ ] **Step 3: Add exact stack-local emit helpers**

Near scalar emit helpers:

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

Keep existing `sub rsp,0x38`: `[rsp+0x00..0x1f]` is shadow, `[rsp+0x20]` state, `[rsp+0x28]` context, `[rsp+0x30..0x37]` local snapshot.

- [ ] **Step 4: Add dedicated indirect emitter**

```cpp
PendingX64Code compile_indirect_transfer_code(const R5900IrBlock& block) {
    constexpr bool helper_frame = true;

    std::vector<std::uint8_t> bytes;
    bytes.reserve(160u +
        block.body.size() * 128u +
        block.terminator.delay_slot.size() * 256u);

    emit_helper_frame_prologue(bytes);
    emit_zero_gpr0(bytes);

    const auto body_emitted = emit_ir_sequence(bytes, block.body, 0u, helper_frame);
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

The existing `Store128` failure path already emits helper-frame epilogue + `ret`; do not add a second epilogue there.

- [ ] **Step 5: Route new terminators**

```cpp
case R5900IrTerminatorKind::IndirectJump:
case R5900IrTerminatorKind::IndirectCall:
    pending = compile_indirect_transfer_code(block);
    break;
```

Keep direct and indirect emitters separate.

- [ ] **Step 6: Run GREEN plus native regressions**

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
- Modify: `tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp`
- Create: `tests/r5900_block_dispatcher_indirect_transfer_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: analyzer `IndirectJump/IndirectCall`, decoded `Jr/Jalr`, Task 1 IR, Task 3 native compiler.
- Produces: dispatcher execution/cache support for final decoded JR/JALR only.

- [ ] **Step 1: Register dedicated dispatcher target and build exact test scaffolding**

Add:

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

Create the new test by copying these namespace-local helpers verbatim from `tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp`: `Bytes`, `fail`, `expect`, `put_u16`, `put_u32`, `r_type`, `i_type`, and `make_memory`. Omit `j_type` because indirect fixtures do not need it. Keep the same 2-segment synthetic ELF structure and executable code segment.

Define:

```cpp
constexpr std::uint32_t jr(std::uint8_t rs) {
    return r_type(rs, 0u, 0u, 0u, 0x08u);
}

constexpr std::uint32_t jalr(std::uint8_t rd, std::uint8_t rs) {
    return r_type(rs, 0u, rd, 0u, 0x09u);
}

constexpr std::uint32_t bne_zero_zero() {
    return i_type(0x05u, 0u, 0u, 0u);
}
```

- [ ] **Step 2: Write RED dispatcher cases**

Use `base = 0x00112000u`. Required cases:

**JR at entry:** words `[JR r5, NOP, guard]`; seed `r5 = base + 0x20`; map target at `base+0x20` as `BNE r0,r0,0` plus NOP. Run with one-block budget and assert block executes two guest instructions and returns exact target.

**JR after body:** words `[ADDIU r8,r0,7, JR r5, ADDIU r9,r0,3]`; one-block budget; require r8=7, r9=3, instructions=3, next_pc dynamic target.

**JALR normal:** `[JALR r9,r5, ADDIU r10,r9,0]`; seed `r9.high64` non-zero; require target from r5, r9.low64=`base+8`, high64 preserved, r10 observes link.

**JALR alias:** `[JALR r5,r5, ADDIU r6,r5,0]`; seed old r5=target; require next_pc=old target, final r5=`base+8`, r6=`base+8`.

**Cache dynamic target:** use `[JR r5, NOP, ADDIU guard]`; first run one block with `r5=target_a` must miss cache; second run same code with `r5=target_b` must hit cache, have zero recompilations, and return target_b.

**Cache span proof:** after compiling `[JR r5,NOP,guard]`, mutate only the word at `base+8` (`terminator+8`) and rerun one block. Require `cache_hits==1`, `recompilations==0`. This proves code after the delay slot is outside the cached guest-word span without exposing private `CachedBlock` fields.

**Terminator mutation:** mutate `JR r5` to `JR r6`, seed r6 with a valid target, rerun and require `recompilations==1`.

**Delay mutation:** restore JR r5, mutate NOP delay to `ADDIU r7,r0,4`, rerun and require `recompilations==1` and r7=4.

**SQ delay rejection:** separately test `[JR r5, SQ ...]` and `[JALR r9,r5, SQ ...]`; require `LoweringFailure`, `next_pc=delay_pc`, zero blocks/instructions, cache size zero, guest memory unchanged.

**Invalid target:** run `[JR r5,NOP]` with enough block budget for a following analysis iteration. For `r5=base+2` require `AnalysisFailure`, one completed block, two completed instructions, `next_pc=base+2`. Repeat with an aligned unmapped address such as `0x00300000`.

- [ ] **Step 3: Run RED**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_indirect_transfer_windows_tests
ctest --test-dir build -C Release --output-on-failure -R r5900_block_dispatcher_indirect_transfer_windows_tests
```

Expected RED: JR/JALR remain ControlFlow boundaries with zero indirect-block progress.

- [ ] **Step 4: Extend supported-transfer recognition**

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

Keep the existing final-transfer exclusion from `body_sites` and existing `guest_words` collection.

- [ ] **Step 5: Split direct and indirect terminator lowering**

Do not invoke `direct_target()` for JR/JALR. Use:

```cpp
ir_block.terminator.guest_pc = transfer_site->pc;
ir_block.terminator.guest_raw = transfer_site->decoded.raw;

if (has_supported_beq || has_supported_j || has_supported_jal) {
    const auto target = transfer_site->decoded.direct_target(transfer_site->pc);
    if (!target.has_value()) {
        result.reason = R5900DispatchStopReason::AnalysisFailure;
        result.next_pc = transfer_site->pc;
        result.message = format_stage_error(
            "analysis", transfer_site->pc,
            "decoded supported direct control transfer unexpectedly lacks target");
        return result;
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

- [ ] **Step 6: Generalize SQ-delay rejection and preserve cache identity**

Keep the SQ check before delay lowering, but use one exact message:

```cpp
"SQ in a supported control-transfer delay slot is outside dispatcher v0 scope"
```

Keep cache identity exactly based on `guest_words`, and keep:

```cpp
replacement.end_pc_exclusive = has_supported_transfer
    ? transfer_site->pc + 8u
    : current_pc + static_cast<std::uint32_t>(body_sites.size() * 4u);
```

Never add runtime target GPR contents to fingerprint or `CachedBlock` equality.

- [ ] **Step 7: Update obsolete direct-transfer fixtures**

`tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp` currently uses `JR r31` as an unsupported stop sentinel and ends with a loop that explicitly requires entry JR/JALR to remain unsupported. That contract is obsolete after this task.

Add:

```cpp
const auto bne00 = i_type(0x05u, 0u, 0u, 0u);
```

In every J/JAL fixture that currently places `jr31` at the mapped target, replace the target pair with:

```cpp
bne00, 0u
```

This preserves a mapped unsupported ControlFlow boundary with a mapped delay slot, so direct J/JAL assertions remain focused on direct transfers.

In the J/JAL SQ-delay fixtures, replace the target `jr31,0u` pair with `bne00,0u` as well.

Delete the final loop whose assertion text is:

```text
entry JR/JALR must remain unsupported control-flow boundary
```

The new dedicated indirect-transfer test now owns those semantics.

- [ ] **Step 8: Run dispatcher GREEN and regressions**

```powershell
cmake --build build --config Release --target `
  r5900_block_dispatcher_indirect_transfer_windows_tests `
  r5900_block_dispatcher_direct_transfer_windows_tests `
  r5900_block_dispatcher_store128_windows_tests `
  r5900_block_dispatcher_windows_tests
ctest --test-dir build -C Release --output-on-failure -R "r5900_block_dispatcher_(indirect_transfer|direct_transfer|store128|windows)_tests"
```

Expected: all selected tests PASS. Specifically verify direct-transfer tests still stop at BNE and no test still encodes “JR/JALR unsupported”.

- [ ] **Step 9: Commit Task 4**

```bash
git add CMakeLists.txt src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp tests/r5900_block_dispatcher_indirect_transfer_windows_tests.cpp
git commit -m "feat: dispatch R5900 indirect transfers"
```

---

### Task 5: Expand synthetic startup through JR and aliasing JALR

**Files:**
- Modify: `tests/r5900_block_dispatcher_startup_windows_tests.cpp`

**Interfaces:**
- Consumes: Task 4 dispatcher support.
- Produces: exact E2E `ControlFlow`, `next_pc=0x001001c4`, `blocks=7`, `instructions=94`, preserving SQ state.

- [ ] **Step 1: Change terminal assertions first to create RED**

Before changing fixture words, require:

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

- [ ] **Step 2: Run startup RED**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_startup_windows_tests
ctest --test-dir build -C Release --output-on-failure -R r5900_block_dispatcher_startup_windows_tests
```

Expected RED: old layout/accounting does not satisfy 7/94/BNE endpoint.

- [ ] **Step 3: Install approved exact post-JAL layout**

Keep all startup words through `0x00100184`. From `0x00100188` onward use:

```cpp
words.push_back(i_type(0x0fu, 0u, 5u, 0x0010u));       // 0x188 LUI r5,0x0010
words.push_back(i_type(0x0du, 5u, 5u, 0x01c0u));       // 0x18c ORI r5,r5,0x01c0
words.push_back(r_type(5u, 0u, 5u, 0u, 0x09u));        // 0x190 JALR r5,r5
words.push_back(i_type(0x09u, 5u, 6u, 0u));            // 0x194 ADDIU r6,r5,0
words.push_back(i_type(0x09u, 0u, 25u, 1u));           // 0x198 poison
words.push_back(i_type(0x09u, 0u, 26u, 1u));           // 0x19c poison
words.push_back(i_type(0x09u, 0u, 24u, 0x0055u));      // 0x1a0 direct callee
words.push_back(r_type(31u, 0u, 0u, 0u, 0x08u));       // 0x1a4 JR r31
words.push_back(i_type(0x09u, 0u, 29u, 0x0077u));      // 0x1a8 JR delay
words.push_back(i_type(0x09u, 0u, 25u, 2u));           // 0x1ac poison
words.push_back(i_type(0x09u, 0u, 26u, 2u));           // 0x1b0 poison
words.push_back(i_type(0x09u, 0u, 27u, 2u));           // 0x1b4 poison
words.push_back(i_type(0x09u, 0u, 28u, 2u));           // 0x1b8 poison
words.push_back(i_type(0x09u, 0u, 30u, 2u));           // 0x1bc poison/guard
words.push_back(i_type(0x09u, 0u, 7u, 0x0066u));       // 0x1c0 indirect target
words.push_back(i_type(0x05u, 0u, 0u, 0u));            // 0x1c4 unsupported BNE
words.push_back(0u);                                    // 0x1c8 mapped BNE delay
```

Update reserve/count/layout assertions to match the new final word count. Keep poison registers distinct from architectural proof registers.

- [ ] **Step 4: Add exact state and poison assertions**

```cpp
expect(state.gpr[22].low64 == 0x33u, "J delay result mismatch");
expect(state.gpr[23].low64 == 0x00100188u,
       "JAL delay must observe direct-call link");
expect(state.gpr[24].low64 == 0x55u, "direct callee entry mismatch");
expect(state.gpr[29].low64 == 0x77u, "JR delay must execute");
expect(state.gpr[31].low64 == 0x00100188u,
       "JR must preserve direct return address");
expect(state.gpr[5].low64 == 0x00100198u,
       "JALR rd==rs link mismatch");
expect(state.gpr[6].low64 == 0x00100198u,
       "JALR delay must observe new link");
expect(state.gpr[7].low64 == 0x66u,
       "indirect target entry mismatch");
expect(state.gpr[25].low64 == 0u && state.gpr[26].low64 == 0u &&
       state.gpr[27].low64 == 0u && state.gpr[28].low64 == 0u &&
       state.gpr[30].low64 == 0u,
       "unreachable poison regions must stay untouched");
```

Retain existing SQ target 16-zero-byte and surrounding-byte integrity assertions. Do not tighten the external legal-ELF harness to the synthetic BNE endpoint.

- [ ] **Step 5: Run E2E GREEN and focused control-flow tests**

```powershell
cmake --build build --config Release --target `
  r5900_block_dispatcher_startup_windows_tests `
  r5900_block_dispatcher_indirect_transfer_windows_tests `
  r5900_block_dispatcher_direct_transfer_windows_tests
ctest --test-dir build -C Release --output-on-failure -R "r5900_block_dispatcher_(startup|indirect_transfer|direct_transfer)_windows_tests"
```

Expected: startup reaches BNE `0x001001c4` at exactly 7 blocks / 94 instructions; all selected tests PASS.

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
- Review full diff against base `27d553fcb0959407af3e6b13503c652458f7d8f1`

**Interfaces:**
- Consumes: Tasks 1–5 complete.
- Produces: `CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION` with exact-SHA CI evidence.

- [ ] **Step 1: Update README**

State that native startup infrastructure now includes:

```text
BEQ + delay slot
SQ guest-memory write
J/JAL + delay slot
JR/JALR + delay slot
```

State synthetic startup now reaches unsupported BNE after 7 native blocks / 94 guest instructions. Explicitly state game does not boot and legal external ELF has not been validated in this environment.

- [ ] **Step 2: Update PROGRESS**

Record:

- `IndirectJump` / `IndirectCall` typed IR.
- JR low32 target snapshot before delay.
- JALR target snapshot before link, including `rd==rs`.
- arbitrary link GPR including `rd==0`.
- PC+8 low64-only link/high64 preservation.
- native snapshot `[rsp+0x30]` under existing 0x38 frame.
- cache independence from runtime target value.
- code-after-delay excluded from cached span; terminator/delay mutations recompile.
- invalid target failure on following analysis with previous block committed.
- SQ-delay dispatcher restriction retained.
- exact synthetic endpoint BNE `0x001001c4`, 7 blocks, 94 instructions.
- next synthetic boundary BNE; external legal-ELF path still requires inspection.

- [ ] **Step 3: Run full Windows validation**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\frame_pacer_windows_tests.exe
.\build\Release\Burnout3PacingProbe.exe --seconds 1
```

Expected count: baseline 43 + four new focused tests = **47/47**, 0 failures. Pacing probe remains `TARGET_HZ 120`, 120 frames for one second, mean near 8.333 ms, high-resolution timer YES, and no >9/10/12 ms regressions under existing acceptance behavior.

- [ ] **Step 4: Commit docs**

```bash
git add README.md docs/PROGRESS.md
git commit -m "docs: record R5900 indirect transfer milestone"
```

- [ ] **Step 5: Require fresh GitHub Actions CI at exact final feature SHA**

Require success for configure, build, CTest, frame pacing telemetry, pacing probe, analyzer package staging/validation, and pacing package staging/validation.

From the same run logs verify:

```text
100% tests passed, 0 tests failed out of 47
r5900_ir_indirect_transfer_validation_tests ... Passed
r5900_ir_indirect_transfer_executor_tests ... Passed
r5900_x64_indirect_transfer_windows_tests ... Passed
r5900_block_dispatcher_indirect_transfer_windows_tests ... Passed
r5900_block_dispatcher_startup_windows_tests ... Passed
```

Record exact pacing telemetry from that run rather than reusing an earlier milestone's numbers.

- [ ] **Step 6: Review full diff against base**

Review:

```text
base = 27d553fcb0959407af3e6b13503c652458f7d8f1
head = final feature SHA
```

Check every item:

- no duplicated direct-target formula for JR/JALR;
- target snapshot precedes JALR link;
- native `[rsp+0x30]` local is outside Win64 shadow space;
- all native return/failure paths restore RSP exactly once;
- rd==0 does not persistently write GPR0;
- rd==rs uses old source target;
- link never writes high64;
- runtime target values never affect cache fingerprint;
- mutation at terminator+8 remains cache hit;
- terminator/delay mutation recompiles;
- `end_pc_exclusive` assignment remains terminator+8;
- invalid target is not repaired/prevalidated by terminator execution;
- SQ delay restriction remains dispatcher-only;
- direct-transfer tests no longer use JR/JALR as unsupported sentinels;
- startup poison ranges do not overlap direct callee/JR region;
- docs make no boot/external-validation claim;
- no temporary workflow/script/patch files remain.

If any issue is fixed after this review, rerun fresh exact-SHA Windows CI before completion.

- [ ] **Step 7: Completion checkpoint**

Only declare complete with:

```text
status = CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION
synthetic startup = 7 blocks / 94 instructions
synthetic stop = BNE at 0x001001c4
full CTest = 47/47 PASS
pacing/package checks = PASS
legal external ELF = NOT YET VALIDATED unless actually run
```

Do not fast-forward `feature/r5900-beq-delay-slot-v0` until the user explicitly chooses integration after verified feature completion.