# R5900 Startup Execution v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Natively execute the 74 straight-line EE startup instructions from guest PC `0x00100008` through `0x0010012c`, then stop before the first `BEQ` at `0x00100130`.

**Architecture:** Append the minimum HI/LO/SA/COP1 state to the existing `R5900IrExecutionState` while preserving the current GPR ABI. Generalize IR destinations and operands, make the reference executor the semantic oracle, implement matching Windows x64/SSE emitters, then expand dispatcher eligibility only after differential tests are green.

**Tech Stack:** C++20, CMake, Visual Studio 2022/MSVC 19.44, Windows x64 ABI, SSE/SSE2, GitHub Actions Windows Server 2022.

**Spec:** `docs/superpowers/specs/2026-09-04-r5900-startup-execution-v0-design.md`

## Global Constraints

- `R5900IrExecutionState` remains the only execution-state object passed to reference and native blocks.
- `gpr[32]` remains the first state field; `offsetof(R5900IrExecutionState, gpr) == 0` and `sizeof(R5900IrGprValue) == 16` are permanent ABI checks.
- New native-state displacements come from `offsetof(...)`.
- `fpr[32]` and `fp_acc` store raw 32-bit IEEE-754 bit patterns.
- `CTC1` v0 supports only FCR31; it does not synchronize host MXCSR.
- `ADDA.S` v0 covers finite normal values and zero cases needed by the observed startup and does not claim EE-exact NaN/denormal/overflow/underflow/rounding/exception behavior.
- `ADDA.S` does not mutate `fcr31`.
- `SYNC` is a semantic no-op in this runtime version.
- Branches, jumps, syscalls/traps, guest loads/stores, and delay slots remain unexecuted.
- Dispatcher cache fingerprinting, stale replacement, block budgets, partial-progress behavior, RW -> RX allocation, instruction-cache flush, and RAII ownership remain unchanged.
- No proprietary game bytes or derived binary blobs are committed. All repository fixtures use synthetic encodings.
- Each task follows RED -> GREEN and ends in a focused commit.

## File Responsibilities

- `src/recompiler/r5900_ir.h` — IR destination/operand/opcode model.
- `src/recompiler/r5900_ir.cpp` — guest instruction lowering.
- `src/recompiler/r5900_ir_validation.cpp` — structural validation.
- `src/recompiler/r5900_ir_executor.h` — shared modeled architecture state.
- `src/recompiler/r5900_ir_executor.cpp` — reference semantics.
- `src/recompiler/windows/r5900_x64_backend.cpp` — generated Windows x64/SSE code.
- `src/recompiler/windows/r5900_block_dispatcher.cpp` — native eligibility/boundary/cache bridge.
- `tests/r5900_ir_tests.cpp` — lowering.
- `tests/r5900_ir_validation_tests.cpp` — malformed/valid IR matrix.
- `tests/r5900_ir_executor_tests.cpp` — reference semantics.
- `tests/r5900_x64_backend_windows_tests.cpp` — ABI/native/differential semantics.
- `tests/r5900_block_dispatcher_windows_tests.cpp` — synthetic startup and optional external ELF validation.
- `README.md`, `docs/PROGRESS.md` — public status/evidence.

---

### Task 1: Generalize IR destinations and append architectural state

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
- Consumes: existing NOP/ADDU/ADDIU/ORI IR and state.
- Produces: `R5900IrDestination`, new destination/operand/write/opcode enums, and appended state fields while keeping existing semantics green.

- [ ] **Step 1: Write ABI/model RED tests**

In `tests/r5900_x64_backend_windows_tests.cpp`, include `<cstddef>` and add:

```cpp
static_assert(std::is_standard_layout_v<R5900IrExecutionState>);
static_assert(sizeof(R5900IrGprValue) == 16u);
static_assert(offsetof(R5900IrExecutionState, gpr) == 0u);
static_assert(offsetof(R5900IrExecutionState, hi) >= 32u * sizeof(R5900IrGprValue));
static_assert(offsetof(R5900IrExecutionState, fpr) > offsetof(R5900IrExecutionState, sa));
static_assert(offsetof(R5900IrExecutionState, fp_acc) > offsetof(R5900IrExecutionState, fcr31));
```

In each IR test file, add:

```cpp
R5900IrDestination gpr_destination(std::uint8_t index) {
    return {R5900IrDestinationKind::Gpr, index};
}
```

and change existing GPR destination construction to this type. Add to `r5900_ir_validation_tests.cpp`:

```cpp
R5900IrInstruction probe{};
probe.destination = R5900IrDestination{R5900IrDestinationKind::Fpr, 3u};
probe.write_mode = R5900IrGprWriteMode::Full128;
R5900IrOperand fpr_probe{};
fpr_probe.kind = R5900IrOperandKind::Fpr;
fpr_probe.gpr_index = 4u;
```

- [ ] **Step 2: Verify RED**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target r5900_ir_tests r5900_ir_validation_tests r5900_ir_executor_tests r5900_x64_backend_windows_tests --parallel
```

Expected: compile failure because the new types/state fields do not exist.

- [ ] **Step 3: Implement typed IR and appended state**

In `src/recompiler/r5900_ir.h`:

```cpp
enum class R5900IrDestinationKind {
    Gpr = 0, Hi, Lo, Hi1, Lo1, Sa, Fpr, Fcr31, FpAccumulator,
};

struct R5900IrDestination {
    R5900IrDestinationKind kind{R5900IrDestinationKind::Gpr};
    std::uint8_t index{};
};

