# R5900 Direct J/JAL + Delay Slot v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute R5900 direct `J` and `JAL` natively with one architectural delay slot, correct `JAL` link semantics, dispatcher/cache integration, and a synthetic startup proof that advances from the validated `SQ` through `J` and `JAL` before stopping at unsupported `JR`.

**Architecture:** Extend `R5900IrTerminator` with explicit `DirectJump` and `DirectCall` kinds and direct-transfer fields. Validation, reference execution, and the Windows x86-64 backend share the same ordering: body first, `JAL` link write before the delay slot, delay slot exactly once, then return `target_pc`. The dispatcher recognizes analyzer-confirmed `J`/`JAL` terminators, includes terminator+delay words in cache identity, and keeps `JR/JALR` and memory-bearing delay slots outside v0.

**Tech Stack:** C++20, CMake/CTest, Windows x86-64 machine-code emitter, Win64 ABI, GitHub Actions `windows-2022` / MSVC 19.44.

**Spec:** `docs/superpowers/specs/2026-09-05-r5900-direct-jump-call-v0-design.md`

## Global Constraints

- Branch: `feature/r5900-direct-jump-call-v0`; base milestone commit is `e232282fd4b997053cf26c95721bc457f8d66610` plus the approved design commits.
- Preserve dependency direction: `b3r_runtime -> b3r_recompiler`, `b3r_analysis -> b3r_runtime`, dispatcher above those layers; never add `b3r_recompiler -> b3r_runtime`.
- `J` and `JAL` are terminators, never ordinary body IR instructions.
- Each direct transfer has exactly one architectural delay-slot IR instruction.
- `JAL`: `GPR31.low64 = uint64(uint32(guest_pc + 8))` before the delay slot; preserve `GPR31.high64`.
- `DirectJump` carries `target_pc` and no link state. `DirectCall` carries `target_pc` and exact `link_pc = guest_pc + 8`.
- Dispatcher v0 rejects `SQ` in `J`/`JAL` delay slots with `LoweringFailure`.
- `JR`, `JALR`, branch-and-link, branch-likely, syscall/HLE, graphics, audio, input, and game boot remain out of scope.
- Cache identity remains start PC + exact ordered guest words + FNV fingerprint; direct-transfer cache words include terminator and delay slot.
- Synthetic CI may establish `CI_VALIDATED`; do not claim `EXTERNALLY_VALIDATED` without running the user's legal ELF.
- Keep native code publication RW -> RX and `FlushInstructionCache`; helper-bearing blocks must continue obeying Win64 shadow-space/alignment rules.

## File Structure

**Modify:**
- `src/recompiler/r5900_ir.h` — typed direct-transfer terminator model.
- `src/recompiler/r5900_ir_validation.cpp` — block-level shape validation for the four terminator kinds.
- `src/recompiler/r5900_ir_executor.cpp` — reference semantics for `DirectJump`/`DirectCall`.
- `src/recompiler/windows/r5900_x64_backend.cpp` — native direct-transfer emission.
- `src/recompiler/windows/r5900_block_dispatcher.cpp` — analyzer-to-IR construction, cache words, and continuation for `J`/`JAL`.
- `tests/r5900_block_dispatcher_startup_windows_tests.cpp` — synthetic startup layout and final E2E assertions.
- `CMakeLists.txt` — register four focused test executables.
- `README.md` — milestone status and limitations.
- `PROGRESS.md` — CI evidence and next gate.

**Create:**
- `tests/r5900_ir_direct_transfer_validation_tests.cpp` — direct-transfer validator contract only.
- `tests/r5900_ir_direct_transfer_executor_tests.cpp` — reference ordering/link semantics only.
- `tests/r5900_x64_direct_transfer_windows_tests.cpp` — reference/native differential and helper-frame direct-transfer cases.
- `tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp` — dispatcher/cache behavior for `J`/`JAL` independent of startup fixture.

---

### Task 1: Direct-transfer IR model and validator

