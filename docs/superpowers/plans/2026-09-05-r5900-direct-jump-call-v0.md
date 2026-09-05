# R5900 Direct J/JAL + Delay Slot v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute R5900 direct `J` and `JAL` natively with one architectural delay slot, correct `JAL` link semantics, dispatcher/cache integration, and a synthetic startup proof that advances from the validated `SQ` through `J` and `JAL` before stopping at unsupported `JR`.

**Architecture:** Extend `R5900IrTerminator` with explicit `DirectJump` and `DirectCall` kinds and direct-transfer fields. Validation, reference execution, and the Windows x86-64 backend use the same ordering: body first, `JAL` link write before the delay slot, delay slot exactly once, then return `target_pc`. The dispatcher recognizes analyzer-confirmed `J`/`JAL`, fingerprints terminator+delay bytes, and leaves `JR/JALR` plus memory-bearing dispatcher delay slots out of v0.

**Tech Stack:** C++20, CMake/CTest, Windows x86-64 machine-code emitter, Win64 ABI, GitHub Actions `windows-2022` / MSVC 19.44.

**Spec:** `docs/superpowers/specs/2026-09-05-r5900-direct-jump-call-v0-design.md`

## Global Constraints

- Work on `feature/r5900-direct-jump-call-v0`; approved base milestone is `e232282fd4b997053cf26c95721bc457f8d66610` plus the committed design/plan documents.
- Preserve dependency direction; never add `b3r_recompiler -> b3r_runtime`.
- `J` and `JAL` are block terminators, never body IR instructions.
- Each direct transfer has exactly one architectural delay-slot IR instruction.
- `JAL` writes `uint64(uint32(guest_pc + 8))` to `GPR31.low64` before the delay slot and preserves `GPR31.high64`.
- `DirectJump` carries `target_pc` with `link_pc == 0`; `DirectCall` carries `target_pc` and exact `link_pc == guest_pc + 8`.
- Dispatcher v0 rejects `SQ` in `J`/`JAL` delay slots with `LoweringFailure`.
- `JR`, `JALR`, branch-and-link, branch-likely, syscall/HLE, graphics, audio, input, and game boot remain out of scope.
- Cache identity remains start PC + exact ordered guest words + FNV fingerprint; direct-transfer cache words include terminator and delay slot.
- Keep RW -> RX publication and `FlushInstructionCache`; helper-bearing blocks continue obeying Win64 shadow-space/alignment rules.
- Synthetic CI may establish `CI_VALIDATED`; never claim `EXTERNALLY_VALIDATED` without a legal external ELF run.

## File Structure

**Modify:**
- `src/recompiler/r5900_ir.h` — typed terminator model.
- `src/recompiler/r5900_ir_validation.cpp` — block terminator validation.
- `src/recompiler/r5900_ir_executor.cpp` — reference semantics.
- `src/recompiler/windows/r5900_x64_backend.cpp` — native emission.
- `src/recompiler/windows/r5900_block_dispatcher.cpp` — analyzer-to-IR and cache integration.
- `tests/r5900_block_dispatcher_startup_windows_tests.cpp` — startup fixture/E2E.
- `CMakeLists.txt` — four new focused test targets.
- `README.md`, `PROGRESS.md` — milestone evidence.

**Create:**
- `tests/r5900_ir_direct_transfer_validation_tests.cpp`
- `tests/r5900_ir_direct_transfer_executor_tests.cpp`
- `tests/r5900_x64_direct_transfer_windows_tests.cpp`
- `tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp`

---

### Task 1: Direct-transfer IR model and validation