enum class R5900IrOperandKind { Gpr = 0, Fpr, Immediate };

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

Change the destination member to:

```cpp
std::optional<R5900IrDestination> destination{};
```

In `src/recompiler/r5900_ir_executor.h`:

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

Adapt current lowering:

```cpp
void set_low64_destination(R5900IrInstruction& ir, std::uint8_t index) {
    ir.destination = R5900IrDestination{R5900IrDestinationKind::Gpr, index};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
}
```

Update current validator/test construction from `R5900IrRegister` to `R5900IrDestination`. For existing `AddWordSignExtend` and `Or64`, explicitly require `destination.kind == Gpr`.

- [ ] **Step 4: Verify GREEN**

```powershell
cmake --build build --config Release --target r5900_ir_tests r5900_ir_validation_tests r5900_ir_executor_tests r5900_x64_backend_windows_tests --parallel
ctest --test-dir build -C Release -R "^(r5900_ir_tests|r5900_ir_validation_tests|r5900_ir_executor_tests|r5900_x64_backend_windows_tests)$" --output-on-failure
```

Expected: all PASS with unchanged NOP/ADDU/ADDIU/ORI behavior.

- [ ] **Step 5: Commit**

```bash
git add src/recompiler/r5900_ir.h src/recompiler/r5900_ir.cpp src/recompiler/r5900_ir_validation.cpp src/recompiler/r5900_ir_executor.h tests/r5900_ir_tests.cpp tests/r5900_ir_validation_tests.cpp tests/r5900_ir_executor_tests.cpp tests/r5900_x64_backend_windows_tests.cpp
git commit -m "refactor: generalize R5900 IR execution state"
```

---

### Task 2: Lower and validate non-COP1 startup instructions

**Files:**
- Modify: `src/recompiler/r5900_ir.cpp`
- Modify: `src/recompiler/r5900_ir_validation.cpp`
- Modify: `tests/r5900_ir_tests.cpp`
- Modify: `tests/r5900_ir_validation_tests.cpp`

**Interfaces:**
- Consumes: Task 1 model and decoder support for `ANDI/LUI/MTHI/MTLO/MTHI1/MTLO1/MTSAH/PADDUW/SYNC`.
- Produces: validated semantic IR for those instructions.

- [ ] **Step 1: Add exact synthetic lowering helpers**

In `tests/r5900_ir_tests.cpp`:

```cpp
constexpr std::uint32_t mmi_type(std::uint8_t rs, std::uint8_t rt,
                                 std::uint8_t rd, std::uint8_t sa,
                                 std::uint8_t funct) {
    return (0x1cu << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) | funct;
}

void expect_lower(std::uint32_t word, R5900IrOpcode opcode,
                  R5900IrDestinationKind kind, std::uint8_t index,
                  std::uint32_t pc) {
    const auto result = lower_r5900_instruction(decode_r5900(word), pc);
    expect(result.ok(), "startup instruction must lower");
    expect(result.instructions.size() == 1u, "startup instruction must emit one IR op");
    const auto& ir = result.instructions.front();
    expect(ir.opcode == opcode, "startup opcode mismatch");
    expect(ir.destination.has_value(), "startup destination missing");
    expect(ir.destination->kind == kind && ir.destination->index == index,
           "startup destination mismatch");
    expect(ir.guest_pc == pc && ir.guest_raw == word,
           "startup provenance mismatch");
}
```

- [ ] **Step 2: Write lowering RED cases**

```cpp
expect_lower(i_type(0x0c,1,2,0x00ff), R5900IrOpcode::And64,
             R5900IrDestinationKind::Gpr, 2, 0x00101000u);
expect_lower(i_type(0x0f,0,2,0x8040), R5900IrOpcode::LoadUpperImmediateSignExtend,
             R5900IrDestinationKind::Gpr, 2, 0x00101004u);
expect_lower(r_type(7,0,0,0,0x11), R5900IrOpcode::MoveGprLow64,
             R5900IrDestinationKind::Hi, 0, 0x00101008u);
expect_lower(r_type(8,0,0,0,0x13), R5900IrOpcode::MoveGprLow64,
             R5900IrDestinationKind::Lo, 0, 0x0010100cu);
expect_lower(mmi_type(9,0,0,0,0x11), R5900IrOpcode::MoveGprLow64,
             R5900IrDestinationKind::Hi1, 0, 0x00101010u);
expect_lower(mmi_type(10,0,0,0,0x13), R5900IrOpcode::MoveGprLow64,
             R5900IrDestinationKind::Lo1, 0, 0x00101014u);
expect_lower(i_type(0x01,11,0x19,5), R5900IrOpcode::ComputeMtsah,
             R5900IrDestinationKind::Sa, 0, 0x00101018u);
expect_lower(mmi_type(12,13,14,0x10,0x28), R5900IrOpcode::AddPackedU32Saturate128,
             R5900IrDestinationKind::Gpr, 14, 0x0010101cu);
```

Also assert `ANDI` immediate equals `0x00ff`, `LUI` input equals `0x8040`, `MTSAH` inputs equal `Gpr(11),Immediate(5)`, `PADDUW.write_mode == Full128`, and `SYNC` lowers to `Nop`. `ANDI/LUI/PADDUW` targeting GPR0 must lower to a provenance-carrying Nop.

- [ ] **Step 3: Verify lowering RED**

```powershell
cmake --build build --config Release --target r5900_ir_tests --parallel
ctest --test-dir build -C Release -R "^r5900_ir_tests$" --output-on-failure
```

