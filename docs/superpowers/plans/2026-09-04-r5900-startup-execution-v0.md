# R5900 Startup Execution v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute the first 74-instruction straight-line Burnout 3 EE startup prefix natively on Windows from guest PC `0x00100008` and stop before the first `BEQ` at `0x00100130`.

**Architecture:** Extend the existing typed R5900 IR and single `R5900IrExecutionState` so special-register, packed-MMI, and narrow COP1 state are explicit and shared by the reference executor and Windows x86-64 backend. Preserve the existing generated-function ABI (`void generated(R5900IrExecutionState*)`), keep `gpr[32]` at offset zero, validate every IR instruction before execution/publication, and expand dispatcher eligibility only after reference and native semantics are green.

**Tech Stack:** C++20, CMake, Visual Studio 2022/MSVC 19.44, Windows x64 ABI, hand-emitted x86-64/SSE machine code, GitHub Actions Windows Server 2022.

**Spec:** `docs/superpowers/specs/2026-09-04-r5900-startup-execution-v0-design.md`

## Global Constraints

- The execution state remains a single `R5900IrExecutionState`; `gpr[32]` must remain the first field and `offsetof(R5900IrExecutionState, gpr) == 0`.
- `sizeof(R5900IrGprValue)` must remain exactly 16 bytes.
- New x64 state displacements must come from `offsetof(...)`; do not hand-maintain magic offsets for appended state fields.
- FPRs and the floating-point accumulator are stored as raw 32-bit IEEE-754 bit patterns.
- `ADDA.S` v0 covers finite normal values and zero cases used by the observed startup; it does not claim EE-exact NaN, denormal, overflow, underflow, exception-flag, or rounding-mode behavior and must not modify `fcr31`.
- `CTC1` v0 supports only control register 31 and does not translate `FCR31` into Windows `MXCSR`.
- `SYNC` is the only newly supported instruction intentionally represented as a semantic no-op.
- The dispatcher must continue to stop before branches/jumps, traps/syscalls, unsupported instructions, and all delay slots.
- No proprietary Burnout 3 executable bytes, dumps, assets, symbols, or derived binary blobs may be committed. Versioned tests use synthetic instruction encodings only.
- Existing RW -> RX executable-memory protection, instruction-cache flush, RAII block ownership, stale-cache validation, and per-block atomicity remain unchanged.
- Implementation follows RED -> GREEN TDD. Every task ends in a focused commit and must leave the targeted test set green.

---

## File Structure Map

- `src/recompiler/r5900_ir.h` — typed IR destinations/operands/opcodes and write modes.
- `src/recompiler/r5900_ir.cpp` — decoder-to-IR lowering; all-or-nothing guest-instruction lowering and GPR-zero discard policy.
- `src/recompiler/r5900_ir_validation.cpp` — structural authority for every IR opcode/destination/operand combination.
- `src/recompiler/r5900_ir_executor.h` — shared modeled EE execution state and executor result types.
- `src/recompiler/r5900_ir_executor.cpp` — deterministic semantic reference implementation used as the differential oracle.
- `src/recompiler/windows/r5900_x64_backend.cpp` — hand-emitted Windows x86-64/SSE implementation using `R5900IrExecutionState*` in `RCX`.
- `src/recompiler/windows/r5900_block_dispatcher.cpp` — supported-instruction eligibility and analyzer -> lowering -> native execution bridge.
- `tests/r5900_ir_tests.cpp` — decoder -> IR lowering contract tests.
- `tests/r5900_ir_validation_tests.cpp` — positive/negative structural validation tests.
- `tests/r5900_ir_executor_tests.cpp` — reference semantic tests and decoder -> IR -> executor integration.
- `tests/r5900_x64_backend_windows_tests.cpp` — ABI/layout assertions, native behavior, and reference-vs-native differential tests.
- `tests/r5900_block_dispatcher_windows_tests.cpp` — synthetic ELF/basic-block/dispatcher integration and optional externally supplied real-ELF validation.
- `README.md` — user-facing milestone statement and limitations.
- `docs/PROGRESS.md` — authoritative validation status and CI/external evidence.

---

### Task 1: Generalize the IR model and append architectural state without changing current semantics

**Files:**
- Modify: `src/recompiler/r5900_ir.h`
- Modify: `src/recompiler/r5900_ir.cpp`
- Modify: `src/recompiler/r5900_ir_validation.cpp`
- Modify: `src/recompiler/r5900_ir_executor.h`
- Modify: `tests/r5900_ir_tests.cpp`
- Modify: `tests/r5900_ir_validation_tests.cpp`
- Modify: `tests/r5900_ir_executor_tests.cpp`
- Modify: `tests/r5900_x64_backend_windows_tests.cpp`

**Interfaces:**
- Consumes: existing `R5900IrInstruction`, `R5900IrOperand`, `R5900IrExecutionState`, `validate_r5900_ir_instruction`, and current NOP/ADDU/ADDIU/ORI behavior.
- Produces: `R5900IrDestinationKind`, `R5900IrDestination`, `R5900IrOperandKind::Fpr`, `R5900IrGprWriteMode::Full128`, appended execution-state fields, and the semantic opcode names required by Tasks 2-6.

- [ ] **Step 1: Write the failing model/ABI tests**

In `tests/r5900_x64_backend_windows_tests.cpp`, add `<cstddef>` and compile-time checks immediately after the existing move/copy assertions:

```cpp
static_assert(std::is_standard_layout_v<R5900IrExecutionState>);
static_assert(sizeof(R5900IrGprValue) == 16u);
static_assert(offsetof(R5900IrExecutionState, gpr) == 0u);
static_assert(offsetof(R5900IrExecutionState, hi) >= sizeof(R5900IrGprValue) * 32u);
static_assert(offsetof(R5900IrExecutionState, fpr) > offsetof(R5900IrExecutionState, sa));
static_assert(offsetof(R5900IrExecutionState, fp_acc) > offsetof(R5900IrExecutionState, fcr31));
```

Update the test helpers in `r5900_ir_tests.cpp`, `r5900_ir_validation_tests.cpp`, `r5900_ir_executor_tests.cpp`, and `r5900_x64_backend_windows_tests.cpp` so a current GPR destination is constructed as:

```cpp
R5900IrDestination gpr_destination(std::uint8_t index) {
    return {R5900IrDestinationKind::Gpr, index};
}
```

and assertions on existing operations also require `destination->kind == R5900IrDestinationKind::Gpr`.

Add a compile-only model assertion in `tests/r5900_ir_validation_tests.cpp`:

```cpp
R5900IrInstruction model_probe{};
model_probe.destination = R5900IrDestination{R5900IrDestinationKind::Fpr, 3u};
model_probe.write_mode = R5900IrGprWriteMode::Full128;
R5900IrOperand fpr_probe{};
fpr_probe.kind = R5900IrOperandKind::Fpr;
fpr_probe.gpr_index = 4u;
```

- [ ] **Step 2: Run the focused tests and verify RED**