**Files:**
- Modify: `src/recompiler/r5900_ir.h`
- Modify: `src/recompiler/r5900_ir_validation.cpp`
- Create: `tests/r5900_ir_direct_transfer_validation_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: existing `R5900IrBlock`, `R5900IrInstruction`, `validate_r5900_ir_instruction`.
- Produces: `R5900IrTerminatorKind::DirectJump`, `R5900IrTerminatorKind::DirectCall`, `R5900IrTerminator::target_pc`, `R5900IrTerminator::link_pc`.

- [ ] **Step 1: Add the focused test target and write RED tests**

Add beside `r5900_ir_block_validation_tests`:

```cmake
add_executable(r5900_ir_direct_transfer_validation_tests
  tests/r5900_ir_direct_transfer_validation_tests.cpp
)
target_link_libraries(r5900_ir_direct_transfer_validation_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_direct_transfer_validation_tests
  COMMAND r5900_ir_direct_transfer_validation_tests)
```

Create these complete builders in the test:

```cpp
R5900IrOperand gpr(std::uint8_t index) {
    R5900IrOperand value{};
    value.kind = R5900IrOperandKind::Gpr;
    value.gpr_index = index;
    return value;
}

R5900IrInstruction nop(std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::Nop;
    return ir;
}

R5900IrBlock direct_jump(std::uint32_t pc,
                         std::uint32_t target,
                         R5900IrInstruction delay) {
    R5900IrBlock block{};
    block.terminator.guest_pc = pc;
    block.terminator.kind = R5900IrTerminatorKind::DirectJump;
    block.terminator.target_pc = target;
    block.terminator.delay_slot = {delay};
    return block;
}

R5900IrBlock direct_call(std::uint32_t pc,
                         std::uint32_t target,
                         R5900IrInstruction delay) {
    auto block = direct_jump(pc, target, delay);
    block.terminator.kind = R5900IrTerminatorKind::DirectCall;
    block.terminator.link_pc = pc + 8u;
    return block;
}
```

Assert valid `J`/`JAL` plus these failures: unaligned target, nonzero `DirectJump.link_pc`, wrong `DirectCall.link_pc`, nonempty inputs, nonzero `taken_pc`, nonzero `fallthrough_pc`, missing delay, two delay instructions, and invalid delay opcode. Use `MalformedInstruction` for shape failures and `UnsupportedOpcode` for the invalid delay opcode.

- [ ] **Step 2: Run RED**

```powershell
cmake -S . -B build -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target r5900_ir_direct_transfer_validation_tests
```

Expected: compile failure naming missing `DirectJump`, `DirectCall`, `target_pc`, or `link_pc`.

- [ ] **Step 3: Extend `R5900IrTerminator`**

In `src/recompiler/r5900_ir.h`:

```cpp
enum class R5900IrTerminatorKind {
    Fallthrough = 0,
    BranchEqual64,
    DirectJump,
    DirectCall,
};
```

Append before `delay_slot`:

```cpp
std::uint32_t target_pc{};
std::uint32_t link_pc{};
```

- [ ] **Step 4: Implement kind-specific validator helpers**

Add a helper for one validated delay instruction:

```cpp
R5900IrValidationResult validate_single_delay_slot(
    const R5900IrTerminator& terminator,
    std::size_t index) {
    if (terminator.delay_slot.size() != 1u) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index,
                       terminator.guest_pc,
                       "control transfer requires exactly one delay-slot instruction");
    }
    return validate_r5900_ir_instruction(terminator.delay_slot.front(), index);
}
```

Then restructure `validate_r5900_ir_block` after body validation. First reject unaligned `guest_pc`. Use a `switch` with these exact checks:

```cpp
case R5900IrTerminatorKind::Fallthrough:
    if ((terminator.fallthrough_pc & 3u) != 0u ||
        !terminator.inputs.empty() || !terminator.delay_slot.empty() ||
        terminator.taken_pc != 0u || terminator.target_pc != 0u ||
        terminator.link_pc != 0u) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       block.body.size(), terminator.guest_pc,
                       "malformed fallthrough terminator");
    }
    return {};

case R5900IrTerminatorKind::DirectJump:
    if (!terminator.inputs.empty() || terminator.taken_pc != 0u ||
        terminator.fallthrough_pc != 0u || terminator.link_pc != 0u ||
        (terminator.target_pc & 3u) != 0u) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       block.body.size(), terminator.guest_pc,
                       "malformed direct-jump terminator");
    }
    return validate_single_delay_slot(terminator, block.body.size());

