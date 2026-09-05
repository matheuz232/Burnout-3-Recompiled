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
- `tests/r5900_direct_transfer_test_support.h` — IR builders shared only by direct-transfer tests.
- `tests/r5900_ir_direct_transfer_validation_tests.cpp`
- `tests/r5900_ir_direct_transfer_executor_tests.cpp`
- `tests/r5900_x64_direct_transfer_windows_tests.cpp`
- `tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp`

---

### Task 1: Direct-transfer IR model and validation

**Files:**
- Modify: `src/recompiler/r5900_ir.h`
- Modify: `src/recompiler/r5900_ir_validation.cpp`
- Create: `tests/r5900_direct_transfer_test_support.h`
- Create: `tests/r5900_ir_direct_transfer_validation_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: existing `R5900IrBlock`, `R5900IrInstruction`, `validate_r5900_ir_instruction`.
- Produces: `R5900IrTerminatorKind::DirectJump`, `R5900IrTerminatorKind::DirectCall`, `R5900IrTerminator::target_pc`, `R5900IrTerminator::link_pc`, plus test-only builders in namespace `b3r::test_support`.

- [ ] **Step 1: Add the focused test target**

Add beside `r5900_ir_block_validation_tests`:

```cmake
add_executable(r5900_ir_direct_transfer_validation_tests
  tests/r5900_ir_direct_transfer_validation_tests.cpp
)
target_link_libraries(r5900_ir_direct_transfer_validation_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_direct_transfer_validation_tests
  COMMAND r5900_ir_direct_transfer_validation_tests)
```

- [ ] **Step 2: Create the shared direct-transfer test builders**

Create `tests/r5900_direct_transfer_test_support.h`:

```cpp
#pragma once

#include "recompiler/r5900_ir.h"

#include <cstdint>