Run:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target r5900_ir_tests r5900_ir_validation_tests r5900_ir_executor_tests r5900_x64_backend_windows_tests --parallel
```

Expected: compile failure because `R5900IrDestinationKind`, `R5900IrDestination`, `Full128`, `Fpr`, and the appended state fields do not exist yet.

- [ ] **Step 3: Add the typed IR model and append state**

In `src/recompiler/r5900_ir.h`, replace the GPR-only destination type with:

```cpp
enum class R5900IrDestinationKind {
    Gpr = 0,
    Hi,
    Lo,
    Hi1,
    Lo1,
    Sa,
    Fpr,
    Fcr31,
    FpAccumulator,
};

struct R5900IrDestination {
    R5900IrDestinationKind kind{R5900IrDestinationKind::Gpr};
    std::uint8_t index{};
};
```

Extend the operand/write/opcode enums exactly as follows:

```cpp
enum class R5900IrOperandKind {
    Gpr = 0,
    Fpr,
    Immediate,
};

enum class R5900IrGprWriteMode {
    None = 0,
    Low64PreserveUpper64,
    Full128,
};

enum class R5900IrOpcode {
    Nop = 0,
    AddWordSignExtend,
    Or64,
    And64,
    LoadUpperImmediateSignExtend,
    AddPackedU32Saturate128,
    MoveGprLow64,
    ComputeMtsah,
    MoveBits32,
    AddF32ToAccumulator,
};
```

Change the instruction destination member to:

```cpp
std::optional<R5900IrDestination> destination{};
```

In `src/recompiler/r5900_ir_executor.h`, append fields after `gpr` without reordering it:

```cpp
struct R5900IrExecutionState {
    std::array<R5900IrGprValue, 32> gpr{};
    std::uint64_t hi{};
    std::uint64_t lo{};
    std::uint64_t hi1{};
    std::uint64_t lo1{};
    std::uint32_t sa{};
    std::array<std::uint32_t, 32> fpr{};
    std::uint32_t fcr31{};
    std::uint32_t fp_acc{};
};
```

Adapt the existing lowering helper in `r5900_ir.cpp`:

```cpp
void set_low64_destination(R5900IrInstruction& ir, std::uint8_t index) {
    ir.destination = R5900IrDestination{R5900IrDestinationKind::Gpr, index};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
}
```

In `r5900_ir_validation.cpp`, preserve current semantics by requiring the old write opcodes to target a GPR:

```cpp
if (ir.destination->kind != R5900IrDestinationKind::Gpr) {
    return failure(R5900IrValidationError::MalformedInstruction,
                   index,
                   ir.guest_pc,
                   "expected GPR destination");
}
```

Update all existing test helper assignments from `R5900IrRegister{n}` to `R5900IrDestination{R5900IrDestinationKind::Gpr, n}`. Do not add new runtime semantics in this task.

- [ ] **Step 4: Run the current semantic suite and verify GREEN**

Run:

```powershell
cmake --build build --config Release --target r5900_ir_tests r5900_ir_validation_tests r5900_ir_executor_tests r5900_x64_backend_windows_tests --parallel
ctest --test-dir build -C Release -R "^(r5900_ir_tests|r5900_ir_validation_tests|r5900_ir_executor_tests|r5900_x64_backend_windows_tests)$" --output-on-failure
```

Expected: all four tests PASS; existing NOP/ADDU/ADDIU/ORI behavior remains unchanged.

- [ ] **Step 5: Commit the model/ABI change**

```bash
git add src/recompiler/r5900_ir.h src/recompiler/r5900_ir.cpp src/recompiler/r5900_ir_validation.cpp src/recompiler/r5900_ir_executor.h tests/r5900_ir_tests.cpp tests/r5900_ir_validation_tests.cpp tests/r5900_ir_executor_tests.cpp tests/r5900_x64_backend_windows_tests.cpp
git commit -m "refactor: generalize R5900 IR execution state"
```

---

### Task 2: Lower and validate the integer, special-register, MMI, and SYNC startup subset

**Files:**
- Modify: `src/recompiler/r5900_ir.cpp`
- Modify: `src/recompiler/r5900_ir_validation.cpp`
- Modify: `tests/r5900_ir_tests.cpp`
- Modify: `tests/r5900_ir_validation_tests.cpp`

**Interfaces:**
- Consumes: typed destination/operand model from Task 1 and decoder identities already present for `ANDI`, `LUI`, `MTHI`, `MTLO`, `MTHI1`, `MTLO1`, `MTSAH`, `PADDUW`, and `SYNC`.
- Produces: structurally validated semantic IR for all non-COP1 startup instructions needed by the dispatcher.

- [ ] **Step 1: Add synthetic encoding helpers and failing lowering tests**

Add to `tests/r5900_ir_tests.cpp`:

```cpp
constexpr std::uint32_t mmi_type(std::uint8_t rs,
                                 std::uint8_t rt,
                                 std::uint8_t rd,
                                 std::uint8_t sa,
                                 std::uint8_t funct) {
    return (0x1cu << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) |
           funct;
}
```

Add RED cases that require these exact mappings:

```cpp
// ANDI r2,r1,0x00ff
expect_lower(i_type(0x0c, 1, 2, 0x00ff), R5900IrOpcode::And64,
             R5900IrDestinationKind::Gpr, 2u);

// LUI r2,0x8040
expect_lower(i_type(0x0f, 0, 2, 0x8040), R5900IrOpcode::LoadUpperImmediateSignExtend,
             R5900IrDestinationKind::Gpr, 2u);

expect_lower(r_type(7, 0, 0, 0, 0x11), R5900IrOpcode::MoveGprLow64,
             R5900IrDestinationKind::Hi, 0u);
expect_lower(r_type(8, 0, 0, 0, 0x13), R5900IrOpcode::MoveGprLow64,
             R5900IrDestinationKind::Lo, 0u);
expect_lower(mmi_type(9, 0, 0, 0, 0x11), R5900IrOpcode::MoveGprLow64,
             R5900IrDestinationKind::Hi1, 0u);
expect_lower(mmi_type(10, 0, 0, 0, 0x13), R5900IrOpcode::MoveGprLow64,
             R5900IrDestinationKind::Lo1, 0u);
expect_lower(i_type(0x01, 11, 0x19, 5), R5900IrOpcode::ComputeMtsah,
             R5900IrDestinationKind::Sa, 0u);
expect_lower(mmi_type(12, 13, 14, 0x10, 0x28), R5900IrOpcode::AddPackedU32Saturate128,
             R5900IrDestinationKind::Gpr, 14u);
```

Also assert:

```cpp
const auto sync = lower_r5900_instruction(decode_r5900(r_type(0, 0, 0, 16, 0x0f)), 0x00100040u);
expect(sync.ok() && sync.instructions.size() == 1u &&
       sync.instructions[0].opcode == R5900IrOpcode::Nop,
       "SYNC must lower to semantic Nop");