Expected: FAIL with unsupported lowering.

- [ ] **Step 4: Implement minimal lowering**

Add:

```cpp
R5900IrDestination destination(R5900IrDestinationKind kind, std::uint8_t index = 0u) {
    return {kind, index};
}

void set_destination(R5900IrInstruction& ir, R5900IrDestinationKind kind,
                     std::uint8_t index = 0u,
                     R5900IrGprWriteMode mode = R5900IrGprWriteMode::None) {
    ir.destination = destination(kind, index);
    ir.write_mode = mode;
}
```

Mappings:

```text
ANDI   -> And64, Gpr(rt) <- Gpr(rs),Immediate(decoded.immediate), Low64PreserveUpper64
LUI    -> LoadUpperImmediateSignExtend, Gpr(rt) <- Immediate(decoded.immediate), Low64PreserveUpper64
MTHI   -> MoveGprLow64, Hi <- Gpr(rs)
MTLO   -> MoveGprLow64, Lo <- Gpr(rs)
MTHI1  -> MoveGprLow64, Hi1 <- Gpr(rs)
MTLO1  -> MoveGprLow64, Lo1 <- Gpr(rs)
MTSAH  -> ComputeMtsah, Sa <- Gpr(rs),Immediate(decoded.immediate)
PADDUW -> AddPackedU32Saturate128, Gpr(rd) <- Gpr(rs),Gpr(rt), Full128
SYNC   -> Nop
```

- [ ] **Step 5: Add exact validator builder and RED matrix**

In `tests/r5900_ir_validation_tests.cpp`:

```cpp
R5900IrInstruction make_ir(R5900IrOpcode opcode,
                           R5900IrDestination destination,
                           R5900IrGprWriteMode mode,
                           std::initializer_list<R5900IrOperand> inputs,
                           std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = opcode;
    ir.destination = destination;
    ir.write_mode = mode;
    ir.inputs.assign(inputs.begin(), inputs.end());
    return ir;
}
```

Require valid shapes:

```text
And64: Gpr <- Gpr,Immediate; Low64PreserveUpper64
LoadUpperImmediateSignExtend: Gpr <- Immediate; Low64PreserveUpper64
AddPackedU32Saturate128: Gpr <- Gpr,Gpr; Full128
MoveGprLow64: Hi|Lo|Hi1|Lo1(index=0) <- Gpr; None
ComputeMtsah: Sa(index=0) <- Gpr,Immediate; None
```

Reject GPR index 32, nonzero unindexed destination index, wrong destination kind, wrong operand count/kind, `Full128` on `And64`, and low64 mode on packed add.

- [ ] **Step 6: Verify validator RED, implement rules, verify GREEN**

```powershell
cmake --build build --config Release --target r5900_ir_validation_tests --parallel
ctest --test-dir build -C Release -R "^r5900_ir_validation_tests$" --output-on-failure
```

Expected: RED before the new switch rules. Implement the five exact structural shapes above and retain instruction-index/guest-PC diagnostics. Then run:

```powershell
cmake --build build --config Release --target r5900_ir_tests r5900_ir_validation_tests --parallel
ctest --test-dir build -C Release -R "^(r5900_ir_tests|r5900_ir_validation_tests)$" --output-on-failure
```

Expected: both PASS.

- [ ] **Step 7: Commit**

```bash
git add src/recompiler/r5900_ir.cpp src/recompiler/r5900_ir_validation.cpp tests/r5900_ir_tests.cpp tests/r5900_ir_validation_tests.cpp
git commit -m "feat: lower R5900 startup integer and MMI subset"
```

---

### Task 3: Implement reference non-COP1 semantics

**Files:**
- Modify: `src/recompiler/r5900_ir_executor.cpp`
- Modify: `tests/r5900_ir_executor_tests.cpp`

**Interfaces:**
- Consumes: Task 2 validated IR.
- Produces: deterministic semantic oracle for ANDI/LUI/special registers/MTSAH/PADDUW/SYNC.

- [ ] **Step 1: Add a complete executor test builder**

```cpp
R5900IrInstruction make_exec_ir(R5900IrOpcode opcode,
                                R5900IrDestination destination,
                                R5900IrGprWriteMode mode,
                                std::initializer_list<R5900IrOperand> inputs,
                                std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = opcode;
    ir.destination = destination;
    ir.write_mode = mode;
    ir.inputs.assign(inputs.begin(), inputs.end());
    return ir;
}
```

- [ ] **Step 2: Write semantic RED cases**

ANDI:

```cpp
R5900IrExecutionState state{};
state.gpr[1] = {0xffff0000123456ffull, 0x9999888877776666ull};
state.gpr[2].high64 = 0x1122334455667788ull;
auto ir = make_exec_ir(R5900IrOpcode::And64,
    {R5900IrDestinationKind::Gpr,2}, R5900IrGprWriteMode::Low64PreserveUpper64,
    {gpr(1), immediate(0x00f0)}, 0x00102000u);
expect(execute_r5900_ir({ir}, state).ok(), "ANDI IR must execute");
expect(state.gpr[2].low64 == 0x00000000000000f0ull, "ANDI result mismatch");
expect(state.gpr[2].high64 == 0x1122334455667788ull, "ANDI high64 changed");
```

Add LUI `0x004e -> 0x00000000004e0000` and `0x8040 -> 0xffffffff80400000`. Add four special-register moves from distinct 64-bit sources. Add MTSAH with source low bits `5`, immediate `3`, expected `12`.