case R5900IrTerminatorKind::DirectCall:
    if (!terminator.inputs.empty() || terminator.taken_pc != 0u ||
        terminator.fallthrough_pc != 0u ||
        (terminator.target_pc & 3u) != 0u ||
        (terminator.link_pc & 3u) != 0u ||
        terminator.link_pc != static_cast<std::uint32_t>(terminator.guest_pc + 8u)) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       block.body.size(), terminator.guest_pc,
                       "malformed direct-call terminator");
    }
    return validate_single_delay_slot(terminator, block.body.size());
```

For `BranchEqual64`, retain the existing two-GPR and one-delay rules, add `target_pc == 0` and `link_pc == 0`, and keep taken/fallthrough alignment checks. Unknown terminator kind returns `UnsupportedOpcode`.

- [ ] **Step 5: Run GREEN and regression tests**

```powershell
cmake --build build --config Release --target r5900_ir_direct_transfer_validation_tests r5900_ir_block_validation_tests
ctest --test-dir build -C Release -R r5900_ir_direct_transfer_validation_tests --output-on-failure
ctest --test-dir build -C Release -R r5900_ir_block_validation_tests --output-on-failure
```

Expected: both PASS.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/recompiler/r5900_ir.h src/recompiler/r5900_ir_validation.cpp tests/r5900_ir_direct_transfer_validation_tests.cpp
git commit -m "feat: model R5900 direct transfer terminators"
```

---

### Task 2: Reference `J` / `JAL` execution

**Files:**
- Modify: `src/recompiler/r5900_ir_executor.cpp`
- Create: `tests/r5900_ir_direct_transfer_executor_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 direct terminators.
- Produces: reference oracle for Task 3.

- [ ] **Step 1: Register test and define all test builders**

Add:

```cmake
add_executable(r5900_ir_direct_transfer_executor_tests
  tests/r5900_ir_direct_transfer_executor_tests.cpp
)
target_link_libraries(r5900_ir_direct_transfer_executor_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_direct_transfer_executor_tests
  COMMAND r5900_ir_direct_transfer_executor_tests)
```

In the test define `gpr`, `nop`, `direct_jump`, and `direct_call` with the exact definitions from Task 1, plus:

```cpp
R5900IrOperand immediate(std::int64_t value) {
    R5900IrOperand operand{};
    operand.kind = R5900IrOperandKind::Immediate;
    operand.immediate = value;
    return operand;
}

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

R5900IrInstruction store128(std::uint8_t base,
                            std::uint8_t source,
                            std::int16_t imm,
                            std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::Store128;
    ir.inputs = {gpr(base), gpr(source), immediate(imm)};
    return ir;
}
```

- [ ] **Step 2: Write RED execution cases**

Cover exactly:

1. `direct_jump(0x00107000, 0x00107100, addiu(7,7,1,0x00107004))`: target returned, delay once, r31 unchanged.
2. `direct_call(0x00107200, 0x00107300, addiu(23,31,0,0x00107204))`: `r31.low64 == 0x00107208`, seeded nonzero `r31.high64` preserved, `r23 == 0x00107208`.
3. `direct_call(0x00107400, 0x00107500, addiu(31,0,9,0x00107404))`: final low64 is 9 and high64 remains seeded sentinel.
4. Construct a call block with `block.body = {store128(2,3,0,0x00107600)}` and `block.terminator = direct_call(0x00107604,0x00107700,addiu(24,0,1,0x00107608)).terminator`; execute with `R5900IrExecutionContext context{}; context.state = &state;` and no memory callback. Assert `MemoryAccessFailure`, original r31 unchanged, r24 unchanged, and `memory_fault.guest_pc == 0x00107600`.

- [ ] **Step 3: Run RED**

```powershell
cmake --build build --config Release --target r5900_ir_direct_transfer_executor_tests
ctest --test-dir build -C Release -R r5900_ir_direct_transfer_executor_tests --output-on-failure
```

Expected: failures report unsupported block terminator.

- [ ] **Step 4: Implement reference semantics**

In `execute_r5900_ir_block`:

```cpp
case R5900IrTerminatorKind::DirectJump: {
    const auto delay = execute_ir_sequence(block.terminator.delay_slot, context);
    if (!delay.ok()) return map_block_execution_failure(delay);
    return {R5900IrExecutionError::None, {}, block.terminator.target_pc};
}