**Files:**
- Modify: `src/recompiler/r5900_ir.h`
- Modify: `src/recompiler/r5900_ir_validation.cpp`
- Create: `tests/r5900_ir_direct_transfer_validation_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: existing `R5900IrBlock`, `R5900IrInstruction`, `R5900IrValidationResult`, `validate_r5900_ir_instruction`.
- Produces: `R5900IrTerminatorKind::DirectJump`, `R5900IrTerminatorKind::DirectCall`, `R5900IrTerminator::target_pc`, `R5900IrTerminator::link_pc`; later tasks use these exact names.

- [ ] **Step 1: Register a focused validation test target and write the failing contract tests**

Add to `CMakeLists.txt` beside the existing block validation test:

```cmake
add_executable(r5900_ir_direct_transfer_validation_tests
  tests/r5900_ir_direct_transfer_validation_tests.cpp
)
target_link_libraries(r5900_ir_direct_transfer_validation_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_direct_transfer_validation_tests
  COMMAND r5900_ir_direct_transfer_validation_tests)
```

Create the test with helpers `nop(pc)`, `gpr(index)`, `direct_jump(pc,target,delay)`, and `direct_call(pc,target,delay)`. The test must assert this matrix:

```cpp
const auto jump = direct_jump(0x00106000u, 0x00106100u, nop(0x00106004u));
expect(validate_r5900_ir_block(jump).ok(), "valid DirectJump must validate");

const auto call = direct_call(0x00106200u, 0x00106300u, nop(0x00106204u));
expect(validate_r5900_ir_block(call).ok(), "valid DirectCall must validate");

{
    auto invalid = jump;
    invalid.terminator.target_pc |= 2u;
    expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
           "unaligned DirectJump target must fail");
}
{
    auto invalid = jump;
    invalid.terminator.link_pc = 0x00106008u;
    expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
           "DirectJump link state must fail");
}
{
    auto invalid = call;
    invalid.terminator.link_pc += 4u;
    expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
           "DirectCall link must equal guest PC plus eight");
}
{
    auto invalid = call;
    invalid.terminator.inputs = {gpr(1u)};
    expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
           "DirectCall inputs must be empty");
}
{
    auto invalid = jump;
    invalid.terminator.taken_pc = 0x00106080u;
    expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
           "DirectJump branch fields must be empty");
}
{
    auto invalid = call;
    invalid.terminator.delay_slot.clear();
    expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
           "DirectCall requires one delay slot");
}
{
    auto invalid = jump;
    invalid.terminator.delay_slot.push_back(nop(0x00106008u));
    expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::MalformedInstruction,
           "DirectJump rejects multiple delay-slot IR instructions");
}
{
    auto invalid = call;
    invalid.terminator.delay_slot.front().opcode = static_cast<R5900IrOpcode>(0xffu);
    expect(validate_r5900_ir_block(invalid).error == R5900IrValidationError::UnsupportedOpcode,
           "invalid direct-transfer delay IR must propagate validation error");
}
```

`direct_call` must set `link_pc = pc + 8u`; both builders set `taken_pc = 0`, `fallthrough_pc = 0`, and exactly one delay instruction.

- [ ] **Step 2: Run the focused test and capture RED**

Run on Windows/MSVC:

```powershell
cmake -S . -B build -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target r5900_ir_direct_transfer_validation_tests
```

Expected: compile failure because `DirectJump`, `DirectCall`, `target_pc`, and `link_pc` do not exist yet. Record the workflow/run evidence before production changes.

- [ ] **Step 3: Extend the IR type**

In `src/recompiler/r5900_ir.h` add:

```cpp
enum class R5900IrTerminatorKind {
    Fallthrough = 0,
    BranchEqual64,
    DirectJump,
    DirectCall,
};
```

and append to `R5900IrTerminator` before `delay_slot`:

```cpp
std::uint32_t target_pc{};
std::uint32_t link_pc{};
```

Do not add a generic link operand or synthetic register-write opcode.

- [ ] **Step 4: Implement kind-specific block validation**

Refactor `validate_r5900_ir_block` so `guest_pc` is always aligned, then validate only active PCs plus forbidden inactive fields. Use these exact shape rules:

```cpp
case R5900IrTerminatorKind::Fallthrough:
    // inputs/delay empty, taken_pc/target_pc/link_pc zero,
    // fallthrough_pc aligned.

case R5900IrTerminatorKind::BranchEqual64:
    // existing 2-GPR + one-delay contract,
    // taken/fallthrough aligned, target_pc/link_pc zero.

case R5900IrTerminatorKind::DirectJump:
    // inputs empty; taken/fallthrough/link zero;
    // target aligned; exactly one validated delay instruction.