PADDUW source lanes:

```text
lhs [ffffffff,00000001,fffffffe,80000000]
rhs [00000001,00000002,00000001,80000000]
out [ffffffff,00000003,ffffffff,ffffffff]
```

Run the packed case three times: separate destination, destination equals lhs, destination equals rhs. Each must produce the same 128-bit result.

- [ ] **Step 3: Verify RED**

```powershell
cmake --build build --config Release --target r5900_ir_executor_tests --parallel
ctest --test-dir build -C Release -R "^r5900_ir_executor_tests$" --output-on-failure
```

Expected: new result checks fail.

- [ ] **Step 4: Implement reference helpers/semantics**

```cpp
std::uint64_t read_gpr_or_imm64(const R5900IrOperand& op,
                                const R5900IrExecutionState& state) {
    return op.kind == R5900IrOperandKind::Immediate
        ? static_cast<std::uint64_t>(op.immediate)
        : state.gpr[op.gpr_index].low64;
}

std::uint32_t lane32(const R5900IrGprValue& value, unsigned lane) {
    return lane < 2u
        ? static_cast<std::uint32_t>(value.low64 >> (lane * 32u))
        : static_cast<std::uint32_t>(value.high64 >> ((lane - 2u) * 32u));
}

std::uint32_t sat_add_u32(std::uint32_t a, std::uint32_t b) {
    const std::uint64_t sum = static_cast<std::uint64_t>(a) + b;
    return sum > 0xffffffffull ? 0xffffffffu : static_cast<std::uint32_t>(sum);
}
```

For PADDUW, copy both source `R5900IrGprValue`s before any write, compute all four lanes, then pack lanes 0/1 into low64 and 2/3 into high64. Implement special destination switch for `Hi/Lo/Hi1/Lo1`. Implement MTSAH as `((gpr.low64 & 7) ^ (imm & 7)) << 1`. Implement LUI through signed 32-bit sign extension. Preserve destination high64 for low64 writes.

- [ ] **Step 5: Verify GREEN and commit**

```powershell
cmake --build build --config Release --target r5900_ir_tests r5900_ir_validation_tests r5900_ir_executor_tests --parallel
ctest --test-dir build -C Release -R "^(r5900_ir_tests|r5900_ir_validation_tests|r5900_ir_executor_tests)$" --output-on-failure
```

```bash
git add src/recompiler/r5900_ir_executor.cpp tests/r5900_ir_executor_tests.cpp
git commit -m "feat: execute R5900 startup integer semantics"
```

---

### Task 4: Add COP1 lowering, validation, and reference semantics

**Files:**
- Modify: `src/recompiler/r5900_ir.cpp`
- Modify: `src/recompiler/r5900_ir_validation.cpp`
- Modify: `src/recompiler/r5900_ir_executor.cpp`
- Modify: `tests/r5900_ir_tests.cpp`
- Modify: `tests/r5900_ir_validation_tests.cpp`
- Modify: `tests/r5900_ir_executor_tests.cpp`

**Interfaces:**
- Consumes: Task 1 FPR/FCR/ACC state and typed IR.
- Produces: reference MTC1/CTC1/ADDA.S pipeline.

- [ ] **Step 1: Add exact COP1 encoder and lowering RED cases**

```cpp
constexpr std::uint32_t cop1_type(std::uint8_t rs, std::uint8_t rt,
                                  std::uint8_t rd, std::uint8_t sa,
                                  std::uint8_t funct) {
    return (0x11u << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) | funct;
}
```

Require:

```text
cop1_type(04,3,5,0,0) -> MoveBits32, Fpr(5) <- Gpr(3)
cop1_type(06,4,31,0,0) -> MoveBits32, Fcr31 <- Gpr(4)
cop1_type(10,2,1,0,18) -> AddF32ToAccumulator, FpAccumulator <- Fpr(1),Fpr(2)
```

`CTC1` to control index 30 must fail lowering and emit no IR.

- [ ] **Step 2: Verify RED and implement lowering**

```powershell
cmake --build build --config Release --target r5900_ir_tests --parallel
ctest --test-dir build -C Release -R "^r5900_ir_tests$" --output-on-failure
```

Add:

```cpp
R5900IrOperand fpr(std::uint8_t index) {
    R5900IrOperand op{};
    op.kind = R5900IrOperandKind::Fpr;
    op.gpr_index = index;
    return op;
}
```

Implement exact mappings above; reject `decoded.rd != 31` for CTC1 before adding any IR.

- [ ] **Step 3: Add/implement validator matrix**

Valid only:

```text
MoveBits32: Fpr(<32) <- Gpr(<32), None
MoveBits32: Fcr31(index 0) <- Gpr(<32), None
AddF32ToAccumulator: FpAccumulator(index 0) <- Fpr(<32),Fpr(<32), None
```

Reject FPR index 32, nonzero unindexed destination indices, GPR destination, immediate source, wrong counts, and non-None write modes.

```powershell
cmake --build build --config Release --target r5900_ir_validation_tests --parallel
ctest --test-dir build -C Release -R "^r5900_ir_validation_tests$" --output-on-failure
```

Expected: RED before rules, GREEN after rules.

- [ ] **Step 4: Write reference COP1 RED cases**

Include `<bit>`. Use `make_exec_ir` from Task 3:

```cpp
R5900IrExecutionState state{};
state.gpr[3].low64 = 0xdeadbeef12345678ull;
auto mtc1 = make_exec_ir(R5900IrOpcode::MoveBits32,
    {R5900IrDestinationKind::Fpr,5}, R5900IrGprWriteMode::None,
    {gpr(3)}, 0x00103000u);
expect(execute_r5900_ir({mtc1}, state).ok(), "MTC1 must execute");
expect(state.fpr[5] == 0x12345678u, "MTC1 raw copy mismatch");
```

Add CTC1 copying `0xa5a5c3c3` to fcr31. Add ADDA.S with raw `1.5f` and `2.25f` expecting raw `3.75f`; set fcr31 to `0x12345678` and prove ADDA.S leaves it unchanged. Add `+0.0f + -0.0f` expecting raw `+0.0f`.

- [ ] **Step 5: Verify RED, implement reference COP1, verify GREEN**

```powershell
cmake --build build --config Release --target r5900_ir_executor_tests --parallel
ctest --test-dir build -C Release -R "^r5900_ir_executor_tests$" --output-on-failure
```

Implement:

```cpp
case R5900IrOpcode::MoveBits32: {
    const auto raw = static_cast<std::uint32_t>(state.gpr[ir.inputs[0].gpr_index].low64);
    if (ir.destination->kind == R5900IrDestinationKind::Fpr)
        state.fpr[ir.destination->index] = raw;
    else
        state.fcr31 = raw;
    break;
}
case R5900IrOpcode::AddF32ToAccumulator: {
    const float a = std::bit_cast<float>(state.fpr[ir.inputs[0].gpr_index]);
    const float b = std::bit_cast<float>(state.fpr[ir.inputs[1].gpr_index]);
    state.fp_acc = std::bit_cast<std::uint32_t>(a + b);
    break;
}
```

Then:

```powershell
cmake --build build --config Release --target r5900_ir_tests r5900_ir_validation_tests r5900_ir_executor_tests --parallel
ctest --test-dir build -C Release -R "^(r5900_ir_tests|r5900_ir_validation_tests|r5900_ir_executor_tests)$" --output-on-failure
```

Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
git add src/recompiler/r5900_ir.cpp src/recompiler/r5900_ir_validation.cpp src/recompiler/r5900_ir_executor.cpp tests/r5900_ir_tests.cpp tests/r5900_ir_validation_tests.cpp tests/r5900_ir_executor_tests.cpp
git commit -m "feat: add R5900 startup COP1 reference semantics"
```

---

### Task 5: Emit native non-COP1 startup semantics

**Files:**
- Modify: `src/recompiler/windows/r5900_x64_backend.cpp`
- Modify: `tests/r5900_x64_backend_windows_tests.cpp`

**Interfaces:**
- Consumes: Tasks 2-3 reference semantics.
- Produces: native ANDI/LUI/special/MTSAH/PADDUW behavior and a reusable typed backend test builder.

- [ ] **Step 1: Add backend test builder and full-state comparator**

```cpp
R5900IrInstruction make_backend_ir(R5900IrOpcode opcode,
                                   R5900IrDestination destination,
                                   R5900IrGprWriteMode mode,
                                   std::initializer_list<R5900IrOperand> inputs,
                                   std::uint32_t pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = pc;
    ir.opcode = opcode;
    ir.destination = destination;
    ir.write_mode = mode;
    ir.inputs.assign(inputs.begin(), inputs.end());
    return ir;
}
```

Extend `expect_states_equal` to compare all 32 GPR low/high values, hi/lo/hi1/lo1, sa, all 32 fpr raw values, fcr31, and fp_acc.

- [ ] **Step 2: Write native differential RED program**

Construct this vector explicitly:

```cpp
std::vector<R5900IrInstruction> program = {
    make_backend_ir(R5900IrOpcode::And64,
        {R5900IrDestinationKind::Gpr,2}, R5900IrGprWriteMode::Low64PreserveUpper64,
        {gpr(1), immediate(0x00f0)}, 0x00104000u),
    make_backend_ir(R5900IrOpcode::LoadUpperImmediateSignExtend,
        {R5900IrDestinationKind::Gpr,3}, R5900IrGprWriteMode::Low64PreserveUpper64,
        {immediate(0x8040)}, 0x00104004u),
    make_backend_ir(R5900IrOpcode::MoveGprLow64,
        {R5900IrDestinationKind::Hi,0}, R5900IrGprWriteMode::None,
        {gpr(4)}, 0x00104008u),
    make_backend_ir(R5900IrOpcode::MoveGprLow64,
        {R5900IrDestinationKind::Lo,0}, R5900IrGprWriteMode::None,
        {gpr(5)}, 0x0010400cu),
    make_backend_ir(R5900IrOpcode::MoveGprLow64,
        {R5900IrDestinationKind::Hi1,0}, R5900IrGprWriteMode::None,
        {gpr(6)}, 0x00104010u),
    make_backend_ir(R5900IrOpcode::MoveGprLow64,
        {R5900IrDestinationKind::Lo1,0}, R5900IrGprWriteMode::None,
        {gpr(7)}, 0x00104014u),
    make_backend_ir(R5900IrOpcode::ComputeMtsah,
        {R5900IrDestinationKind::Sa,0}, R5900IrGprWriteMode::None,
        {gpr(8), immediate(3)}, 0x00104018u),
    make_backend_ir(R5900IrOpcode::AddPackedU32Saturate128,
        {R5900IrDestinationKind::Gpr,11}, R5900IrGprWriteMode::Full128,
        {gpr(9), gpr(10)}, 0x0010401cu),
};
```

Initialize GPR9/10 with the four saturation lanes from Task 3. Clone initial state; run reference into `expected`, native into `actual`; compare full state.

- [ ] **Step 3: Verify RED**

```powershell
cmake --build build --config Release --target r5900_x64_backend_windows_tests --parallel
ctest --test-dir build -C Release -R "^r5900_x64_backend_windows_tests$" --output-on-failure
```

Expected: `UnsupportedOpcode`.

- [ ] **Step 4: Add derived state offsets and scalar emitters**

```cpp
constexpr std::uint32_t state_offset(std::size_t v) { return static_cast<std::uint32_t>(v); }
constexpr std::uint32_t hi_offset()  { return state_offset(offsetof(R5900IrExecutionState, hi)); }
constexpr std::uint32_t lo_offset()  { return state_offset(offsetof(R5900IrExecutionState, lo)); }
constexpr std::uint32_t hi1_offset() { return state_offset(offsetof(R5900IrExecutionState, hi1)); }
constexpr std::uint32_t lo1_offset() { return state_offset(offsetof(R5900IrExecutionState, lo1)); }
constexpr std::uint32_t sa_offset()  { return state_offset(offsetof(R5900IrExecutionState, sa)); }
```

Add `emit_store_eax_to_state` as `89 81 <disp32>`.

Emit:

```text
ANDI: load RAX source; 25 <imm32> (AND EAX,imm32); store RAX low64
LUI:  B8 <word32>; 48 98 (CDQE); store RAX low64
MTSAH: load EAX; 83 E0 07; 83 F0 <imm8>; D1 E0; store EAX to sa
MoveGprLow64: load RAX; store to offsetof-selected Hi/Lo/Hi1/Lo1
```

- [ ] **Step 5: Emit alias-safe PADDUW**

Load complete sources before any write:

```text
F3 0F 6F 81 <src0 disp32>  MOVDQU XMM0,[RCX+src0]
F3 0F 6F 89 <src1 disp32>  MOVDQU XMM1,[RCX+src1]
```

Per lane:

```text
66 0F 7E C0  MOVD EAX,XMM0
66 0F 7E CA  MOVD EDX,XMM1
01 D0        ADD EAX,EDX
19 D2        SBB EDX,EDX
09 D0        OR EAX,EDX
89 81 <disp> MOV [RCX+dest_lane],EAX
```

After lanes 0-2:

```text
66 0F 73 D8 04  PSRLDQ XMM0,4
66 0F 73 D9 04  PSRLDQ XMM1,4
```

- [ ] **Step 6: Wire opcodes, verify GREEN, commit**

Add cases for `And64`, `LoadUpperImmediateSignExtend`, `MoveGprLow64`, `ComputeMtsah`, and `AddPackedU32Saturate128`.

```powershell
cmake --build build --config Release --target r5900_x64_backend_windows_tests --parallel
ctest --test-dir build -C Release -R "^r5900_x64_backend_windows_tests$" --output-on-failure
```

Expected: PASS with full-state differential equality.

```bash
git add src/recompiler/windows/r5900_x64_backend.cpp tests/r5900_x64_backend_windows_tests.cpp
git commit -m "feat: emit R5900 startup integer x64 code"
```

---

### Task 6: Emit native COP1 semantics

**Files:**
- Modify: `src/recompiler/windows/r5900_x64_backend.cpp`
- Modify: `tests/r5900_x64_backend_windows_tests.cpp`

**Interfaces:**
- Consumes: Task 4 reference semantics and `make_backend_ir`/full-state comparator from Task 5.
- Produces: native MTC1/CTC1/ADDA.S.

- [ ] **Step 1: Write explicit COP1 differential RED program**

Include `<bit>` and add:

```cpp
std::vector<R5900IrInstruction> program = {
    make_backend_ir(R5900IrOpcode::MoveBits32,
        {R5900IrDestinationKind::Fpr,5}, R5900IrGprWriteMode::None,
        {gpr(3)}, 0x00105000u),
    make_backend_ir(R5900IrOpcode::MoveBits32,
        {R5900IrDestinationKind::Fpr,6}, R5900IrGprWriteMode::None,
        {gpr(4)}, 0x00105004u),
    make_backend_ir(R5900IrOpcode::MoveBits32,
        {R5900IrDestinationKind::Fcr31,0}, R5900IrGprWriteMode::None,
        {gpr(7)}, 0x00105008u),
    make_backend_ir(R5900IrOpcode::AddF32ToAccumulator,
        {R5900IrDestinationKind::FpAccumulator,0}, R5900IrGprWriteMode::None,
        {fpr(5), fpr(6)}, 0x0010500cu),
};
```

Add an `fpr(index)` test helper if the backend test file does not already have one:

```cpp
R5900IrOperand fpr(std::uint8_t index) {
    R5900IrOperand op{};
    op.kind = R5900IrOperandKind::Fpr;
    op.gpr_index = index;
    return op;
}
```

Set GPR3 low32 to raw `1.5f`, GPR4 low32 to raw `2.25f`, and GPR7 low32 to `0xa5a5c3c3`. Run reference/native and require full-state equality with fp_acc raw `3.75f`. Add a second program with `+0.0f`/`-0.0f` and a nonzero preexisting fcr31 that remains unchanged by ADDA.S.

- [ ] **Step 2: Verify RED**

```powershell
cmake --build build --config Release --target r5900_x64_backend_windows_tests --parallel
ctest --test-dir build -C Release -R "^r5900_x64_backend_windows_tests$" --output-on-failure
```

Expected: unsupported COP1 IR opcode.

- [ ] **Step 3: Add derived COP1 offsets and raw move emitter**

```cpp
constexpr std::uint32_t fpr_offset(std::uint8_t index) {
    return state_offset(offsetof(R5900IrExecutionState, fpr) +
                        static_cast<std::size_t>(index) * sizeof(std::uint32_t));
}
constexpr std::uint32_t fcr31_offset() { return state_offset(offsetof(R5900IrExecutionState, fcr31)); }
constexpr std::uint32_t fp_acc_offset() { return state_offset(offsetof(R5900IrExecutionState, fp_acc)); }
```

`MoveBits32` loads EAX from GPR low32 and stores EAX to validated FPR or FCR31 destination.

- [ ] **Step 4: Emit ADDA.S**

```text
F3 0F 10 81 <fs disp32>   MOVSS XMM0,[RCX+FPRfs]
F3 0F 58 81 <ft disp32>   ADDSS XMM0,[RCX+FPRft]
F3 0F 11 81 <acc disp32>  MOVSS [RCX+FP_ACC],XMM0
```

Do not touch MXCSR or fcr31.

- [ ] **Step 5: Wire cases, verify GREEN, commit**

```powershell
cmake --build build --config Release --target r5900_x64_backend_windows_tests --parallel
ctest --test-dir build -C Release -R "^r5900_x64_backend_windows_tests$" --output-on-failure
```

Expected: PASS; all modeled state matches reference.

```bash
git add src/recompiler/windows/r5900_x64_backend.cpp tests/r5900_x64_backend_windows_tests.cpp
git commit -m "feat: emit R5900 startup COP1 x64 code"
```

---

### Task 7: Integrate dispatcher, synthetic 74-instruction startup, optional real ELF validation, and docs

**Files:**
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Modify: `tests/r5900_block_dispatcher_windows_tests.cpp`
- Modify: `README.md`
- Modify: `docs/PROGRESS.md`

**Interfaces:**
- Consumes: complete Tasks 1-6 native subset.
- Produces: exact ControlFlow stop at `0x00100130`, no delay slot, optional user-supplied ELF validation, final CI evidence.

- [ ] **Step 1: Add complete synthetic encoders to dispatcher tests**

```cpp
constexpr std::uint32_t mmi_type(std::uint8_t rs, std::uint8_t rt,
                                 std::uint8_t rd, std::uint8_t sa,
                                 std::uint8_t funct) {
    return (0x1cu << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) | funct;
}