case R5900IrTerminatorKind::DirectCall: {
    state.gpr[31].low64 = static_cast<std::uint64_t>(block.terminator.link_pc);
    normalize_zero(state);
    const auto delay = execute_ir_sequence(block.terminator.delay_slot, context);
    if (!delay.ok()) return map_block_execution_failure(delay);
    return {R5900IrExecutionError::None, {}, block.terminator.target_pc};
}
```

Leave `high64` untouched. Body validation/execution stays before the switch, so body failure precedes link creation.

- [ ] **Step 5: Run GREEN/regressions**

```powershell
cmake --build build --config Release --target r5900_ir_direct_transfer_executor_tests r5900_ir_block_executor_tests r5900_ir_store128_executor_tests
ctest --test-dir build -C Release -R r5900_ir_direct_transfer_executor_tests --output-on-failure
ctest --test-dir build -C Release -R r5900_ir_block_executor_tests --output-on-failure
ctest --test-dir build -C Release -R r5900_ir_store128_executor_tests --output-on-failure
```

Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/recompiler/r5900_ir_executor.cpp tests/r5900_ir_direct_transfer_executor_tests.cpp
git commit -m "feat: execute R5900 direct jump and call IR"
```

---

### Task 3: Windows x86-64 direct-transfer backend

**Files:**
- Modify: `src/recompiler/windows/r5900_x64_backend.cpp`
- Create: `tests/r5900_x64_direct_transfer_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 1-2.
- Produces: native support from `compile_r5900_ir_x64(const R5900IrBlock&)`; no public header change.

- [ ] **Step 1: Register Windows-only target**

```cmake
add_executable(r5900_x64_direct_transfer_windows_tests
  tests/r5900_x64_direct_transfer_windows_tests.cpp
)
target_link_libraries(r5900_x64_direct_transfer_windows_tests PRIVATE b3r_recompiler_x64)
add_test(NAME r5900_x64_direct_transfer_windows_tests
  COMMAND r5900_x64_direct_transfer_windows_tests)
```

- [ ] **Step 2: Write differential RED tests**

Define the same `gpr`, `immediate`, `nop`, `addiu`, `store128`, `direct_jump`, and `direct_call` builders locally in this standalone test. Add:

```cpp
struct MemoryProbe {
    bool succeed{true};
    std::uint32_t address{};
    std::uint64_t low64{};
    std::uint64_t high64{};
};