case R5900IrTerminatorKind::DirectCall:
    // inputs empty; taken/fallthrough zero;
    // target/link aligned; link_pc == uint32_t(guest_pc + 8u);
    // exactly one validated delay instruction.
```

Use `R5900IrValidationError::MalformedInstruction` for shape/alignment/link mismatches and preserve `UnsupportedOpcode` for unknown terminator kinds.

- [ ] **Step 5: Run validation tests GREEN plus regressions**

```powershell
cmake --build build --config Release --target r5900_ir_direct_transfer_validation_tests r5900_ir_block_validation_tests
ctest --test-dir build -C Release -R "r5900_ir_(direct_transfer_validation|block_validation)_tests" --output-on-failure
```

Expected: both tests PASS.

- [ ] **Step 6: Commit Task 1**

```bash
git add CMakeLists.txt src/recompiler/r5900_ir.h src/recompiler/r5900_ir_validation.cpp tests/r5900_ir_direct_transfer_validation_tests.cpp
git commit -m "feat: model R5900 direct transfer terminators"
```

---

### Task 2: Reference `J` / `JAL` execution semantics

**Files:**
- Modify: `src/recompiler/r5900_ir_executor.cpp`
- Create: `tests/r5900_ir_direct_transfer_executor_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 `DirectJump`, `DirectCall`, `target_pc`, `link_pc`; existing `execute_ir_sequence` and `R5900IrExecutionContext`.
- Produces: semantic oracle used verbatim by Task 3 differential tests.

- [ ] **Step 1: Register and write the reference-executor tests**

Add:

```cmake
add_executable(r5900_ir_direct_transfer_executor_tests
  tests/r5900_ir_direct_transfer_executor_tests.cpp
)
target_link_libraries(r5900_ir_direct_transfer_executor_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_direct_transfer_executor_tests
  COMMAND r5900_ir_direct_transfer_executor_tests)
```

Test these exact cases using the existing semantic `AddWordSignExtend` helper for delay instructions:

```cpp
// J: delay executes once, target returned, r31 untouched.
state.gpr[31] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
auto jump = direct_jump(0x00107000u, 0x00107100u,
                        addiu(7u, 7u, 1, 0x00107004u));

// JAL: link visible to delay, low64 becomes PC+8, high64 preserved.
state.gpr[31] = {0xdeadbeefdeadbeefull, 0x0123456789abcdefull};
auto call = direct_call(0x00107200u, 0x00107300u,
                        addiu(23u, 31u, 0, 0x00107204u));
expect(result.next_pc == 0x00107300u, "JAL must return direct target");
expect(state.gpr[31].low64 == 0x00107208u, "JAL link mismatch");
expect(state.gpr[31].high64 == 0x0123456789abcdefull, "JAL must preserve r31 high64");
expect(state.gpr[23].low64 == 0x00107208u, "delay must observe new JAL link");

// Delay write to r31 occurs after link and wins on low64.
auto overwrite = direct_call(0x00107400u, 0x00107500u,
                             addiu(31u, 0u, 9, 0x00107404u));

// Valid body Store128 failure prevents JAL link and delay.
R5900IrExecutionContext context{};
context.state = &state; // memory callback intentionally absent
block.body = {store128(2u, 3u, 0, 0x00107600u)};
block.terminator = direct_call_terminator(0x00107604u, 0x00107700u,
                                         addiu(24u, 0u, 1, 0x00107608u));
```

For the body failure, assert `MemoryAccessFailure`, original `r31` unchanged, delay destination unchanged, and `memory_fault.guest_pc == 0x00107600u`.

- [ ] **Step 2: Run RED**

```powershell
cmake --build build --config Release --target r5900_ir_direct_transfer_executor_tests
ctest --test-dir build -C Release -R r5900_ir_direct_transfer_executor_tests --output-on-failure
```

Expected: test executable builds after Task 1 but fails because `execute_r5900_ir_block` returns `UnsupportedOpcode` for the new terminators.

- [ ] **Step 3: Implement reference terminator execution**

In `execute_r5900_ir_block`, add:

```cpp
case R5900IrTerminatorKind::DirectJump: {
    const auto delay_result = execute_ir_sequence(block.terminator.delay_slot, context);
    if (!delay_result.ok()) {
        return map_block_execution_failure(delay_result);
    }
    return {R5900IrExecutionError::None, {}, block.terminator.target_pc};
}

case R5900IrTerminatorKind::DirectCall: {
    state.gpr[31].low64 = static_cast<std::uint64_t>(block.terminator.link_pc);
    normalize_zero(state);
    const auto delay_result = execute_ir_sequence(block.terminator.delay_slot, context);
    if (!delay_result.ok()) {
        return map_block_execution_failure(delay_result);
    }
    return {R5900IrExecutionError::None, {}, block.terminator.target_pc};
}
```

Do not write `state.gpr[31].high64`. Do not move link creation before body execution or block validation.

- [ ] **Step 4: Run reference GREEN and existing block-executor regressions**

```powershell
cmake --build build --config Release --target r5900_ir_direct_transfer_executor_tests r5900_ir_block_executor_tests r5900_ir_store128_executor_tests
ctest --test-dir build -C Release -R "r5900_ir_(direct_transfer_executor|block_executor|store128_executor)_tests" --output-on-failure
```

Expected: all selected tests PASS.

- [ ] **Step 5: Commit Task 2**

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
- Consumes: Task 1 terminators and Task 2 reference semantics.
- Produces: `compile_r5900_ir_x64(const R5900IrBlock&)` support for `DirectJump` and `DirectCall` with no public API change.

- [ ] **Step 1: Register a Windows-only differential test target**

Inside `if(WIN32)` add:

```cmake
add_executable(r5900_x64_direct_transfer_windows_tests
  tests/r5900_x64_direct_transfer_windows_tests.cpp
)
target_link_libraries(r5900_x64_direct_transfer_windows_tests PRIVATE b3r_recompiler_x64)
add_test(NAME r5900_x64_direct_transfer_windows_tests
  COMMAND r5900_x64_direct_transfer_windows_tests)
```

- [ ] **Step 2: Write native/reference differential RED tests**

For every case, copy the same initial state into `reference_state` and `native_state`, execute reference with `execute_r5900_ir_block`, compile with `compile_r5900_ir_x64`, execute native through `R5900IrExecutionContext`, then compare full architectural state and `next_pc`.

Required cases:

```text
1. DirectJump + ADDIU delay -> target returned, delay exactly once, r31 unchanged.
2. DirectCall + delay reading r31 -> PC+8 observed, high64 sentinel preserved.
3. DirectCall + delay writing r31 -> delay result wins after link.
4. DirectJump with Store128 in delay + successful callback -> helper frame works and target returned.
5. DirectCall with Store128 in delay + failing callback -> both paths return MemoryAccessFailure; JAL link remains committed; identical memory_fault provenance.
```

Use a test callback:

```cpp
struct MemoryProbe {
    bool succeed{true};
    std::uint32_t address{};
    std::uint64_t low64{};
    std::uint64_t high64{};
};

bool write128(void* user, std::uint32_t address,
              std::uint64_t low64, std::uint64_t high64) noexcept {
    auto& probe = *static_cast<MemoryProbe*>(user);
    probe.address = address;
    probe.low64 = low64;
    probe.high64 = high64;
    return probe.succeed;
}
```

- [ ] **Step 3: Run RED**

```powershell
cmake --build build --config Release --target r5900_x64_direct_transfer_windows_tests
ctest --test-dir build -C Release -R r5900_x64_direct_transfer_windows_tests --output-on-failure
```

Expected: compile succeeds but runtime/compile assertions fail because the backend reports unsupported block terminator.

- [ ] **Step 4: Add native direct-transfer emission**

Add a helper near `compile_branch_equal_code`:

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

Then add both cases in `compile_r5900_ir_x64(const R5900IrBlock&)`:

```cpp
case R5900IrTerminatorKind::DirectJump:
case R5900IrTerminatorKind::DirectCall:
    pending = compile_direct_transfer_code(block);
    break;
```

The `emit_store128` failure epilogue already returns immediately. Because the link is emitted before the delay sequence, a failing `JAL` delay store must retain the link in both reference and native state.

- [ ] **Step 5: Run native GREEN plus backend regressions**

```powershell
cmake --build build --config Release --target r5900_x64_direct_transfer_windows_tests r5900_x64_backend_windows_tests r5900_x64_store128_windows_tests
ctest --test-dir build -C Release -R "r5900_x64_(direct_transfer|backend|store128)_windows_tests" --output-on-failure
```