```

For `ANDI`, verify the IR immediate is exactly `0x00ff`. For `LUI`, verify the IR input retains the original 16-bit immediate `0x8040`. For `MTSAH`, verify inputs are `Gpr(11), Immediate(5)`. For `PADDUW`, verify two GPR inputs and `write_mode == Full128`. Add GPR-zero cases proving `ANDI`, `LUI`, and `PADDUW` targeting GPR0 lower to provenance-carrying `Nop`.

- [ ] **Step 2: Run lowering tests and verify RED**

```powershell
cmake --build build --config Release --target r5900_ir_tests --parallel
ctest --test-dir build -C Release -R "^r5900_ir_tests$" --output-on-failure
```

Expected: FAIL because the new decoder identities still return `UnsupportedInstruction` from lowering.

- [ ] **Step 3: Implement minimal lowering helpers and cases**

Add helpers in `src/recompiler/r5900_ir.cpp`:

```cpp
R5900IrDestination destination(R5900IrDestinationKind kind, std::uint8_t index = 0u) {
    return {kind, index};
}

void set_destination(R5900IrInstruction& ir,
                     R5900IrDestinationKind kind,
                     std::uint8_t index = 0u,
                     R5900IrGprWriteMode mode = R5900IrGprWriteMode::None) {
    ir.destination = destination(kind, index);
    ir.write_mode = mode;
}
```

Add switch cases with these exact shapes:

```cpp
case R5900Instruction::Andi: {
    if (decoded.rt == 0u) return discarded_gpr_zero_write(decoded, guest_pc);
    auto ir = base_instruction(decoded, guest_pc, R5900IrOpcode::And64);
    set_destination(ir, R5900IrDestinationKind::Gpr, decoded.rt,
                    R5900IrGprWriteMode::Low64PreserveUpper64);
    ir.inputs = {gpr(decoded.rs), immediate(decoded.immediate)};
    result.instructions.push_back(ir);
    return result;
}

case R5900Instruction::Lui: {
    if (decoded.rt == 0u) return discarded_gpr_zero_write(decoded, guest_pc);
    auto ir = base_instruction(decoded, guest_pc, R5900IrOpcode::LoadUpperImmediateSignExtend);
    set_destination(ir, R5900IrDestinationKind::Gpr, decoded.rt,
                    R5900IrGprWriteMode::Low64PreserveUpper64);
    ir.inputs = {immediate(decoded.immediate)};
    result.instructions.push_back(ir);
    return result;
}
```

Use `MoveGprLow64` for `MTHI`, `MTLO`, `MTHI1`, and `MTLO1`, with one GPR input and destinations `Hi`, `Lo`, `Hi1`, and `Lo1`. Use `ComputeMtsah` with `Sa <- Gpr(rs), Immediate(decoded.immediate)`. Use `AddPackedU32Saturate128` with `Gpr(rd) <- Gpr(rs),Gpr(rt)` and `Full128`; `rd == 0` lowers to `Nop`. `SYNC` lowers to `Nop`.

- [ ] **Step 4: Add failing validator matrix tests**

In `tests/r5900_ir_validation_tests.cpp`, add builders:

```cpp
R5900IrDestination dst(R5900IrDestinationKind kind, std::uint8_t index = 0u) {
    return {kind, index};
}

R5900IrInstruction unary_ir(R5900IrOpcode opcode,
                            R5900IrDestination destination,
                            R5900IrOperand input,
                            R5900IrGprWriteMode mode = R5900IrGprWriteMode::None) {
    R5900IrInstruction ir{};
    ir.guest_pc = 0x00102040u;
    ir.opcode = opcode;
    ir.destination = destination;
    ir.write_mode = mode;
    ir.inputs = {input};
    return ir;
}
```

Require valid examples for `And64`, `LoadUpperImmediateSignExtend`, `MoveGprLow64` to all four special destinations, `ComputeMtsah`, and `AddPackedU32Saturate128`. Add negative cases for wrong destination kind, GPR/FPR index 32, nonzero index on `Hi/Lo/Hi1/Lo1/Sa`, wrong operand count/type, `Full128` on `And64`, and low64 mode on `AddPackedU32Saturate128`.

- [ ] **Step 5: Run validator tests and verify RED**

```powershell
cmake --build build --config Release --target r5900_ir_validation_tests --parallel
ctest --test-dir build -C Release -R "^r5900_ir_validation_tests$" --output-on-failure
```

Expected: FAIL because the validator has no structural rules for the new opcodes.

- [ ] **Step 6: Implement opcode-specific validation**

Refactor `src/recompiler/r5900_ir_validation.cpp` into small helpers that explicitly check destination kind/index, write mode, operand count, and operand kinds. The core rules must be encoded directly:

```cpp
case R5900IrOpcode::And64:
    return validate_gpr_write(instruction, instruction_index,
                              R5900IrGprWriteMode::Low64PreserveUpper64,
                              {R5900IrOperandKind::Gpr, R5900IrOperandKind::Immediate});
case R5900IrOpcode::LoadUpperImmediateSignExtend:
    return validate_gpr_write(instruction, instruction_index,
                              R5900IrGprWriteMode::Low64PreserveUpper64,
                              {R5900IrOperandKind::Immediate});
case R5900IrOpcode::AddPackedU32Saturate128:
    return validate_gpr_write(instruction, instruction_index,
                              R5900IrGprWriteMode::Full128,
                              {R5900IrOperandKind::Gpr, R5900IrOperandKind::Gpr});
case R5900IrOpcode::MoveGprLow64:
    return validate_special_move(instruction, instruction_index);
case R5900IrOpcode::ComputeMtsah:
    return validate_mtsah(instruction, instruction_index);
```

`validate_special_move` must accept only `Hi`, `Lo`, `Hi1`, or `Lo1` with index zero and exactly one GPR source. `validate_mtsah` must accept only `Sa` with index zero, write mode `None`, and `Gpr,Immediate` inputs. Preserve deterministic instruction-index and guest-PC diagnostics.

- [ ] **Step 7: Run lowering + validation suites and verify GREEN**

```powershell
cmake --build build --config Release --target r5900_ir_tests r5900_ir_validation_tests --parallel
ctest --test-dir build -C Release -R "^(r5900_ir_tests|r5900_ir_validation_tests)$" --output-on-failure
```

Expected: both PASS.

- [ ] **Step 8: Commit integer/special lowering and validation**

```bash
git add src/recompiler/r5900_ir.cpp src/recompiler/r5900_ir_validation.cpp tests/r5900_ir_tests.cpp tests/r5900_ir_validation_tests.cpp
git commit -m "feat: lower R5900 startup integer and MMI subset"
```

---

### Task 3: Implement reference semantics for integer, special-register, MMI, and SYNC operations

**Files:**
- Modify: `src/recompiler/r5900_ir_executor.cpp`
- Modify: `tests/r5900_ir_executor_tests.cpp`

**Interfaces:**
- Consumes: validated IR shapes from Task 2.
- Produces: deterministic oracle semantics for `And64`, `LoadUpperImmediateSignExtend`, `MoveGprLow64`, `ComputeMtsah`, `AddPackedU32Saturate128`, and `Nop`/`SYNC`.

- [ ] **Step 1: Write failing reference-executor tests**

Add tests covering:

```cpp
// ANDI semantics: upper source bits must be cleared by zero-extended imm16.
state.gpr[1].low64 = 0xffff0000123456ffull;
execute(And64, Gpr(2), Gpr(1), Immediate(0x00f0));
expect(state.gpr[2].low64 == 0x00000000000000f0ull, "ANDI must zero upper result bits");