bool write128(void* user,
              std::uint32_t address,
              std::uint64_t low64,
              std::uint64_t high64) noexcept {
    auto& probe = *static_cast<MemoryProbe*>(user);
    probe.address = address;
    probe.low64 = low64;
    probe.high64 = high64;
    return probe.succeed;
}
```

For each block, clone initial state and memory probe, execute one copy with `execute_r5900_ir_block`, compile/execute the other with `compile_r5900_ir_x64`, and compare full state, `next_pc`, execution error, and memory fault. Cases: J+ADDIU delay; JAL+delay reading r31; JAL+delay writing r31; J+successful `Store128` delay; JAL+failing `Store128` delay. In the failing JAL-delay case assert link committed before `MemoryAccessFailure` in both paths.

- [ ] **Step 3: Run RED**

```powershell
cmake --build build --config Release --target r5900_x64_direct_transfer_windows_tests
ctest --test-dir build -C Release -R r5900_x64_direct_transfer_windows_tests --output-on-failure
```

Expected: compile result reports unsupported block terminator.

- [ ] **Step 4: Implement shared direct-transfer emitter**

Add:

```cpp
PendingX64Code compile_direct_transfer_code(const R5900IrBlock& block) {
    const bool helper_frame =
        sequence_needs_helper(block.body) ||
        sequence_needs_helper(block.terminator.delay_slot);

    std::vector<std::uint8_t> bytes;
    bytes.reserve(128u + block.body.size() * 128u +
                  block.terminator.delay_slot.size() * 256u);
    if (helper_frame) emit_helper_frame_prologue(bytes);
    emit_zero_gpr0(bytes);

    const auto body = emit_ir_sequence(bytes, block.body, 0u, helper_frame);
    if (!body.ok()) return pending_failure(body.error, body.message);

    if (block.terminator.kind == R5900IrTerminatorKind::DirectCall) {
        emit_mov_eax_imm32(bytes, block.terminator.link_pc);
        emit_store_rax_to_state(bytes, gpr_low64_offset(31u));
    }

    emit_zero_gpr0(bytes);
    const auto delay = emit_ir_sequence(bytes,
                                        block.terminator.delay_slot,
                                        block.body.size() + 1u,
                                        helper_frame);
    if (!delay.ok()) return pending_failure(delay.error, delay.message);

    emit_zero_gpr0(bytes);
    emit_mov_eax_imm32(bytes, block.terminator.target_pc);
    if (helper_frame) emit_helper_frame_epilogue(bytes);
    bytes.push_back(0xc3u);
    return publish_code(bytes);
}
```

Add switch cases:

```cpp
case R5900IrTerminatorKind::DirectJump:
case R5900IrTerminatorKind::DirectCall:
    pending = compile_direct_transfer_code(block);
    break;
```

- [ ] **Step 5: Run GREEN/regressions**

```powershell
cmake --build build --config Release --target r5900_x64_direct_transfer_windows_tests r5900_x64_backend_windows_tests r5900_x64_store128_windows_tests
ctest --test-dir build -C Release -R r5900_x64_direct_transfer_windows_tests --output-on-failure
ctest --test-dir build -C Release -R r5900_x64_backend_windows_tests --output-on-failure
ctest --test-dir build -C Release -R r5900_x64_store128_windows_tests --output-on-failure
```

Expected: all PASS, including helper-frame failure return without access violation.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/recompiler/windows/r5900_x64_backend.cpp tests/r5900_x64_direct_transfer_windows_tests.cpp
git commit -m "feat: emit native R5900 direct jump and call"
```

---

### Task 4: Dispatcher and cache integration

**Files:**
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Create: `tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: analyzer `DirectJump`/`DirectCall`, decoder `direct_target`, Tasks 1-3.
- Produces: dispatcher-native J/JAL continuation and exact cache coverage.

- [ ] **Step 1: Register focused dispatcher target and fixture helpers**

```cmake
add_executable(r5900_block_dispatcher_direct_transfer_windows_tests
  tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp
)
target_link_libraries(r5900_block_dispatcher_direct_transfer_windows_tests PRIVATE
  b3r_recompiler_dispatcher_x64
)
add_test(NAME r5900_block_dispatcher_direct_transfer_windows_tests
  COMMAND r5900_block_dispatcher_direct_transfer_windows_tests)
```

Copy the existing synthetic ELF `make_memory`, `put_u16`, `put_u32`, `i_type`, and `r_type` fixture utility definitions into the standalone test and add:

```cpp
constexpr std::uint32_t j_type(std::uint8_t op, std::uint32_t target) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           ((target >> 2u) & 0x03ffffffu);
}
```

- [ ] **Step 2: Write dispatcher RED cases**

Create fixtures proving:

1. `J` at entry + ADDIU delay + poison fallthrough + target `JR`: executes J+delay, follows target, stops `ControlFlow` at JR, poison unchanged.
2. `JAL` at entry + delay `ADDIU r23,r31,0` + target `JR`: link is PC+8, high64 sentinel preserved, delay sees link.
3. Repeat unchanged J/JAL fixture: first run cache miss, second run cache hit.
4. After first run mutate transfer or delay word via `memory.write_u32`; next run increments recompilations and follows changed semantics.
5. `SQ` as J delay and as JAL delay: `LoweringFailure` at delay PC; no block/instruction completion and no target memory write.
6. Entry `JR` and `JALR`: remain `ControlFlow`; mapped delay slots do not execute.

- [ ] **Step 3: Run RED**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_direct_transfer_windows_tests
ctest --test-dir build -C Release -R r5900_block_dispatcher_direct_transfer_windows_tests --output-on-failure
```