Expected: all selected tests PASS with no access violation and no new ABI warning/error.

- [ ] **Step 6: Commit Task 3**

```bash
git add CMakeLists.txt src/recompiler/windows/r5900_x64_backend.cpp tests/r5900_x64_direct_transfer_windows_tests.cpp
git commit -m "feat: emit native R5900 direct jump and call"
```

---

### Task 4: Dispatcher recognition, cache identity, and continuation

**Files:**
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Create: `tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: analyzer `R5900BlockEndKind::DirectJump`/`DirectCall`, decoder `direct_target`, Tasks 1-3 IR/backend.
- Produces: native dispatcher execution of analyzer-confirmed `J` and `JAL`; exact cache includes body + terminator + delay word.

- [ ] **Step 1: Register and write focused dispatcher RED tests**

Add under `if(WIN32)`:

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

Use the existing synthetic ELF mapping pattern plus helpers:

```cpp
constexpr std::uint32_t j_type(std::uint8_t op, std::uint32_t target) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           ((target >> 2u) & 0x03ffffffu);
}
```

Required cases:

```text
A. J at entry, ADDIU delay, target contains unsupported JR:
   - J and delay execute/count;
   - dispatcher continues to target;
   - stops ControlFlow at JR;
   - no linear fallthrough poison executes.

B. JAL at entry, delay copies r31, target contains unsupported JR:
   - r31.low64 = call PC+8;
   - r31.high64 sentinel preserved;
   - delay sees link;
   - target reached.

C. J/JAL exact cache hit:
   - first run cache_misses increments;
   - second identical run cache_hits increments;
   - state/result match expected direct target.

D. mutate the terminator word or delay word with Ps2MemoryMap::write_u32:
   - next run does not exact-hit stale block;
   - recompilations increments;
   - changed semantics are observed.

E. SQ in J or JAL delay slot:
   - `LoweringFailure` at delay PC;
   - zero blocks/instructions for a transfer-at-entry fixture;
   - no guest-memory mutation.

F. JR/JALR remain unsupported ControlFlow boundaries and their delay slots are not executed.
```

- [ ] **Step 2: Run RED**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_direct_transfer_windows_tests
ctest --test-dir build -C Release -R r5900_block_dispatcher_direct_transfer_windows_tests --output-on-failure
```

Expected: `J`/`JAL` cases stop at their own PCs with `ControlFlow`, proving dispatcher support is absent.

- [ ] **Step 3: Recognize supported direct terminators**

In `R5900BlockDispatcher::run`, derive explicit candidates beside `has_supported_beq`:

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

Select the final transfer site as a terminator, not body. Generalize the existing BEQ body loop so the final supported BEQ/J/JAL is skipped while preceding eligible instructions remain body sites. Unsupported `JR/JALR` must continue setting `boundary_reason = ControlFlow`.

- [ ] **Step 4: Include transfer and delay words in cache identity**

Reserve and append `guest_words` for supported direct transfers exactly like BEQ:

```text
body words in order
J or JAL word
delay-slot word
```

Require both mapped/readable. For cached replacements set:

```cpp
replacement.end_pc_exclusive = transfer_site->pc + 8u;
replacement.guest_instruction_count = guest_words.size();
```

Do not set `boundary_reason` for a supported `J`/`JAL`, because native `next_pc` must drive the next loop iteration.

- [ ] **Step 5: Build the direct-transfer IR terminator**

For supported direct transfer:

```cpp
const auto target = transfer_site->decoded.direct_target(transfer_site->pc);
if (!target.has_value()) {
    // AnalysisFailure at transfer PC with deterministic message.
}

ir_block.terminator.guest_pc = transfer_site->pc;
ir_block.terminator.guest_raw = transfer_site->decoded.raw;
ir_block.terminator.kind = has_supported_j
    ? R5900IrTerminatorKind::DirectJump
    : R5900IrTerminatorKind::DirectCall;
ir_block.terminator.target_pc = *target;
ir_block.terminator.link_pc = has_supported_jal ? transfer_site->pc + 8u : 0u;
```