// LUI positive and negative sign extension.
execute(LoadUpperImmediateSignExtend, Gpr(2), Immediate(0x004e));
expect(state.gpr[2].low64 == 0x00000000004e0000ull, "positive LUI must sign-extend correctly");
execute(LoadUpperImmediateSignExtend, Gpr(3), Immediate(0x8040));
expect(state.gpr[3].low64 == 0xffffffff80400000ull, "negative LUI must sign-extend correctly");
```

Add `MTHI/MTLO/MTHI1/MTLO1` tests with distinct 64-bit source patterns, `MTSAH` with `GPR.low32 = 5` and immediate `3` expecting `((5 & 7) ^ 3) << 1 == 12`, and `SYNC` preserving every modeled field.

Add `PADDUW` tests using four independent lanes:

```text
rs lanes = [0xffffffff, 1, 0xfffffffe, 0x80000000]
rt lanes = [1,          2, 1,          0x80000000]
expected = [0xffffffff, 3, 0xffffffff, 0xffffffff]
```

Initialize destination high/low to sentinels, then add alias cases for `rd == rs` and `rd == rt` proving all four source lanes are consumed before destination mutation. Add a GPR0 normalization case.

- [ ] **Step 2: Run executor tests and verify RED**

```powershell
cmake --build build --config Release --target r5900_ir_executor_tests --parallel
ctest --test-dir build -C Release -R "^r5900_ir_executor_tests$" --output-on-failure
```

Expected: FAIL because the new opcodes currently have no executor semantics.

- [ ] **Step 3: Implement typed operand readers and integer/special semantics**

Replace the GPR-assuming operand reader with explicit helpers:

```cpp
std::uint64_t read_gpr_or_immediate64(const R5900IrOperand& operand,
                                      const R5900IrExecutionState& state) {
    if (operand.kind == R5900IrOperandKind::Immediate) {
        return static_cast<std::uint64_t>(operand.immediate);
    }
    return state.gpr[operand.gpr_index].low64;
}

std::uint32_t lane32(const R5900IrGprValue& value, unsigned lane) {
    if (lane < 2u) {
        return static_cast<std::uint32_t>(value.low64 >> (lane * 32u));
    }
    return static_cast<std::uint32_t>(value.high64 >> ((lane - 2u) * 32u));
}

std::uint32_t saturating_add_u32(std::uint32_t lhs, std::uint32_t rhs) {
    const std::uint64_t sum = static_cast<std::uint64_t>(lhs) + rhs;
    return sum > 0xffffffffull ? 0xffffffffu : static_cast<std::uint32_t>(sum);
}
```

Implement `AddPackedU32Saturate128` by copying both source `R5900IrGprValue`s to locals before any destination write, calculating four saturated lanes, and packing lanes 0/1 into `low64` and 2/3 into `high64`.

Implement special moves with a destination-kind switch:

```cpp
case R5900IrDestinationKind::Hi:  state.hi  = value; break;
case R5900IrDestinationKind::Lo:  state.lo  = value; break;
case R5900IrDestinationKind::Hi1: state.hi1 = value; break;
case R5900IrDestinationKind::Lo1: state.lo1 = value; break;
default: break; // unreachable after validation
```

Implement `ComputeMtsah` as:

```cpp
state.sa = static_cast<std::uint32_t>(
    ((state.gpr[ir.inputs[0].gpr_index].low64 & 0x7ull) ^
     (static_cast<std::uint64_t>(ir.inputs[1].immediate) & 0x7ull)) << 1u);
```

Implement `And64` as a 64-bit AND with the already zero-extended IR immediate. Implement `LoadUpperImmediateSignExtend` by shifting the low 16 bits left 16, interpreting the result as `std::int32_t`, and converting to `std::int64_t`/`std::uint64_t`. Preserve destination `high64` for low64 writes.

- [ ] **Step 4: Run executor + earlier suites and verify GREEN**

```powershell
cmake --build build --config Release --target r5900_ir_tests r5900_ir_validation_tests r5900_ir_executor_tests --parallel
ctest --test-dir build -C Release -R "^(r5900_ir_tests|r5900_ir_validation_tests|r5900_ir_executor_tests)$" --output-on-failure
```

Expected: all PASS.

- [ ] **Step 5: Commit reference integer/special semantics**

```bash
git add src/recompiler/r5900_ir_executor.cpp tests/r5900_ir_executor_tests.cpp
git commit -m "feat: execute R5900 startup integer semantics"
```

---

### Task 4: Add COP1 lowering, validation, and reference execution

**Files:**
- Modify: `src/recompiler/r5900_ir.cpp`
- Modify: `src/recompiler/r5900_ir_validation.cpp`
- Modify: `src/recompiler/r5900_ir_executor.cpp`
- Modify: `tests/r5900_ir_tests.cpp`
- Modify: `tests/r5900_ir_validation_tests.cpp`
- Modify: `tests/r5900_ir_executor_tests.cpp`

**Interfaces:**
- Consumes: `R5900IrOperandKind::Fpr`, destinations `Fpr/Fcr31/FpAccumulator`, opcodes `MoveBits32` and `AddF32ToAccumulator`, plus raw `fpr[]/fcr31/fp_acc` state from Task 1.
- Produces: complete reference semantics for `MTC1`, `CTC1 r31`, and the narrow `ADDA.S` v0 contract.

- [ ] **Step 1: Add COP1 encoding helper and failing lowering tests**

Add to `tests/r5900_ir_tests.cpp`:

```cpp
constexpr std::uint32_t cop1_type(std::uint8_t rs,
                                  std::uint8_t rt,
                                  std::uint8_t rd,
                                  std::uint8_t sa,
                                  std::uint8_t funct) {
    return (0x11u << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) |
           funct;
}
```

Require:

```text
MTC1 encoding cop1_type(0x04, 3, 5, 0, 0)
  -> MoveBits32, Fpr(5) <- Gpr(3)

CTC1 encoding cop1_type(0x06, 4, 31, 0, 0)
  -> MoveBits32, Fcr31 <- Gpr(4)

ADDA.S encoding cop1_type(0x10, 2, 1, 0, 0x18)
  -> AddF32ToAccumulator, FpAccumulator <- Fpr(1),Fpr(2)