namespace b3r::test_support {
using namespace b3r::recompiler;

inline R5900IrOperand gpr(std::uint8_t index) {
    R5900IrOperand value{};
    value.kind = R5900IrOperandKind::Gpr;
    value.gpr_index = index;
    return value;
}

inline R5900IrOperand immediate(std::int64_t value) {
    R5900IrOperand operand{};
    operand.kind = R5900IrOperandKind::Immediate;
    operand.immediate = value;
    return operand;
}

inline R5900IrInstruction nop(std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::Nop;
    return ir;
}

inline R5900IrInstruction addiu(std::uint8_t rt,
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

inline R5900IrInstruction store128(std::uint8_t base,
                                   std::uint8_t source,
                                   std::int16_t imm,
                                   std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = R5900IrOpcode::Store128;
    ir.inputs = {gpr(base), gpr(source), immediate(imm)};
    return ir;
}

inline R5900IrBlock direct_jump(std::uint32_t pc,
                                std::uint32_t target,
                                R5900IrInstruction delay) {
    R5900IrBlock block{};
    block.terminator.guest_pc = pc;
    block.terminator.kind = R5900IrTerminatorKind::DirectJump;
    block.terminator.target_pc = target;
    block.terminator.delay_slot = {delay};
    return block;
}

inline R5900IrBlock direct_call(std::uint32_t pc,
                                std::uint32_t target,
                                R5900IrInstruction delay) {
    auto block = direct_jump(pc, target, delay);
    block.terminator.kind = R5900IrTerminatorKind::DirectCall;
    block.terminator.link_pc = pc + 8u;
    return block;
}

} // namespace b3r::test_support
```

This header is test-only and must not be included by production code.

- [ ] **Step 3: Write validation RED cases**

In `tests/r5900_ir_direct_transfer_validation_tests.cpp`, include the support header and assert valid `direct_jump(0x00106000,0x00106100,nop(0x00106004))` and valid `direct_call(0x00106200,0x00106300,nop(0x00106204))`.

Add explicit invalid cases for: unaligned `target_pc`; nonzero `DirectJump.link_pc`; wrong `DirectCall.link_pc`; nonempty `inputs`; nonzero `taken_pc`; nonzero `fallthrough_pc`; missing delay; two delays; invalid delay opcode. Shape failures expect `MalformedInstruction`; invalid delay opcode expects `UnsupportedOpcode`.

- [ ] **Step 4: Run RED**

```powershell
cmake -S . -B build -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target r5900_ir_direct_transfer_validation_tests
```

Expected: compile failure naming missing `DirectJump`, `DirectCall`, `target_pc`, or `link_pc`.

- [ ] **Step 5: Extend `R5900IrTerminator`**

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

- [ ] **Step 6: Implement kind-specific validation**

Add in the validation translation unit:

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

Restructure `validate_r5900_ir_block` so unaligned `terminator.guest_pc` is always rejected first, then validate active fields by kind. `Fallthrough` requires aligned fallthrough and zero/empty branch/direct state. `BranchEqual64` keeps its two-GPR, aligned taken/fallthrough, one-delay contract and additionally requires `target_pc == 0` and `link_pc == 0`.

For direct kinds use these checks:

```cpp
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

Unknown terminator kind returns `UnsupportedOpcode`.

- [ ] **Step 7: Run GREEN and regression tests**

```powershell
cmake --build build --config Release --target r5900_ir_direct_transfer_validation_tests r5900_ir_block_validation_tests
ctest --test-dir build -C Release -R r5900_ir_direct_transfer_validation_tests --output-on-failure
ctest --test-dir build -C Release -R r5900_ir_block_validation_tests --output-on-failure
```

Expected: both PASS.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt src/recompiler/r5900_ir.h src/recompiler/r5900_ir_validation.cpp tests/r5900_direct_transfer_test_support.h tests/r5900_ir_direct_transfer_validation_tests.cpp
git commit -m "feat: model R5900 direct transfer terminators"
```

---

### Task 2: Reference `J` / `JAL` execution

**Files:**
- Modify: `src/recompiler/r5900_ir_executor.cpp`
- Create: `tests/r5900_ir_direct_transfer_executor_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 direct terminators and `tests/r5900_direct_transfer_test_support.h`.
- Produces: reference semantic oracle for Task 3.

- [ ] **Step 1: Register and write executor RED tests**

Add:

```cmake
add_executable(r5900_ir_direct_transfer_executor_tests
  tests/r5900_ir_direct_transfer_executor_tests.cpp
)
target_link_libraries(r5900_ir_direct_transfer_executor_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_direct_transfer_executor_tests
  COMMAND r5900_ir_direct_transfer_executor_tests)
```

Include `r5900_direct_transfer_test_support.h` and cover exactly:

1. `direct_jump(0x00107000,0x00107100,addiu(7,7,1,0x00107004))`: target returned, delay once, r31 untouched.
2. `direct_call(0x00107200,0x00107300,addiu(23,31,0,0x00107204))`: `r31.low64 == 0x00107208`, seeded nonzero high64 preserved, r23 observes link.
3. `direct_call(0x00107400,0x00107500,addiu(31,0,9,0x00107404))`: delay write wins on low64 and preserves seeded high64.
4. A block with `body = {store128(2,3,0,0x00107600)}` and terminator copied from `direct_call(0x00107604,0x00107700,addiu(24,0,1,0x00107608)).terminator`; execute with state-only context and no memory callback. Expect `MemoryAccessFailure`, original r31 unchanged, r24 unchanged, and memory-fault PC `0x00107600`.

- [ ] **Step 2: Run RED**

```powershell
cmake --build build --config Release --target r5900_ir_direct_transfer_executor_tests
ctest --test-dir build -C Release -R r5900_ir_direct_transfer_executor_tests --output-on-failure
```

Expected: new valid blocks fail with unsupported block terminator.

- [ ] **Step 3: Implement reference semantics**

In `execute_r5900_ir_block` add:

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

Do not touch `state.gpr[31].high64`. Keep body validation/execution before terminator semantics.

- [ ] **Step 4: Run GREEN/regressions**

```powershell
cmake --build build --config Release --target r5900_ir_direct_transfer_executor_tests r5900_ir_block_executor_tests r5900_ir_store128_executor_tests
ctest --test-dir build -C Release -R r5900_ir_direct_transfer_executor_tests --output-on-failure
ctest --test-dir build -C Release -R r5900_ir_block_executor_tests --output-on-failure
ctest --test-dir build -C Release -R r5900_ir_store128_executor_tests --output-on-failure
```

Expected: all PASS.

- [ ] **Step 5: Commit**

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
- Consumes: Tasks 1-2 and test support header.
- Produces: `compile_r5900_ir_x64(const R5900IrBlock&)` support for both direct terminators; no public API change.

- [ ] **Step 1: Register Windows-only differential target**

```cmake
add_executable(r5900_x64_direct_transfer_windows_tests
  tests/r5900_x64_direct_transfer_windows_tests.cpp
)
target_link_libraries(r5900_x64_direct_transfer_windows_tests PRIVATE b3r_recompiler_x64)
add_test(NAME r5900_x64_direct_transfer_windows_tests
  COMMAND r5900_x64_direct_transfer_windows_tests)
```

- [ ] **Step 2: Write native/reference RED tests**

Include `r5900_direct_transfer_test_support.h`. Add a local memory fixture:

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

For each test clone state and probe, run the reference block against one context and compiled native block against the other, then compare all GPR low/high halves, HI/LO/HI1/LO1, SA, FPRs, FCR31, FP accumulator, error, next PC, and memory-fault fields.

Cases: J+ADDIU delay; JAL+delay reading r31; JAL+delay writing r31; J+successful Store128 delay; JAL+failing Store128 delay. The failing JAL-delay case must show link committed before both paths report `MemoryAccessFailure`.

- [ ] **Step 3: Run RED**

```powershell
cmake --build build --config Release --target r5900_x64_direct_transfer_windows_tests
ctest --test-dir build -C Release -R r5900_x64_direct_transfer_windows_tests --output-on-failure
```

Expected: compile result reports unsupported block terminator.

- [ ] **Step 4: Implement native direct-transfer emission**

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

In `compile_r5900_ir_x64(const R5900IrBlock&)` add:

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

Expected: all PASS, including helper-frame failure return without host crash.

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
- Consumes: analyzer `R5900BlockEndKind::DirectJump`/`DirectCall`, decoder `direct_target`, Tasks 1-3.
- Produces: dispatcher-native J/JAL continuation and exact cache coverage.

- [ ] **Step 1: Register focused dispatcher target**

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

- [ ] **Step 2: Build standalone dispatcher fixtures and RED cases**

In the test implement the existing synthetic ELF pattern directly: `put_u16`, `put_u32`, `make_memory`, `i_type`, `r_type`, and:

```cpp
constexpr std::uint32_t j_type(std::uint8_t op, std::uint32_t target) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           ((target >> 2u) & 0x03ffffffu);
}
```

Cover:

1. J at entry + ADDIU delay + poison fallthrough + target JR: J/delay counted, target reached, stop `ControlFlow` at JR, poison unchanged.
2. JAL at entry + `ADDIU r23,r31,0` delay + target JR: PC+8 link, high64 sentinel preserved, delay sees link.
3. Repeat unchanged fixture: first run cache miss, second run cache hit.
4. Mutate transfer or delay word through `write_u32`: next run recompiles and observes changed semantics.
5. SQ as J delay and JAL delay: `LoweringFailure` at delay PC, no completed transfer block, no memory side effect.
6. Entry JR and JALR: remain `ControlFlow`; mapped delay slot not executed.

- [ ] **Step 3: Run RED**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_direct_transfer_windows_tests
ctest --test-dir build -C Release -R r5900_block_dispatcher_direct_transfer_windows_tests --output-on-failure
```

Expected: J/JAL fixtures stop at the transfer PC with `ControlFlow`.

- [ ] **Step 4: Recognize supported direct terminators and generalize body selection**

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
const bool has_supported_terminator = has_supported_beq || has_supported_direct;
```

Create `transfer_site` for supported direct transfers. In the body loop skip the final instruction when it is any supported terminator. Update the empty-body guard from BEQ-only to:

```cpp
if (body_sites.empty() && !has_supported_terminator) {
    if (boundary_reason.has_value()) {
        result.reason = *boundary_reason;
        result.next_pc = boundary_pc;
        return result;
    }
    result.reason = R5900DispatchStopReason::UnsupportedInstruction;
    result.next_pc = current_pc;
    result.message = format_stage_error(
        "dispatch", current_pc,
        "analyzed block has no v0-executable instruction candidate");
    return result;
}
```

This is required for J/JAL at block entry.

- [ ] **Step 5: Add direct cache words and IR construction**

Reserve `guest_words` for body plus two words for any supported control terminator. For J/JAL require readable transfer and analyzer delay words and append them in that order after body words.

Build:

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

Reject `Sq` in the analyzer delay slot with `LoweringFailure`; otherwise lower and require exactly one IR instruction. Assign `terminator.delay_slot`.

For cached direct blocks set:

```cpp
replacement.end_pc_exclusive = transfer_site->pc + 8u;
replacement.guest_instruction_count = guest_words.size();
```

A supported J/JAL must not set `boundary_reason`; native `next_pc` must drive the next dispatcher iteration.

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

- [ ] **Step 2: Replace the old sentinel with the exact 18-word fixture**

After SQ at `0x00100160` append:

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
words.push_back(0u);                                    // 0x19c guard
words.push_back(i_type(0x09u, 0u, 24u, 0x0055u));       // 0x1a0 callee
words.push_back(r_type(31u, 0u, 0u, 0u, 0x08u));        // 0x1a4 JR r31
words.push_back(0u);                                    // 0x1a8 mapped JR delay
expect(words.size() == 105u, "synthetic J/JAL fixture count mismatch");
```

- [ ] **Step 3: Update synthetic assertions**

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

Keep all existing SQ target zeroing, surrounding-byte, GPR2/GPR3/GPR4, HI/LO, SA, FCR31, accumulator, and FPR assertions.

- [ ] **Step 4: Run startup GREEN**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_startup_windows_tests
ctest --test-dir build -C Release -R r5900_block_dispatcher_startup_windows_tests --output-on-failure
```

Expected: PASS, `next_pc = 0x001001a4`, 5 blocks, 87 instructions.

- [ ] **Step 5: Preserve conservative external-ELF contract**

Keep:

```cpp
expect(result.instructions_executed >= 82u,
       "real startup must execute at least through SQ");
expect(result.next_pc != kSqPc,
       "real startup must advance beyond SQ");
```

plus existing mapped/zeroed SQ-target checks. Do not hard-code real post-SQ J/JAL/JR PCs or block count.

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
- Produces: `CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION` evidence.

- [ ] **Step 1: Run full Windows CI on exact feature HEAD**

Require success for Configure, Build, Test, frame-pacing telemetry, pacing probe smoke, analyzer package stage/validation, and pacing package stage/validation.

Expected suite count is previous 39 + four new registered executables = 43 tests. If concurrent repository changes alter the count, require actual suite 0 failures rather than editing tests to force 43.

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

Record exactly these claims:

```text
R5900 direct J/JAL + delay slot v0: CI_VALIDATED
J and JAL are typed native terminators.
JAL writes zero-extended PC+8 to GPR31.low64 before the delay slot and preserves high64.
Synthetic startup reaches unsupported JR at 0x001001a4 after 5 blocks / 87 instructions.
JR/JALR remain unsupported.
SQ in J/JAL delay slots remains outside dispatcher v0.
The legal external ELF has not yet been run through this expanded native path.
The game does not boot yet.
```

- [ ] **Step 4: Update PROGRESS with exact evidence**

Record final feature SHA, workflow run/job IDs, MSVC version, CTest pass count, focused target results, and pacing telemetry. Set startup status to `CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION`. State `JR/JALR` as the next repository-only candidate while noting that an external legal ELF run may reveal a different immediate blocker and takes precedence.

- [ ] **Step 5: Commit docs and run fresh final CI**

```bash
git add README.md PROGRESS.md
git commit -m "docs: record R5900 direct J/JAL milestone"
```

Run Windows CI on this exact documentation HEAD and require 0 test failures plus all package/pacing checks green.

- [ ] **Step 6: Review before integration**

Invoke `superpowers:requesting-code-review`, then `superpowers:verification-before-completion`. Review checklist:

```text
link-before-delay ordering
GPR31.high64 preservation
J/JAL excluded from body lowering
helper-frame failure epilogue for direct-transfer delay IR
cache words include terminator+delay
entry J/JAL passes the generalized empty-body guard
supported J/JAL leaves no stale boundary_reason
JR/JALR remain unexecuted
poison regions remain untouched
no EXTERNALLY_VALIDATED claim
no game-boot claim
```

Only after fresh CI and review are green should `feature/r5900-direct-jump-call-v0` be offered for fast-forward integration into `feature/r5900-beq-delay-slot-v0`.