Require analyzer delay slot. Reject `delay.decoded.instruction == R5900Instruction::Sq` with `LoweringFailure` and a message naming `J/JAL delay slot outside dispatcher v0 scope`. Otherwise lower and require exactly one IR instruction, then assign `terminator.delay_slot`.

- [ ] **Step 6: Run dispatcher GREEN and existing dispatcher regressions**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_direct_transfer_windows_tests r5900_block_dispatcher_windows_tests r5900_block_dispatcher_store128_windows_tests
ctest --test-dir build -C Release -R "r5900_block_dispatcher_(direct_transfer|store128)?_?windows_tests" --output-on-failure
```

If the regex misses the generic target, run the three executables individually with `ctest -R` exact names. Expected: all three PASS.

- [ ] **Step 7: Commit Task 4**

```bash
git add CMakeLists.txt src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp
git commit -m "feat: dispatch R5900 J and JAL blocks"
```

---

### Task 5: Startup E2E through `J` and `JAL`

**Files:**
- Modify: `tests/r5900_block_dispatcher_startup_windows_tests.cpp`

**Interfaces:**
- Consumes: complete direct-transfer stack from Tasks 1-4.
- Produces: deterministic synthetic startup proof: 5 blocks / 87 instructions / stop at `JR 0x001001a4`.

- [ ] **Step 1: Replace the old post-SQ sentinel with the approved mapped layout**

Add constants:

```cpp
constexpr std::uint32_t kDirectJumpPc = 0x00100164u;
constexpr std::uint32_t kDirectJumpTarget = 0x00100180u;
constexpr std::uint32_t kDirectCallPc = 0x00100180u;
constexpr std::uint32_t kDirectCallTarget = 0x001001a0u;
constexpr std::uint32_t kIndirectReturnPc = 0x001001a4u;
constexpr std::uint32_t kExpectedLinkPc = 0x00100188u;
```

Add a `j_type(op,target)` encoder. Extend `make_synthetic_startup_words` after the existing `SQ` with the exact fixture:

```text
0x00100164  J     0x00100180
0x00100168  ADDIU r22,r0,0x33
0x0010016c  ADDIU r25,r0,1   ; poison
0x00100170  ADDIU r26,r0,1   ; poison
0x00100174  ADDIU r27,r0,1   ; poison
0x00100178  ADDIU r28,r0,1   ; poison
0x0010017c  ADDIU r30,r0,1   ; poison
0x00100180  JAL   0x001001a0
0x00100184  ADDIU r23,r31,0
0x00100188  ADDIU r25,r0,2   ; poison
0x0010018c  ADDIU r26,r0,2   ; poison
0x00100190  ADDIU r27,r0,2   ; poison
0x00100194  ADDIU r28,r0,2   ; poison
0x00100198  ADDIU r30,r0,2   ; poison
0x0010019c  NOP              ; poison/fallthrough guard
0x001001a0  ADDIU r24,r0,0x55
0x001001a4  JR    r31
0x001001a8  NOP
```

Use `r_type(31u, 0u, 0u, 0u, 0x08u)` for `JR r31`.

- [ ] **Step 2: Update synthetic expected result before production rerun**

The test must assert:

```cpp
expect(result.reason == R5900DispatchStopReason::ControlFlow,
       "startup must stop at unsupported JR boundary");
expect(result.next_pc == kIndirectReturnPc,
       "startup next PC must be JR boundary");
expect(result.blocks_executed == 5u,
       "startup must complete five native blocks through J/JAL");
expect(result.instructions_executed == 87u,
       "startup must complete exactly 87 guest instructions");
expect(state.gpr[22].low64 == 0x33u, "J delay slot must execute");
expect(state.gpr[23].low64 == kExpectedLinkPc,
       "JAL delay slot must observe link");
expect(state.gpr[24].low64 == 0x55u, "callee prefix must execute");
expect(state.gpr[31].low64 == kExpectedLinkPc,
       "JAL must set r31 low64 to PC+8");
expect(state.gpr[31].high64 == 0u,
       "startup fixture keeps r31 high64 zero after earlier PADDUW clear");
```

Assert poison registers `r25-r28` and `r30` remain zero after the earlier startup initialization path. Preserve all existing SQ target/boundary-byte and architectural-state assertions.

- [ ] **Step 3: Run startup E2E**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_startup_windows_tests
ctest --test-dir build -C Release -R r5900_block_dispatcher_startup_windows_tests --output-on-failure
```