```

Also require `CTC1` with `rd == 30` to return `UnsupportedInstruction` with an empty IR vector.

- [ ] **Step 2: Run lowering tests and verify RED**

```powershell
cmake --build build --config Release --target r5900_ir_tests --parallel
ctest --test-dir build -C Release -R "^r5900_ir_tests$" --output-on-failure
```

Expected: FAIL because COP1 lowering is not implemented.

- [ ] **Step 3: Implement COP1 lowering**

Add an FPR operand helper:

```cpp
R5900IrOperand fpr(std::uint8_t index) {
    R5900IrOperand operand{};
    operand.kind = R5900IrOperandKind::Fpr;
    operand.gpr_index = index;
    return operand;
}
```

Add lowering cases:

```cpp
case R5900Instruction::Mtc1: {
    auto ir = base_instruction(decoded, guest_pc, R5900IrOpcode::MoveBits32);
    set_destination(ir, R5900IrDestinationKind::Fpr, decoded.rd);
    ir.inputs = {gpr(decoded.rt)};
    result.instructions.push_back(ir);
    return result;
}
case R5900Instruction::Ctc1: {
    if (decoded.rd != 31u) {
        result.error = R5900IrLoweringError::UnsupportedInstruction;
        result.message = "unsupported CTC1 control register: only FCR31 is supported";
        return result;
    }
    auto ir = base_instruction(decoded, guest_pc, R5900IrOpcode::MoveBits32);
    set_destination(ir, R5900IrDestinationKind::Fcr31);
    ir.inputs = {gpr(decoded.rt)};
    result.instructions.push_back(ir);
    return result;
}
case R5900Instruction::AddaS: {
    auto ir = base_instruction(decoded, guest_pc, R5900IrOpcode::AddF32ToAccumulator);
    set_destination(ir, R5900IrDestinationKind::FpAccumulator);
    ir.inputs = {fpr(decoded.rd), fpr(decoded.rt)};
    result.instructions.push_back(ir);
    return result;
}
```

- [ ] **Step 4: Add failing COP1 validator tests**

Require valid `MoveBits32` only for `Fpr(index<32) <- Gpr(index<32)` and `Fcr31(index==0) <- Gpr(index<32)`. Require `AddF32ToAccumulator` only for `FpAccumulator(index==0) <- Fpr,Fpr`. Reject FPR index 32, `MoveBits32` to GPR, nonzero unindexed destination indices, immediate sources, wrong operand counts, and wrong write modes.

Run:

```powershell
cmake --build build --config Release --target r5900_ir_validation_tests --parallel
ctest --test-dir build -C Release -R "^r5900_ir_validation_tests$" --output-on-failure
```

Expected: FAIL until COP1 validation rules exist.

- [ ] **Step 5: Implement COP1 validation rules**

Add explicit cases:

```cpp
case R5900IrOpcode::MoveBits32:
    return validate_move_bits32(instruction, instruction_index);
case R5900IrOpcode::AddF32ToAccumulator:
    return validate_add_f32_accumulator(instruction, instruction_index);
```

`validate_move_bits32` accepts only `Fpr` with index `< 32` or `Fcr31` with index zero, `write_mode == None`, and exactly one GPR source. `validate_add_f32_accumulator` accepts only `FpAccumulator` index zero, `write_mode == None`, and exactly two FPR sources with both indices `< 32`.

- [ ] **Step 6: Add failing reference COP1 semantic tests**

In `tests/r5900_ir_executor_tests.cpp`, add raw-bit cases:

```cpp
state.gpr[3].low64 = 0xdeadbeef12345678ull;
// MTC1 must write exactly 0x12345678 to FPR[5].

state.gpr[4].low64 = 0x11223344a5a5c3c3ull;
// CTC1 must write exactly 0xa5a5c3c3 to fcr31.
```

For `ADDA.S`, use `std::bit_cast<std::uint32_t>(1.5f)` and `std::bit_cast<std::uint32_t>(2.25f)` and expect the bit pattern of `3.75f`. Also test `+0.0f + -0.0f` expecting `+0.0f`. Set `fcr31` to a sentinel before the operation and assert it is unchanged.

- [ ] **Step 7: Implement raw COP1 reference execution**

Add `<bit>` and implement:

```cpp
case R5900IrOpcode::MoveBits32: {
    const auto raw = static_cast<std::uint32_t>(
        state.gpr[ir.inputs[0].gpr_index].low64);
    if (ir.destination->kind == R5900IrDestinationKind::Fpr) {
        state.fpr[ir.destination->index] = raw;
    } else {
        state.fcr31 = raw;
    }
    break;
}
case R5900IrOpcode::AddF32ToAccumulator: {
    const float lhs = std::bit_cast<float>(state.fpr[ir.inputs[0].gpr_index]);
    const float rhs = std::bit_cast<float>(state.fpr[ir.inputs[1].gpr_index]);
    const float sum = lhs + rhs;
    state.fp_acc = std::bit_cast<std::uint32_t>(sum);
    break;
}
```

Do not read or alter host rounding controls and do not update `state.fcr31`.

- [ ] **Step 8: Run portable IR suites and verify GREEN**

```powershell
cmake --build build --config Release --target r5900_ir_tests r5900_ir_validation_tests r5900_ir_executor_tests --parallel
ctest --test-dir build -C Release -R "^(r5900_ir_tests|r5900_ir_validation_tests|r5900_ir_executor_tests)$" --output-on-failure
```

Expected: all PASS.

- [ ] **Step 9: Commit COP1 reference pipeline**

```bash
git add src/recompiler/r5900_ir.cpp src/recompiler/r5900_ir_validation.cpp src/recompiler/r5900_ir_executor.cpp tests/r5900_ir_tests.cpp tests/r5900_ir_validation_tests.cpp tests/r5900_ir_executor_tests.cpp
git commit -m "feat: add R5900 startup COP1 reference semantics"
```

---

### Task 5: Emit native x64 for integer, special-register, MMI, LUI, ANDI, and SYNC semantics

**Files:**
- Modify: `src/recompiler/windows/r5900_x64_backend.cpp`
- Modify: `tests/r5900_x64_backend_windows_tests.cpp`

**Interfaces:**
- Consumes: validated IR and reference semantics from Tasks 2-3, generated-block ABI `void(R5900IrExecutionState*)`, and existing RW -> RX allocation path.
- Produces: native implementations for `And64`, `LoadUpperImmediateSignExtend`, `MoveGprLow64`, `ComputeMtsah`, `AddPackedU32Saturate128`, and `Nop`/`SYNC`.

- [ ] **Step 1: Extend full-state differential test helpers and add failing integer/special native tests**

First extend `expect_states_equal` so it compares every modeled field, not only GPRs:

```cpp
expect(expected.hi == actual.hi && expected.lo == actual.lo, message);
expect(expected.hi1 == actual.hi1 && expected.lo1 == actual.lo1, message);
expect(expected.sa == actual.sa, message);
for (std::size_t index = 0; index < expected.fpr.size(); ++index) {
    expect(expected.fpr[index] == actual.fpr[index], message);
}
expect(expected.fcr31 == actual.fcr31, message);
expect(expected.fp_acc == actual.fp_acc, message);
```

Add one native-vs-reference program containing `ANDI`, positive and negative `LUI`, all four `MoveGprLow64` destination kinds, `MTSAH`, `PADDUW` with a saturation case, and `Nop`. Initialize every GPR low/high and every appended state field with distinct nonzero sentinels so unintended writes are visible.

- [ ] **Step 2: Run x64 backend tests and verify RED**

```powershell
cmake --build build --config Release --target r5900_x64_backend_windows_tests --parallel
ctest --test-dir build -C Release -R "^r5900_x64_backend_windows_tests$" --output-on-failure
```

Expected: compile result reports `UnsupportedOpcode` for the first newly emitted opcode.

- [ ] **Step 3: Add offset helpers and scalar state load/store emitters**

At the top of `r5900_x64_backend.cpp`, retain existing layout assertions and add:

```cpp
static_assert(offsetof(R5900IrExecutionState, gpr) == 0u);