constexpr std::uint32_t cop1_type(std::uint8_t rs, std::uint8_t rt,
                                  std::uint8_t rd, std::uint8_t sa,
                                  std::uint8_t funct) {
    return (0x11u << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) | funct;
}
```

- [ ] **Step 2: Build the exact 74-instruction synthetic prefix**

```cpp
std::vector<std::uint32_t> make_startup_prefix_words(std::uint32_t base) {
    std::vector<std::uint32_t> words;
    for (std::uint8_t rd = 1; rd <= 30; ++rd)
        words.push_back(mmi_type(0,0,rd,0x10,0x28));

    words.push_back(r_type(0,0,0,0,0x11));
    words.push_back(r_type(0,0,0,0,0x13));
    words.push_back(mmi_type(0,0,0,0,0x11));
    words.push_back(mmi_type(0,0,0,0,0x13));
    words.push_back(i_type(0x01,0,0x19,0));
    words.push_back(r_type(0,0,0,16,0x0f));

    for (std::uint8_t fs = 0; fs < 32; ++fs)
        words.push_back(cop1_type(0x04,0,fs,0,0));

    words.push_back(cop1_type(0x06,0,31,0,0));
    words.push_back(cop1_type(0x10,0,0,0,0x18));
    words.push_back(i_type(0x0f,0,2,0x004e));
    words.push_back(i_type(0x0d,2,2,0x2680));
    words.push_back(i_type(0x0f,0,3,0x01ec));
    words.push_back(i_type(0x0d,3,3,0xea00));

    expect(words.size() == 74u, "startup fixture count mismatch");
    expect(base + static_cast<std::uint32_t>(words.size() * 4u) == 0x00100130u,
           "startup fixture boundary mismatch");
    words.push_back(i_type(0x04,0,0,1));
    words.push_back(i_type(0x09,0,4,1));
    return words;
}
```

Use `base = 0x00100008`.

- [ ] **Step 3: Write exact dispatcher RED assertions**

Run one block, `max_instructions = 128`, and require:

```cpp
expect(result.reason == R5900DispatchStopReason::ControlFlow, "must stop before BEQ");
expect(result.next_pc == 0x00100130u, "BEQ PC mismatch");
expect(result.blocks_executed == 1u && result.instructions_executed == 74u,
       "must execute 74 instructions");
expect(state.gpr[2].low64 == 0x00000000004e2680ull, "r2 mismatch");
expect(state.gpr[3].low64 == 0x0000000001ecea00ull, "r3 mismatch");
expect(state.gpr[4].low64 == 0u, "delay slot executed unexpectedly");
expect(state.hi == 0 && state.lo == 0 && state.hi1 == 0 && state.lo1 == 0,
       "special state mismatch");
expect(state.sa == 0 && state.fcr31 == 0 && state.fp_acc == 0,
       "SA/COP1 state mismatch");