Expected: J/JAL stop at their own PCs with `ControlFlow`.

- [ ] **Step 4: Recognize analyzer-confirmed direct terminators**

Near `has_supported_beq` add:

```cpp
const bool has_supported_j =
    block.end_kind == analysis::R5900BlockEndKind::DirectJump &&
    !block.instructions.empty() &&
    block.instructions.back().decoded.instruction == R5900Instruction::J;
const bool has_supported_jal =
    block.end_kind == analysis::R5900BlockEndKind::DirectCall &&
    !block.instructions.empty() &&
    block.instructions.back().decoded.instruction == R5900Instruction::Jal;
const bool has_supported_direct = has_supported_j || has_supported_jal;
```

Select `const analysis::R5900InstructionSite* transfer_site = has_supported_direct ? &block.instructions.back() : nullptr;`. In the body loop skip the final instruction when it is supported BEQ or supported direct transfer. Unsupported jump-class instructions continue setting `boundary_reason = ControlFlow`.

- [ ] **Step 5: Add cache words and direct IR construction**

For supported direct transfer require `transfer_site` and `block.delay_slot`. Append both guest words after body words. Build:

```cpp
const auto target = transfer_site->decoded.direct_target(transfer_site->pc);
if (!target.has_value()) {
    result.reason = R5900DispatchStopReason::AnalysisFailure;
    result.next_pc = transfer_site->pc;
    result.message = format_stage_error(
        "analysis", transfer_site->pc,
        "decoded direct transfer unexpectedly lacks target");
    return result;
}

ir_block.terminator.guest_pc = transfer_site->pc;
ir_block.terminator.guest_raw = transfer_site->decoded.raw;
ir_block.terminator.kind = has_supported_j
    ? R5900IrTerminatorKind::DirectJump
    : R5900IrTerminatorKind::DirectCall;
ir_block.terminator.target_pc = *target;
ir_block.terminator.link_pc = has_supported_jal ? transfer_site->pc + 8u : 0u;
```

For the analyzer delay site, reject `Sq` with `LoweringFailure`. Otherwise lower it and require exactly one IR instruction, then assign `ir_block.terminator.delay_slot`.

Set cached range/count:

```cpp
replacement.end_pc_exclusive = transfer_site->pc + 8u;
replacement.guest_instruction_count = guest_words.size();
```

A supported J/JAL must not set `boundary_reason`; successful native `next_pc` must drive dispatch continuation.

- [ ] **Step 6: Run GREEN/regressions**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_direct_transfer_windows_tests r5900_block_dispatcher_windows_tests r5900_block_dispatcher_store128_windows_tests
ctest --test-dir build -C Release -R r5900_block_dispatcher_direct_transfer_windows_tests --output-on-failure
ctest --test-dir build -C Release -R r5900_block_dispatcher_windows_tests --output-on-failure
ctest --test-dir build -C Release -R r5900_block_dispatcher_store128_windows_tests --output-on-failure
```

Expected: all PASS.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp
git commit -m "feat: dispatch R5900 J and JAL blocks"
```

---

### Task 5: Startup E2E through J/JAL

**Files:**
- Modify: `tests/r5900_block_dispatcher_startup_windows_tests.cpp`

**Interfaces:**
- Consumes: Tasks 1-4.
- Produces: synthetic proof at 5 blocks / 87 instructions, stopping at `JR 0x001001a4`.