constexpr std::uint32_t state_offset(std::size_t offset) {
    return static_cast<std::uint32_t>(offset);
}
constexpr std::uint32_t hi_offset()  { return state_offset(offsetof(R5900IrExecutionState, hi)); }
constexpr std::uint32_t lo_offset()  { return state_offset(offsetof(R5900IrExecutionState, lo)); }
constexpr std::uint32_t hi1_offset() { return state_offset(offsetof(R5900IrExecutionState, hi1)); }
constexpr std::uint32_t lo1_offset() { return state_offset(offsetof(R5900IrExecutionState, lo1)); }
constexpr std::uint32_t sa_offset()  { return state_offset(offsetof(R5900IrExecutionState, sa)); }
```

Add a 32-bit store helper:

```cpp
void emit_store_eax_to_state(std::vector<std::uint8_t>& bytes, std::uint32_t displacement) {
    bytes.push_back(0x89u);
    bytes.push_back(0x81u);
    emit_u32(bytes, displacement);
}
```

- [ ] **Step 4: Implement simple integer/special emitters**

Implement `MoveGprLow64` by loading the source GPR low64 into `RAX` and storing it at the `offsetof`-derived destination displacement. Implement `ANDI` by loading source low64, applying a 32-bit `AND EAX, imm32` (`0x25` + imm32; the IR immediate is at most 16 bits, so the architectural result necessarily has all upper bits clear), then storing `RAX` to the destination low64. Implement `LUI` with `MOV EAX, imm32`, `CDQE`, and a 64-bit store. Implement `MTSAH` with:

```text
load EAX from source GPR low32
AND EAX, 7        bytes: 83 E0 07
XOR EAX, imm8     bytes: 83 F0 xx
SHL EAX, 1        bytes: D1 E0
store EAX to state.sa
```

`SYNC` remains `R5900IrOpcode::Nop`, so no new emitter case is needed for it.

- [ ] **Step 5: Implement alias-safe four-lane PADDUW emission**

Load both 128-bit sources before writing any destination lane. Add helpers using unaligned SSE loads:

```text
MOVDQU XMM0,[RCX+disp32]  F3 0F 6F 81 <disp32>
MOVDQU XMM1,[RCX+disp32]  F3 0F 6F 89 <disp32>
MOVD EAX,XMM0             66 0F 7E C0
MOVD EDX,XMM1             66 0F 7E CA
PSRLDQ XMM0,4             66 0F 73 D8 04
PSRLDQ XMM1,4             66 0F 73 D9 04
```

For each of four lanes, emit:

```text
MOVD EAX,XMM0
MOVD EDX,XMM1
ADD EAX,EDX               01 D0
SBB EDX,EDX               19 D2
OR  EAX,EDX               09 D0
MOV [RCX+dest_lane],EAX   89 81 <disp32>
```

`SBB EDX,EDX` converts carry into `0xffffffff`, and `OR` saturates overflowing unsigned additions. After lanes 0-2, shift both XMM sources right by four bytes. Because XMM0/XMM1 capture both complete sources before the first destination store, `rd == rs` and `rd == rt` remain correct.

- [ ] **Step 6: Wire new opcodes into the compile switch**

Add explicit cases:

```cpp
case R5900IrOpcode::And64:
    emit_and64(bytes, instruction);
    break;
case R5900IrOpcode::LoadUpperImmediateSignExtend:
    emit_lui_value(bytes, instruction);
    break;
case R5900IrOpcode::MoveGprLow64:
    emit_move_gpr_low64(bytes, instruction);
    break;
case R5900IrOpcode::ComputeMtsah:
    emit_mtsah(bytes, instruction);
    break;
case R5900IrOpcode::AddPackedU32Saturate128:
    emit_padduw(bytes, instruction);
    break;
```

The existing validation pass must still complete before `bytes` are allocated into executable memory.

- [ ] **Step 7: Run native differential tests and verify GREEN**

```powershell
cmake --build build --config Release --target r5900_x64_backend_windows_tests --parallel
ctest --test-dir build -C Release -R "^r5900_x64_backend_windows_tests$" --output-on-failure
```

Expected: PASS with bit-for-bit equality for all modeled state fields in the integer/special differential program.

- [ ] **Step 8: Commit native integer/special backend**

```bash
git add src/recompiler/windows/r5900_x64_backend.cpp tests/r5900_x64_backend_windows_tests.cpp
git commit -m "feat: emit R5900 startup integer x64 code"
```

---

### Task 6: Emit native COP1 startup operations and prove full-state differential equivalence

**Files:**
- Modify: `src/recompiler/windows/r5900_x64_backend.cpp`
- Modify: `tests/r5900_x64_backend_windows_tests.cpp`

**Interfaces:**
- Consumes: reference `MoveBits32` and `AddF32ToAccumulator` semantics from Task 4 plus full-state comparison helper from Task 5.
- Produces: native `MTC1`, `CTC1/FCR31`, and `ADDA.S` support with raw-bit state equivalence.

- [ ] **Step 1: Add failing native COP1 differential tests**

Build an IR program that:

```text
MTC1: Gpr(3) -> Fpr(5)
MTC1: Gpr(4) -> Fpr(6)
CTC1: Gpr(7) -> Fcr31
ADDA.S: Fpr(5),Fpr(6) -> FpAccumulator
```

Use GPR low32 values containing raw bits for `1.5f`, `2.25f`, and a distinct `fcr31` sentinel source. Run the reference executor into `expected`, compile/run native into `actual`, then call the full-state equality helper. Add a second `ADDA.S` case with `+0.0f` and `-0.0f`. Assert `fcr31` remains unchanged across `ADDA.S` itself.

- [ ] **Step 2: Run backend tests and verify RED**

```powershell
cmake --build build --config Release --target r5900_x64_backend_windows_tests --parallel
ctest --test-dir build -C Release -R "^r5900_x64_backend_windows_tests$" --output-on-failure
```

Expected: `UnsupportedOpcode` for `MoveBits32` or `AddF32ToAccumulator`.

- [ ] **Step 3: Add FPR/FCR/accumulator offset helpers and raw move emission**

Add:

```cpp
constexpr std::uint32_t fpr_offset(std::uint8_t index) {
    return state_offset(offsetof(R5900IrExecutionState, fpr) +
                        static_cast<std::size_t>(index) * sizeof(std::uint32_t));
}
constexpr std::uint32_t fcr31_offset() {
    return state_offset(offsetof(R5900IrExecutionState, fcr31));
}
constexpr std::uint32_t fp_acc_offset() {
    return state_offset(offsetof(R5900IrExecutionState, fp_acc));
}
```

For `MoveBits32`, load `EAX` from the GPR low32 and store `EAX` either to `fpr_offset(destination.index)` or `fcr31_offset()` according to the already validated destination kind.

- [ ] **Step 4: Emit scalar single-precision ADDA.S**

Use exact scalar SSE instructions:

```text
MOVSS XMM0,[RCX+fpr(fs)]      F3 0F 10 81 <disp32>
ADDSS XMM0,[RCX+fpr(ft)]      F3 0F 58 81 <disp32>
MOVSS [RCX+fp_acc],XMM0       F3 0F 11 81 <disp32>
```

Do not emit MXCSR reads/writes and do not touch `fcr31`.

- [ ] **Step 5: Wire COP1 opcodes and run the complete backend suite**

Add:

```cpp
case R5900IrOpcode::MoveBits32:
    emit_move_bits32(bytes, instruction);
    break;