```

Also require every FPR raw value is zero and a sentinel GPR31 is unchanged.

- [ ] **Step 4: Verify RED**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_windows_tests --parallel
ctest --test-dir build -C Release -R "^r5900_block_dispatcher_windows_tests$" --output-on-failure
```

Expected: unsupported first new startup instruction.

- [ ] **Step 5: Expand eligibility exactly**

```cpp
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
```

Do not change other dispatcher logic. Change old ANDI-unsupported fixtures to use `XORI` as the unsupported sentinel; change the standalone ANDI test to a positive native execution test.

- [ ] **Step 6: Verify synthetic GREEN**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_windows_tests --parallel
ctest --test-dir build -C Release -R "^r5900_block_dispatcher_windows_tests$" --output-on-failure
```

Expected: PASS, exact 74 instructions, exact BEQ boundary, no delay slot.

- [ ] **Step 7: Add optional external ELF validation without CI game data**

Include `<fstream>`, change `main` to `int main(int argc, char** argv)`, and add:

```cpp
Bytes read_binary_file(const char* path) {
    std::ifstream input(path, std::ios::binary);
    expect(static_cast<bool>(input), "external ELF must open");
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    expect(end > 0, "external ELF must be non-empty");
    const auto size = static_cast<std::size_t>(end);
    input.seekg(0, std::ios::beg);
    Bytes bytes(size);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    expect(input.gcount() == static_cast<std::streamsize>(size), "external ELF read incomplete");
    return bytes;
}
```

Add:

```cpp
void validate_external_startup(const char* path) {
    const auto parsed = b3r::recompiler::parse_ps2_elf(read_binary_file(path));
    expect(parsed.ok(), "external ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "external ELF must map");
    b3r::recompiler::R5900BlockDispatcherOptions options{};
    options.block_options.max_instructions = 256;
    b3r::recompiler::R5900BlockDispatcher dispatcher(*built.memory, options);
    b3r::recompiler::R5900IrExecutionState state{};
    const auto result = dispatcher.run(0x00100008u, state, 1u);
    expect(result.reason == b3r::recompiler::R5900DispatchStopReason::ControlFlow,
           "real startup stop reason mismatch");
    expect(result.next_pc == 0x00100130u, "real startup boundary mismatch");
    expect(result.instructions_executed == 74u, "real startup count mismatch");
    expect(state.gpr[2].low64 == 0x00000000004e2680ull, "real r2 mismatch");
    expect(state.gpr[3].low64 == 0x0000000001ecea00ull, "real r3 mismatch");
    expect(state.gpr[4].low64 == 0u, "real delay slot executed");
    std::cout << "REAL_ELF_STARTUP_VALIDATED start=0x00100008 stop=0x00100130 instructions=74\n";
}
```

At the end:

```cpp
if (argc == 2) validate_external_startup(argv[1]);
else if (argc != 1) fail("usage: r5900_block_dispatcher_windows_tests.exe [external-elf-path]");
```

CTest continues to pass no path.

- [ ] **Step 8: Run full regression**

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\frame_pacer_windows_tests.exe
.\build\Release\Burnout3PacingProbe.exe --seconds 1
```

Expected: all 28 CTest targets PASS and pacing probes remain green.

- [ ] **Step 9: Run external validation only if the supplied ELF is on the Windows host**

```powershell
.\build\Release\r5900_block_dispatcher_windows_tests.exe "D:\Games\Burnout3\SLUS_210.50"
```

Required evidence:

```text
REAL_ELF_STARTUP_VALIDATED start=0x00100008 stop=0x00100130 instructions=74
r5900_block_dispatcher_windows_tests: PASS
```

If this exact run is not performed successfully, external validation remains pending.

- [ ] **Step 10: Update status docs based on actual evidence**

`README.md` must state the new narrow native startup capability while retaining explicit limitations: no native branches/delay slots, guest loads/stores, syscall HLE, graphics, audio, input, game boot, menu, or gameplay.

`docs/PROGRESS.md` uses `EXTERNALLY_VALIDATED` only after Step 9 succeeds. Otherwise use `CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION`. Record the final branch-head Windows CI run ID and synthetic result.

- [ ] **Step 11: Commit/push/final CI**

```bash
git add src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_windows_tests.cpp README.md docs/PROGRESS.md
git commit -m "feat: execute R5900 startup prefix natively"
git push origin feature/r5900-startup-execution-v0
```

Required final gate: branch-head Windows Server 2022/MSVC 19.44 workflow succeeds through configure/build/test/pacing/package. Do not merge into `main`; integration remains a separate user decision.

---

## Completion Checklist

- [ ] GPR ABI preserved and checked.
- [ ] HI/LO/HI1/LO1/SA/FPR/FCR31/FP_ACC modeled.
- [ ] New IR shapes validate centrally.
- [ ] All startup instructions lower with provenance.
- [ ] Reference semantics pass integer/MMI/COP1 edge cases.
- [ ] Native semantics match reference across all modeled state.
- [ ] PADDUW is four-lane unsigned saturating and alias-safe.
- [ ] ADDA.S remains within its explicit v0 FP contract.
- [ ] Dispatcher executes 74 synthetic instructions and stops at `0x00100130` before BEQ/delay slot.
- [ ] Existing cache/budget/stale/failure/trap/control-flow regressions stay green.
- [ ] Optional real ELF validation never commits/uploads game data.
- [ ] Documentation distinguishes synthetic CI from actual real-ELF validation.
- [ ] Final Windows CI is green at branch head.