- [ ] **Step 1: Add exact layout constants and encoder**

```cpp
constexpr std::uint32_t kDirectJumpPc = 0x00100164u;
constexpr std::uint32_t kDirectJumpTarget = 0x00100180u;
constexpr std::uint32_t kDirectCallPc = 0x00100180u;
constexpr std::uint32_t kDirectCallTarget = 0x001001a0u;
constexpr std::uint32_t kIndirectReturnPc = 0x001001a4u;
constexpr std::uint32_t kExpectedLinkPc = 0x00100188u;

constexpr std::uint32_t j_type(std::uint8_t op, std::uint32_t target) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           ((target >> 2u) & 0x03ffffffu);
}
```

Change `words.reserve(89u)` to `words.reserve(105u)`.

- [ ] **Step 2: Replace old sentinel with the 18-word direct-transfer fixture**

After the existing SQ at `0x00100160`, append exactly:

```cpp
words.push_back(j_type(0x02u, 0x00100180u));            // 0x164 J
words.push_back(i_type(0x09u, 0u, 22u, 0x0033u));       // 0x168 delay
words.push_back(i_type(0x09u, 0u, 25u, 1u));            // 0x16c poison
words.push_back(i_type(0x09u, 0u, 26u, 1u));            // 0x170 poison
words.push_back(i_type(0x09u, 0u, 27u, 1u));            // 0x174 poison
words.push_back(i_type(0x09u, 0u, 28u, 1u));            // 0x178 poison
words.push_back(i_type(0x09u, 0u, 30u, 1u));            // 0x17c poison
words.push_back(j_type(0x03u, 0x001001a0u));            // 0x180 JAL
words.push_back(i_type(0x09u, 31u, 23u, 0u));           // 0x184 delay sees r31
words.push_back(i_type(0x09u, 0u, 25u, 2u));            // 0x188 poison
words.push_back(i_type(0x09u, 0u, 26u, 2u));            // 0x18c poison
words.push_back(i_type(0x09u, 0u, 27u, 2u));            // 0x190 poison
words.push_back(i_type(0x09u, 0u, 28u, 2u));            // 0x194 poison
words.push_back(i_type(0x09u, 0u, 30u, 2u));            // 0x198 poison
words.push_back(0u);                                    // 0x19c poison guard
words.push_back(i_type(0x09u, 0u, 24u, 0x0055u));       // 0x1a0 callee
words.push_back(r_type(31u, 0u, 0u, 0u, 0x08u));        // 0x1a4 JR r31
words.push_back(0u);                                    // 0x1a8 mapped JR delay
expect(words.size() == 105u, "synthetic J/JAL fixture count mismatch");
```

- [ ] **Step 3: Replace old synthetic expectations**

Assert:

```cpp
expect(result.reason == R5900DispatchStopReason::ControlFlow,
       "startup must stop at unsupported JR");
expect(result.next_pc == kIndirectReturnPc, "startup JR boundary mismatch");
expect(result.blocks_executed == 5u, "startup block count mismatch");
expect(result.instructions_executed == 87u, "startup instruction count mismatch");
expect(state.gpr[22].low64 == 0x33u, "J delay slot mismatch");
expect(state.gpr[23].low64 == kExpectedLinkPc, "JAL delay link observation mismatch");
expect(state.gpr[24].low64 == 0x55u, "callee prefix mismatch");
expect(state.gpr[31].low64 == kExpectedLinkPc, "JAL link mismatch");
expect(state.gpr[31].high64 == 0u, "startup r31 high64 mismatch");
expect(state.gpr[25].low64 == 0u && state.gpr[26].low64 == 0u &&
       state.gpr[27].low64 == 0u && state.gpr[28].low64 == 0u &&
       state.gpr[30].low64 == 0u,
       "direct-transfer poison fallthrough must remain untouched");
```

Keep the existing SQ target zeroing and surrounding-byte assertions.