case R5900IrOpcode::AddF32ToAccumulator:
    emit_add_f32_accumulator(bytes, instruction);
    break;
```

Run:

```powershell
cmake --build build --config Release --target r5900_x64_backend_windows_tests --parallel
ctest --test-dir build -C Release -R "^r5900_x64_backend_windows_tests$" --output-on-failure
```

Expected: PASS; reference and native states match for all GPRs, HI/LO/HI1/LO1, SA, all 32 raw FPRs, FCR31, and FP accumulator.

- [ ] **Step 6: Commit native COP1 backend**

```bash
git add src/recompiler/windows/r5900_x64_backend.cpp tests/r5900_x64_backend_windows_tests.cpp
git commit -m "feat: emit R5900 startup COP1 x64 code"
```

---

### Task 7: Expand dispatcher eligibility, execute the 74-instruction synthetic startup, add external real-ELF validation, and document evidence

**Files:**
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Modify: `tests/r5900_block_dispatcher_windows_tests.cpp`
- Modify: `README.md`
- Modify: `docs/PROGRESS.md`

**Interfaces:**
- Consumes: complete lowering/validation/reference/native support from Tasks 1-6 and existing dispatcher cache/boundary behavior.
- Produces: one native dispatcher block covering the startup subset, exact `ControlFlow` stop at the synthetic/real `BEQ`, optional out-of-repository validation against a user-supplied ELF path, and honest milestone documentation.

- [ ] **Step 1: Add encoding helpers and the 74-instruction synthetic startup builder**

In `tests/r5900_block_dispatcher_windows_tests.cpp`, add `mmi_type` and `cop1_type` with the same synthetic encodings used by decoder tests. Add:

```cpp
std::vector<std::uint32_t> make_startup_prefix_words(std::uint32_t base) {
    std::vector<std::uint32_t> words;

    // 30 packed clears. This is synthetic ISA coverage, not copied game bytes.
    for (std::uint8_t rd = 1u; rd <= 30u; ++rd) {
        words.push_back(mmi_type(0, 0, rd, 0x10, 0x28)); // PADDUW rd,zero,zero
    }

    words.push_back(r_type(0, 0, 0, 0, 0x11));          // MTHI zero
    words.push_back(r_type(0, 0, 0, 0, 0x13));          // MTLO zero
    words.push_back(mmi_type(0, 0, 0, 0, 0x11));        // MTHI1 zero
    words.push_back(mmi_type(0, 0, 0, 0, 0x13));        // MTLO1 zero
    words.push_back(i_type(0x01, 0, 0x19, 0));          // MTSAH zero,0
    words.push_back(r_type(0, 0, 0, 16, 0x0f));         // SYNC

    for (std::uint8_t fs = 0u; fs < 32u; ++fs) {
        words.push_back(cop1_type(0x04, 0, fs, 0, 0));   // MTC1 zero,fs
    }

    words.push_back(cop1_type(0x06, 0, 31, 0, 0));      // CTC1 zero,31
    words.push_back(cop1_type(0x10, 0, 0, 0, 0x18));    // ADDA.S f0,f0
    words.push_back(i_type(0x0f, 0, 2, 0x004e));        // LUI r2,0x004e
    words.push_back(i_type(0x0d, 2, 2, 0x2680));        // ORI r2,r2,0x2680
    words.push_back(i_type(0x0f, 0, 3, 0x01ec));        // LUI r3,0x01ec
    words.push_back(i_type(0x0d, 3, 3, 0xea00));        // ORI r3,r3,0xea00

    expect(words.size() == 74u, "synthetic startup must contain exactly 74 executable instructions");
    expect(base + static_cast<std::uint32_t>(words.size() * 4u) == 0x00100130u,
           "synthetic startup control-flow boundary must be 0x00100130");

    words.push_back(i_type(0x04, 0, 0, 1u));             // BEQ zero,zero,+1
    words.push_back(i_type(0x09, 0, 4, 1u));             // delay-slot sentinel; must not execute
    return words;
}
```

Use `base = 0x00100008u` for this test.

- [ ] **Step 2: Add the failing dispatcher startup test**

Construct an executable synthetic ELF from `make_startup_prefix_words(base)`, set `max_instructions = 128`, run `dispatcher.run(base, state, 1u)`, and require:

```cpp
expect(result.reason == R5900DispatchStopReason::ControlFlow,
       "startup prefix must stop before BEQ");
expect(result.next_pc == 0x00100130u,
       "startup boundary PC must be exact");
expect(result.blocks_executed == 1u && result.instructions_executed == 74u,
       "startup prefix must execute exactly 74 instructions");
expect(result.cache_misses == 1u && result.cache_hits == 0u,
       "first startup compilation must be one cache miss");
expect(state.gpr[2].low64 == 0x00000000004e2680ull,
       "synthetic startup must construct r2");
expect(state.gpr[3].low64 == 0x0000000001ecea00ull,
       "synthetic startup must construct r3");
expect(state.gpr[4].low64 == 0u,
       "BEQ delay slot must not execute");
expect(state.hi == 0u && state.lo == 0u && state.hi1 == 0u && state.lo1 == 0u,
       "startup special registers must be zero");
expect(state.sa == 0u && state.fcr31 == 0u && state.fp_acc == 0u,
       "startup SA/FCR31/FP accumulator must be zero");
for (const auto raw : state.fpr) {
    expect(raw == 0u, "startup FPRs must be zero");
}
```

Initialize `gpr[31]` to a known sentinel and assert it is preserved so the test proves the 30 synthetic PADDUW operations do not accidentally touch unrelated GPRs.

- [ ] **Step 3: Run dispatcher test and verify RED at current eligibility gate**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_windows_tests --parallel
ctest --test-dir build -C Release -R "^r5900_block_dispatcher_windows_tests$" --output-on-failure
```

Expected: FAIL with `UnsupportedInstruction` at the first new startup instruction because `is_dispatcher_v0_eligible()` still permits only NOP/ADDU/ADDIU/ORI.

- [ ] **Step 4: Expand dispatcher eligibility only to the completed subset**

Replace the eligibility switch with exactly:

```cpp
bool is_dispatcher_v0_eligible(R5900Instruction instruction) noexcept {
    switch (instruction) {
    case R5900Instruction::Nop:
    case R5900Instruction::Addu:
    case R5900Instruction::Addiu:
    case R5900Instruction::Ori:
    case R5900Instruction::Andi:
    case R5900Instruction::Lui:
    case R5900Instruction::Mthi:
    case R5900Instruction::Mtlo:
    case R5900Instruction::Mthi1:
    case R5900Instruction::Mtlo1:
    case R5900Instruction::Mtsah:
    case R5900Instruction::Padduw:
    case R5900Instruction::Mtc1:
    case R5900Instruction::Ctc1:
    case R5900Instruction::AddaS:
    case R5900Instruction::Sync:
        return true;
    default:
        return false;
    }
}
```