Expected: PASS with 5 blocks, 87 instructions, `next_pc = 0x001001a4`, and zeroed `0x004e2680..0x004e268f`.

- [ ] **Step 4: Keep external legal-ELF mode conservative**

Do not assert that the real ELF contains `J`/`JAL` at the synthetic PCs. Keep these existing external conditions:

```text
instructions_executed >= 82
next_pc != 0x00100160
SQ target mapped and zeroed
later stop PC/block count dynamically reported
```

If output wording is updated, retain the distinction that only `SQ` is externally required by the harness until a real legal ELF run proves more.

- [ ] **Step 5: Commit Task 5**

```bash
git add tests/r5900_block_dispatcher_startup_windows_tests.cpp
git commit -m "test: advance startup through R5900 J and JAL"
```

---

### Task 6: Full verification, documentation, and milestone evidence

**Files:**
- Modify: `README.md`
- Modify: `PROGRESS.md`

**Interfaces:**
- Consumes: all completed tasks and GitHub Actions evidence.
- Produces: `CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION` documentation with no external-validation overclaim.

- [ ] **Step 1: Run complete Windows CI/test suite from feature HEAD**

Run the repository's normal Windows workflow on `feature/r5900-direct-jump-call-v0`. Required successful steps:

```text
Configure
Build
Test
Frame pacing telemetry
Pacing probe smoke
Stage/Validate analyzer package
Stage/Validate pacing probe package
```

Expected CTest total is the previous 39 tests plus four newly registered targets = **43/43**, assuming no unrelated test target is added concurrently. If the repository changes concurrently, report the actual total and require 0 failures rather than forcing 43.

- [ ] **Step 2: Verify the four milestone-focused tests explicitly in CI logs**

Confirm PASS for:

```text
r5900_ir_direct_transfer_validation_tests
r5900_ir_direct_transfer_executor_tests
r5900_x64_direct_transfer_windows_tests
r5900_block_dispatcher_direct_transfer_windows_tests
r5900_block_dispatcher_startup_windows_tests
```

Also confirm no regression in:

```text
r5900_ir_block_validation_tests
r5900_ir_block_executor_tests
r5900_x64_store128_windows_tests
r5900_block_dispatcher_store128_windows_tests
```

- [ ] **Step 3: Update README milestone text**

Document these facts exactly:

```text
R5900 direct J/JAL + delay slot v0: CI_VALIDATED
- J and JAL are native typed terminators.
- JAL writes zero-extended PC+8 to GPR31.low64 before the delay slot and preserves high64.
- Synthetic startup reaches unsupported JR at 0x001001a4 after 5 blocks / 87 instructions.
- JR/JALR remain unsupported.
- SQ in J/JAL delay slots remains outside dispatcher v0.
- The legal external ELF has not yet been run through this expanded native path.
- The game does not boot yet.
```

- [ ] **Step 4: Update PROGRESS with exact CI evidence**

Record feature HEAD SHA, workflow run/job IDs, MSVC version, CTest pass count, the five focused target results, and 120 Hz telemetry. Set startup status to:

```text
CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION
```

The next implementation gate should name `JR/JALR` or the first externally observed post-`SQ` blocker after the user runs the legal ELF; do not invent the real next opcode.

- [ ] **Step 5: Re-run CI after documentation commits**

Require a fresh green workflow on the exact final feature HEAD. Verify 0 test failures and all packaging/pacing checks green.

- [ ] **Step 6: Commit documentation**

```bash
git add README.md PROGRESS.md
git commit -m "docs: record R5900 direct J/JAL milestone"
```

- [ ] **Step 7: Perform final review before integration**

Invoke `superpowers:requesting-code-review`, then `superpowers:verification-before-completion`. Review must specifically check:

```text
- link-before-delay ordering in reference and native paths
- GPR31.high64 preservation
- no body lowering of J/JAL
- helper-frame failure epilogue correctness for direct-transfer delay IR
- exact cache word coverage includes terminator+delay
- supported J/JAL does not leave stale boundary_reason
- JR/JALR remain unexecuted
- synthetic poison regions remain untouched
- no claim of external validation or game boot
```

Only after review and fresh CI are green should the branch be offered for fast-forward integration back into `feature/r5900-beq-delay-slot-v0`.