- [ ] **Step 4: Run startup GREEN**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_startup_windows_tests
ctest --test-dir build -C Release -R r5900_block_dispatcher_startup_windows_tests --output-on-failure
```

Expected: PASS, `next_pc = 0x001001a4`, 5 blocks, 87 instructions.

- [ ] **Step 5: Preserve conservative external-ELF assertions**

External mode retains only:

```cpp
expect(result.instructions_executed >= 82u,
       "real startup must execute at least through SQ");
expect(result.next_pc != kSqPc,
       "real startup must advance beyond SQ");
```

and the existing mapped/zeroed SQ-target checks. Do not hard-code real post-SQ J/JAL/JR PCs.

- [ ] **Step 6: Commit**

```bash
git add tests/r5900_block_dispatcher_startup_windows_tests.cpp
git commit -m "test: advance startup through R5900 J and JAL"
```

---

### Task 6: Full verification and milestone documentation

**Files:**
- Modify: `README.md`
- Modify: `PROGRESS.md`

**Interfaces:**
- Consumes: complete feature and CI logs.
- Produces: final `CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION` milestone evidence.

- [ ] **Step 1: Run the full Windows workflow on exact feature HEAD**

Require success for Configure, Build, Test, frame-pacing telemetry, pacing probe smoke, analyzer package stage/validation, and pacing package stage/validation.

The expected test count is prior 39 + four new registered targets = 43. If concurrent repository changes alter the count, require the actual suite to report 0 failures rather than editing tests to force 43.

- [ ] **Step 2: Verify focused and regression tests in CI logs**

Focused PASS set:

```text
r5900_ir_direct_transfer_validation_tests
r5900_ir_direct_transfer_executor_tests
r5900_x64_direct_transfer_windows_tests
r5900_block_dispatcher_direct_transfer_windows_tests
r5900_block_dispatcher_startup_windows_tests
```

Regression PASS set:

```text
r5900_ir_block_validation_tests
r5900_ir_block_executor_tests
r5900_x64_store128_windows_tests
r5900_block_dispatcher_store128_windows_tests
```

- [ ] **Step 3: Update README**

State:

```text
R5900 direct J/JAL + delay slot v0: CI_VALIDATED
- J and JAL are typed native terminators.
- JAL writes zero-extended PC+8 to GPR31.low64 before the delay slot and preserves high64.
- Synthetic startup reaches unsupported JR at 0x001001a4 after 5 blocks / 87 instructions.
- JR/JALR remain unsupported.
- SQ in J/JAL delay slots remains outside dispatcher v0.
- The legal external ELF has not yet been run through this expanded native path.
- The game does not boot yet.
```

- [ ] **Step 4: Update PROGRESS with exact evidence**

Record final feature SHA, workflow run/job IDs, MSVC version, CTest pass count, focused target results, and pacing telemetry. Set startup status to `CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION`. Name the next repository-only candidate as `JR/JALR`, but state that an external legal ELF run may reveal a different immediate blocker and takes precedence.

- [ ] **Step 5: Commit docs and run fresh final CI**

```bash
git add README.md PROGRESS.md
git commit -m "docs: record R5900 direct J/JAL milestone"
```

Run the Windows workflow on this exact documentation HEAD and require 0 test failures plus all package/pacing checks green.

- [ ] **Step 6: Review before integration**

Invoke `superpowers:requesting-code-review`, then `superpowers:verification-before-completion`. Explicit review checklist:

```text
link-before-delay ordering
GPR31.high64 preservation
J/JAL excluded from body lowering
helper-frame failure epilogue for direct-transfer delay IR
cache words include terminator+delay
supported J/JAL leaves no stale boundary_reason
JR/JALR remain unexecuted
poison regions remain untouched
no EXTERNALLY_VALIDATED claim
no game-boot claim
```

Only after fresh CI and review are green should `feature/r5900-direct-jump-call-v0` be offered for fast-forward integration into `feature/r5900-beq-delay-slot-v0`.