Do not alter branch, trap, delay-slot, cache fingerprint, stale replacement, or block-budget logic.

- [ ] **Step 5: Update obsolete ANDI-boundary tests and verify the synthetic startup GREEN**

The existing test that expects a supported NOP/ADDU/ADDIU/ORI prefix to stop before `ANDI` must now expect `ANDI` to execute. Replace its unsupported sentinel with an instruction that remains deliberately unsupported, such as `XORI`, and preserve the same prefix-boundary assertions. Replace the standalone `ANDI at entry must be unsupported` case with a direct `ANDI` execution case checking zero-extension and high64 preservation.

Run:

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_windows_tests --parallel
ctest --test-dir build -C Release -R "^r5900_block_dispatcher_windows_tests$" --output-on-failure
```

Expected: PASS, including exactly 74 executed startup instructions and a `ControlFlow` stop at `0x00100130` with no delay-slot execution.

- [ ] **Step 6: Add optional externally supplied real-ELF validation to the existing Windows test executable**

Change the test entry point to `int main(int argc, char** argv)`. Add `<fstream>` and a file reader:

```cpp
Bytes read_binary_file(const char* path) {
    std::ifstream input(path, std::ios::binary);
    expect(static_cast<bool>(input), "external ELF must open");
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    expect(size > 0, "external ELF must be non-empty");
    input.seekg(0, std::ios::beg);
    Bytes bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    expect(static_cast<bool>(input), "external ELF must read completely");
    return bytes;
}
```

Add a function that runs only when one path argument is supplied:

```cpp
void validate_external_startup(const char* path) {
    const auto bytes = read_binary_file(path);
    const auto parsed = b3r::recompiler::parse_ps2_elf(bytes);
    expect(parsed.ok(), "external Burnout 3 ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "external Burnout 3 ELF must map");

    b3r::recompiler::R5900BlockDispatcherOptions options{};
    options.block_options.max_instructions = 256u;
    b3r::recompiler::R5900BlockDispatcher dispatcher(*built.memory, options);
    b3r::recompiler::R5900IrExecutionState state{};

    const auto result = dispatcher.run(0x00100008u, state, 1u);
    expect(result.reason == b3r::recompiler::R5900DispatchStopReason::ControlFlow,
           "real startup must stop at first control-flow boundary");
    expect(result.next_pc == 0x00100130u,
           "real startup first control-flow PC must be 0x00100130");
    expect(result.blocks_executed == 1u && result.instructions_executed == 74u,
           "real startup must execute exactly 74 native guest instructions");
    expect(state.gpr[2].low64 == 0x00000000004e2680ull,
           "real startup r2 invariant must match observed ELF");
    expect(state.gpr[3].low64 == 0x0000000001ecea00ull,
           "real startup r3 invariant must match observed ELF");
    expect(state.gpr[4].low64 == 0u,
           "real startup delay slot must remain unexecuted");

    std::cout << "REAL_ELF_STARTUP_VALIDATED start=0x00100008 stop=0x00100130 instructions=74\n";
}
```

At the end of normal synthetic tests:

```cpp
if (argc == 2) {
    validate_external_startup(argv[1]);
} else if (argc != 1) {
    fail("usage: r5900_block_dispatcher_windows_tests.exe [external-elf-path]");
}
```

The ordinary CTest invocation passes no path and remains fully synthetic. No proprietary file is copied into the repository or CI artifacts.

- [ ] **Step 7: Run full Windows regression**

Run:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\frame_pacer_windows_tests.exe
.\build\Release\Burnout3PacingProbe.exe --seconds 1
```

Expected: all 28 CTest targets PASS; frame pacing telemetry and the one-second/120-frame pacing probe remain green.

- [ ] **Step 8: Run external validation when the supplied legal ELF is available on a Windows host**

Run with the user's external file path; for example:

```powershell
.\build\Release\r5900_block_dispatcher_windows_tests.exe "D:\Games\Burnout3\SLUS_210.50"
```

Required terminal evidence:

```text
REAL_ELF_STARTUP_VALIDATED start=0x00100008 stop=0x00100130 instructions=74
r5900_block_dispatcher_windows_tests: PASS
```

Do not mark the real-ELF milestone externally validated unless this command exits successfully against the supplied file. If no Windows host with that file is available during execution, keep the code/CI milestone green but document the external run as pending rather than inferring success from the synthetic fixture.

- [ ] **Step 9: Update README and PROGRESS with exact scope and evidence**

Update `README.md` to state that the runtime can natively execute the narrow startup subset through the first branch boundary, while explicitly retaining these limitations: no branch execution, no delay slots, no guest loads/stores, no syscall HLE, no graphics/audio/input, no game boot, no menu, and no gameplay.

Update the decoder/runtime rows in `docs/PROGRESS.md` and add a startup-execution evidence line. If Step 8 succeeded, use `EXTERNALLY_VALIDATED` and record the exact validation command/result plus the final Windows CI run ID. If Step 8 was not executable, use `CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION` and state that only the synthetic 74-instruction startup fixture has executed natively so far.

- [ ] **Step 10: Commit dispatcher integration and documentation**

```bash
git add src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_windows_tests.cpp README.md docs/PROGRESS.md
git commit -m "feat: execute R5900 startup prefix natively"
```

- [ ] **Step 11: Push branch and require final Windows CI success**

```bash
git push origin feature/r5900-startup-execution-v0
```

Required final gate: Windows Server 2022 / MSVC 19.44 workflow completes successfully at the branch head with all configure/build/test/pacing/package stages green. Do not integrate this branch into `main` as part of this plan; integration remains a separate user decision after final review.

---

## Completion Checklist

- [ ] `R5900IrExecutionState` keeps `gpr[32]` at offset zero and appends HI/LO/HI1/LO1/SA/FPR/FCR31/FP_ACC.
- [ ] Typed IR destinations and FPR operands validate structurally before execution/publication.
- [ ] `ANDI`, `LUI`, `MTHI`, `MTLO`, `MTHI1`, `MTLO1`, `MTSAH`, `PADDUW`, `MTC1`, `CTC1 r31`, `ADDA.S`, and `SYNC` lower deterministically with guest PC/raw provenance.
- [ ] Reference executor implements the complete v0 semantic subset and remains fail-fast on malformed IR.
- [ ] Windows x64 backend matches the reference executor across every modeled state field.
- [ ] `PADDUW` is four-lane unsigned saturating and correct under source/destination aliasing.
- [ ] `ADDA.S` uses scalar single-precision host arithmetic for the explicitly narrow v0 contract and leaves FCR31 unchanged.
- [ ] Dispatcher executes exactly 74 synthetic startup instructions from `0x00100008` and stops before `BEQ` at `0x00100130` without executing its delay slot.
- [ ] Existing cache, budget, stale-code, trap, branch-boundary, and failure-path tests remain green.
- [ ] External `SLUS_210.50` validation is reported only if the optional test invocation actually succeeds; otherwise status remains ready for that validation.
- [ ] No proprietary game bytes or derived binary data are committed.
- [ ] Full Windows CI is green at the final branch head.
