# R5900 Startup Execution v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute the first 74-instruction straight-line Burnout 3 EE startup prefix natively on Windows from guest PC `0x00100008` and stop before the first `BEQ` at `0x00100130`.

**Architecture:** Extend the current single `R5900IrExecutionState` by appending special-register and narrow COP1 state while keeping `gpr[32]` at offset zero. Generalize IR destinations, validate every new semantic opcode centrally, implement the same semantics first in the deterministic reference executor and then in hand-emitted Windows x86-64/SSE code, and expand dispatcher eligibility only after differential tests are green.

**Tech Stack:** C++20, CMake, Visual Studio 2022/MSVC 19.44, Windows x64 ABI, SSE/SSE2 machine code emitted manually, GitHub Actions Windows Server 2022.

**Spec:** `docs/superpowers/specs/2026-09-04-r5900-startup-execution-v0-design.md`

## Global Constraints

- `R5900IrExecutionState` remains the single state object shared by the reference executor and native block ABI.
- `gpr[32]` remains the first state field; `offsetof(R5900IrExecutionState, gpr) == 0` and `sizeof(R5900IrGprValue) == 16` are permanent ABI assertions.
- New x64 state displacements come from `offsetof(...)`; appended-state offsets are never hand-maintained numeric constants.
- FPRs and the FP accumulator are raw 32-bit IEEE-754 bit patterns.
- `CTC1` v0 accepts only FCR31 and does not map EE FCR31 into host MXCSR.
- `ADDA.S` v0 covers finite normal operands and zero cases required by the observed startup. It does not claim EE-exact NaN, denormal, overflow, underflow, exception-flag, or rounding-mode behavior and must not mutate `fcr31`.
- `SYNC` is the only new instruction intentionally lowered to a semantic no-op.
- Dispatcher v0 still executes no branch/jump, trap/syscall, unsupported instruction, or delay slot.
- Existing cache fingerprinting, stale replacement, RW -> RX protection, instruction-cache flush, RAII ownership, and partial-progress behavior remain unchanged.
- No proprietary Burnout 3 bytes, dumps, assets, symbols, or derived binary blobs are committed. All repository tests use synthetic ISA encodings.
- Every task uses RED -> GREEN and ends in a focused commit.

---

## File Structure

- `src/recompiler/r5900_ir.h` — IR destination/operand/write/opcode model.
- `src/recompiler/r5900_ir.cpp` — decoder-to-IR lowering.
- `src/recompiler/r5900_ir_validation.cpp` — structural validator and error diagnostics.
- `src/recompiler/r5900_ir_executor.h` — modeled EE state shared with native code.
- `src/recompiler/r5900_ir_executor.cpp` — reference semantics.
- `src/recompiler/windows/r5900_x64_backend.cpp` — Windows x64/SSE machine-code emitter.
- `src/recompiler/windows/r5900_block_dispatcher.cpp` — native-dispatch eligibility/boundaries/cache bridge.
- `tests/r5900_ir_tests.cpp` — lowering tests.
- `tests/r5900_ir_validation_tests.cpp` — validation matrix.
- `tests/r5900_ir_executor_tests.cpp` — reference semantics.
- `tests/r5900_x64_backend_windows_tests.cpp` — native and differential semantics.
- `tests/r5900_block_dispatcher_windows_tests.cpp` — synthetic startup and optional external ELF validation.
- `README.md`, `docs/PROGRESS.md` — honest milestone/evidence reporting.

---

### Task 1: Generalize IR destinations and append execution state while preserving existing behavior

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
- Consumes: current NOP/ADDU/ADDIU/ORI IR behavior.
- Produces: typed destinations, FPR operand kind, `Full128`, new semantic opcode identities, and appended architectural state without implementing new runtime semantics yet.

- [ ] **Step 1: Write the failing model/ABI tests**

In `tests/r5900_x64_backend_windows_tests.cpp`, include `<cstddef>` and add:

```cpp
static_assert(std::is_standard_layout_v<R5900IrExecutionState>);
static_assert(sizeof(R5900IrGprValue) == 16u);
static_assert(offsetof(R5900IrExecutionState, gpr) == 0u);
static_assert(offsetof(R5900IrExecutionState, hi) >= 32u * sizeof(R5900IrGprValue));
static_assert(offsetof(R5900IrExecutionState, fpr) > offsetof(R5900IrExecutionState, sa));
static_assert(offsetof(R5900IrExecutionState, fp_acc) > offsetof(R5900IrExecutionState, fcr31));
```

In each IR test file, replace GPR-only test construction with this explicit helper:

```cpp
R5900IrDestination gpr_destination(std::uint8_t index) {
    return {R5900IrDestinationKind::Gpr, index};
}
```

Add a model probe to `tests/r5900_ir_validation_tests.cpp`:

```cpp
R5900IrInstruction probe{};
probe.destination = R5900IrDestination{R5900IrDestinationKind::Fpr, 3u};
probe.write_mode = R5900IrGprWriteMode::Full128;
R5900IrOperand source{};
source.kind = R5900IrOperandKind::Fpr;
source.gpr_index = 4u;
```

- [ ] **Step 2: Verify RED**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target r5900_ir_tests r5900_ir_validation_tests r5900_ir_executor_tests r5900_x64_backend_windows_tests --parallel
```

Expected: compile errors for missing destination kinds, FPR operand kind, `Full128`, and appended state members.

- [ ] **Step 3: Implement the model exactly**

In `src/recompiler/r5900_ir.h`:

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

Change:

```cpp
std::optional<R5900IrDestination> destination{};
```

In `src/recompiler/r5900_ir_executor.h`, append without reordering `gpr`:

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

Adapt current GPR destinations in `r5900_ir.cpp`:

```cpp
void set_low64_destination(R5900IrInstruction& ir, std::uint8_t index) {
    ir.destination = R5900IrDestination{R5900IrDestinationKind::Gpr, index};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
}
```

Update existing validator/test construction from `R5900IrRegister` to `R5900IrDestination`. For `AddWordSignExtend` and `Or64`, validator must additionally reject any destination kind other than `Gpr`.

- [ ] **Step 4: Verify GREEN with no semantic expansion**

```powershell
cmake --build build --config Release --target r5900_ir_tests r5900_ir_validation_tests r5900_ir_executor_tests r5900_x64_backend_windows_tests --parallel
ctest --test-dir build -C Release -R "^(r5900_ir_tests|r5900_ir_validation_tests|r5900_ir_executor_tests|r5900_x64_backend_windows_tests)$" --output-on-failure
```

Expected: all four PASS; NOP/ADDU/ADDIU/ORI results are unchanged.

- [ ] **Step 5: Commit**

```bash
git add src/recompiler/r5900_ir.h src/recompiler/r5900_ir.cpp src/recompiler/r5900_ir_validation.cpp src/recompiler/r5900_ir_executor.h tests/r5900_ir_tests.cpp tests/r5900_ir_validation_tests.cpp tests/r5900_ir_executor_tests.cpp tests/r5900_x64_backend_windows_tests.cpp
git commit -m "refactor: generalize R5900 IR execution state"
```

---

### Task 2: Lower and validate integer/special/MMI startup instructions

**Files:**
- Modify: `src/recompiler/r5900_ir.cpp`
- Modify: `src/recompiler/r5900_ir_validation.cpp`
- Modify: `tests/r5900_ir_tests.cpp`
- Modify: `tests/r5900_ir_validation_tests.cpp`

**Interfaces:**
- Consumes: Task 1 model and decoder support already present for `ANDI/LUI/MTHI/MTLO/MTHI1/MTLO1/MTSAH/PADDUW/SYNC`.
- Produces: validated semantic IR for those instructions.

- [ ] **Step 1: Add complete synthetic helper functions to lowering tests**

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

void expect_lower(std::uint32_t word,
                  R5900IrOpcode opcode,
                  R5900IrDestinationKind kind,
                  std::uint8_t index,
                  std::uint32_t pc) {
    const auto result = lower_r5900_instruction(decode_r5900(word), pc);
    expect(result.ok(), "synthetic startup instruction must lower");
    expect(result.instructions.size() == 1u, "startup instruction must emit one IR op");
    const auto& ir = result.instructions.front();
    expect(ir.opcode == opcode, "startup instruction semantic opcode mismatch");
    expect(ir.destination.has_value(), "startup instruction must have destination");
    expect(ir.destination->kind == kind && ir.destination->index == index,
           "startup instruction destination mismatch");
    expect(ir.guest_pc == pc && ir.guest_raw == word,
           "startup lowering must preserve provenance");
}
```

- [ ] **Step 2: Write lowering RED cases**

Add these cases using distinct PCs:

```cpp
expect_lower(i_type(0x0c, 1, 2, 0x00ff), R5900IrOpcode::And64,
             R5900IrDestinationKind::Gpr, 2u, 0x00101000u);
expect_lower(i_type(0x0f, 0, 2, 0x8040), R5900IrOpcode::LoadUpperImmediateSignExtend,
             R5900IrDestinationKind::Gpr, 2u, 0x00101004u);
expect_lower(r_type(7, 0, 0, 0, 0x11), R5900IrOpcode::MoveGprLow64,
             R5900IrDestinationKind::Hi, 0u, 0x00101008u);
expect_lower(r_type(8, 0, 0, 0, 0x13), R5900IrOpcode::MoveGprLow64,
             R5900IrDestinationKind::Lo, 0u, 0x0010100cu);
expect_lower(mmi_type(9, 0, 0, 0, 0x11), R5900IrOpcode::MoveGprLow64,
             R5900IrDestinationKind::Hi1, 0u, 0x00101010u);
expect_lower(mmi_type(10, 0, 0, 0, 0x13), R5900IrOpcode::MoveGprLow64,
             R5900IrDestinationKind::Lo1, 0u, 0x00101014u);
expect_lower(i_type(0x01, 11, 0x19, 5), R5900IrOpcode::ComputeMtsah,
             R5900IrDestinationKind::Sa, 0u, 0x00101018u);
expect_lower(mmi_type(12, 13, 14, 0x10, 0x28), R5900IrOpcode::AddPackedU32Saturate128,
             R5900IrDestinationKind::Gpr, 14u, 0x0010101cu);
```

Then directly inspect operands/write modes:

```cpp
const auto andi = lower_r5900_instruction(decode_r5900(i_type(0x0c, 1, 2, 0x00ff)), 0x00101020u);
expect(andi.instructions[0].inputs.size() == 2u &&
       andi.instructions[0].inputs[1].kind == R5900IrOperandKind::Immediate &&
       andi.instructions[0].inputs[1].immediate == 0x00ff,
       "ANDI immediate must be zero-extended in IR");

const auto padduw = lower_r5900_instruction(decode_r5900(mmi_type(12, 13, 14, 0x10, 0x28)), 0x00101024u);
expect(padduw.instructions[0].write_mode == R5900IrGprWriteMode::Full128,
       "PADDUW must claim a full 128-bit GPR write");

const auto sync = lower_r5900_instruction(decode_r5900(r_type(0, 0, 0, 16, 0x0f)), 0x00101028u);
expect(sync.ok() && sync.instructions.size() == 1u &&
       sync.instructions[0].opcode == R5900IrOpcode::Nop,
       "SYNC must lower to semantic Nop");
```

Add GPR0 cases for `ANDI`, `LUI`, and `PADDUW` expecting a provenance-carrying `Nop`.

- [ ] **Step 3: Verify lowering RED**

```powershell
cmake --build build --config Release --target r5900_ir_tests --parallel
ctest --test-dir build -C Release -R "^r5900_ir_tests$" --output-on-failure
```

Expected: FAIL because lowering still rejects the new instructions.

- [ ] **Step 4: Implement minimal lowering**

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

Implement exact mappings:

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

`ANDI/LUI/PADDUW` writes to GPR0 use the existing discarded-write Nop path. Special-register operations with `rs == 0` still emit operations because they must overwrite nonzero special state with zero.

- [ ] **Step 5: Write validator RED cases with explicit builders**

In `tests/r5900_ir_validation_tests.cpp`, add:

```cpp
R5900IrDestination dst(R5900IrDestinationKind kind, std::uint8_t index = 0u) {
    return {kind, index};
}

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

Require valid instances for all new non-COP1 opcodes. Add negative cases for destination index 32, nonzero index on `Hi/Lo/Hi1/Lo1/Sa`, wrong destination kind, wrong operand count/kind, `Full128` on `And64`, and low64 mode on `AddPackedU32Saturate128`.

- [ ] **Step 6: Verify validator RED**

```powershell
cmake --build build --config Release --target r5900_ir_validation_tests --parallel
ctest --test-dir build -C Release -R "^r5900_ir_validation_tests$" --output-on-failure
```

Expected: FAIL because the validator has no rules for new opcodes.

- [ ] **Step 7: Implement opcode-specific validation**

Add explicit rules:

```text
And64: Gpr <- Gpr,Immediate; Low64PreserveUpper64
LoadUpperImmediateSignExtend: Gpr <- Immediate; Low64PreserveUpper64
AddPackedU32Saturate128: Gpr <- Gpr,Gpr; Full128
MoveGprLow64: Hi|Lo|Hi1|Lo1 <- Gpr; destination index 0; write mode None
ComputeMtsah: Sa <- Gpr,Immediate; destination index 0; write mode None
```

Retain current deterministic instruction-index and guest-PC diagnostics.

- [ ] **Step 8: Verify GREEN and commit**

```powershell
cmake --build build --config Release --target r5900_ir_tests r5900_ir_validation_tests --parallel
ctest --test-dir build -C Release -R "^(r5900_ir_tests|r5900_ir_validation_tests)$" --output-on-failure
```

Expected: both PASS.

```bash
git add src/recompiler/r5900_ir.cpp src/recompiler/r5900_ir_validation.cpp tests/r5900_ir_tests.cpp tests/r5900_ir_validation_tests.cpp
git commit -m "feat: lower R5900 startup integer and MMI subset"
```

---

### Task 3: Implement reference integer/special/MMI semantics

**Files:**
- Modify: `src/recompiler/r5900_ir_executor.cpp`
- Modify: `tests/r5900_ir_executor_tests.cpp`

**Interfaces:**
- Consumes: validated IR from Task 2.
- Produces: deterministic semantic oracle for all non-COP1 startup operations.

- [ ] **Step 1: Add explicit IR test builders**

In `tests/r5900_ir_executor_tests.cpp`, add:

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

- [ ] **Step 2: Write reference RED cases**

For `ANDI`:

```cpp
R5900IrExecutionState state{};
state.gpr[1] = {0xffff0000123456ffull, 0x9999888877776666ull};
state.gpr[2].high64 = 0x1122334455667788ull;
auto ir = make_ir(R5900IrOpcode::And64,
                  {R5900IrDestinationKind::Gpr, 2u},
                  R5900IrGprWriteMode::Low64PreserveUpper64,
                  {gpr(1), immediate(0x00f0)}, 0x00102000u);
expect(execute_r5900_ir({ir}, state).ok(), "ANDI reference IR must execute");
expect(state.gpr[2].low64 == 0x00000000000000f0ull, "ANDI result mismatch");
expect(state.gpr[2].high64 == 0x1122334455667788ull, "ANDI must preserve high64");
```

For positive/negative `LUI`, expect `0x00000000004e0000` and `0xffffffff80400000` respectively while preserving destination high64.

For special moves, initialize sources to four distinct 64-bit values and require exact writes to `hi`, `lo`, `hi1`, `lo1`. For `MTSAH`, use source low bits `5` and immediate `3`, expecting `12`.

For `PADDUW`, construct source GPRs with lanes:

```text
lhs = [0xffffffff, 1, 0xfffffffe, 0x80000000]
rhs = [1,          2, 1,          0x80000000]
out = [0xffffffff, 3, 0xffffffff, 0xffffffff]
```

Add two additional tests with destination equal to lhs and destination equal to rhs. Each alias test must produce the same four output lanes.

- [ ] **Step 3: Verify RED**

```powershell
cmake --build build --config Release --target r5900_ir_executor_tests --parallel
ctest --test-dir build -C Release -R "^r5900_ir_executor_tests$" --output-on-failure
```

Expected: new state assertions fail because executor switch has no new semantics.

- [ ] **Step 4: Implement explicit typed readers and arithmetic**

In `src/recompiler/r5900_ir_executor.cpp` add:

```cpp
std::uint64_t read_gpr_or_immediate64(const R5900IrOperand& operand,
                                      const R5900IrExecutionState& state) {
    if (operand.kind == R5900IrOperandKind::Immediate) {
        return static_cast<std::uint64_t>(operand.immediate);
    }
    return state.gpr[operand.gpr_index].low64;
}

std::uint32_t lane32(const R5900IrGprValue& value, unsigned lane) {
    return lane < 2u
        ? static_cast<std::uint32_t>(value.low64 >> (lane * 32u))
        : static_cast<std::uint32_t>(value.high64 >> ((lane - 2u) * 32u));
}

std::uint32_t sat_add_u32(std::uint32_t lhs, std::uint32_t rhs) {
    const std::uint64_t sum = static_cast<std::uint64_t>(lhs) + rhs;
    return sum > 0xffffffffull ? 0xffffffffu : static_cast<std::uint32_t>(sum);
}
```

`PADDUW` must copy both source `R5900IrGprValue`s to locals before writing destination, compute all four lanes, then pack lanes 0/1 into `low64` and lanes 2/3 into `high64`.

Implement special destination writes with a switch over `Hi/Lo/Hi1/Lo1`. Implement `MTSAH` as `((gpr.low64 & 7) ^ (imm & 7)) << 1`. Implement `ANDI` as 64-bit AND with the already zero-extended IR immediate. Implement `LUI` as `(std::int64_t)(std::int32_t)((std::uint32_t)imm << 16)` stored into low64. `Nop` continues to cover SYNC.

- [ ] **Step 5: Verify GREEN and commit**

```powershell
cmake --build build --config Release --target r5900_ir_tests r5900_ir_validation_tests r5900_ir_executor_tests --parallel
ctest --test-dir build -C Release -R "^(r5900_ir_tests|r5900_ir_validation_tests|r5900_ir_executor_tests)$" --output-on-failure
```

Expected: all PASS.

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
- Consumes: raw FPR/FCR31/FP_ACC state and typed IR model from Task 1.
- Produces: complete reference pipeline for `MTC1`, `CTC1 r31`, and narrow `ADDA.S`.

- [ ] **Step 1: Add complete COP1 encoding/helper functions and lowering RED cases**

In `tests/r5900_ir_tests.cpp`:

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
cop1_type(0x04,3,5,0,0)    -> MoveBits32, Fpr(5) <- Gpr(3)
cop1_type(0x06,4,31,0,0)   -> MoveBits32, Fcr31 <- Gpr(4)
cop1_type(0x10,2,1,0,0x18) -> AddF32ToAccumulator, FpAccumulator <- Fpr(1),Fpr(2)
```

Require `cop1_type(0x06,4,30,0,0)` to fail lowering with `UnsupportedInstruction` and no partial IR.

- [ ] **Step 2: Verify lowering RED, then implement lowering**

```powershell
cmake --build build --config Release --target r5900_ir_tests --parallel
ctest --test-dir build -C Release -R "^r5900_ir_tests$" --output-on-failure
```

Expected: FAIL before implementation.

Add:

```cpp
R5900IrOperand fpr(std::uint8_t index) {
    R5900IrOperand operand{};
    operand.kind = R5900IrOperandKind::Fpr;
    operand.gpr_index = index;
    return operand;
}
```

Lower exactly:

```text
MTC1  -> MoveBits32, Fpr(decoded.rd) <- Gpr(decoded.rt)
CTC1  -> MoveBits32, Fcr31 <- Gpr(decoded.rt), only when decoded.rd == 31
ADDA.S -> AddF32ToAccumulator, FpAccumulator <- Fpr(decoded.rd),Fpr(decoded.rt)
```

- [ ] **Step 3: Write validator RED cases and implement rules**

Valid shapes:

```text
MoveBits32: Fpr(index<32) <- Gpr(index<32), write mode None
MoveBits32: Fcr31(index=0) <- Gpr(index<32), write mode None
AddF32ToAccumulator: FpAccumulator(index=0) <- Fpr(index<32),Fpr(index<32), write mode None
```

Reject FPR index 32, nonzero Fcr31/FpAccumulator index, GPR destination, immediate source, wrong operand counts, or any non-None write mode.

Run before and after implementation:

```powershell
cmake --build build --config Release --target r5900_ir_validation_tests --parallel
ctest --test-dir build -C Release -R "^r5900_ir_validation_tests$" --output-on-failure
```

Expected: RED before rules, GREEN after rules.

- [ ] **Step 4: Write reference COP1 RED cases**

Include `<bit>` in `tests/r5900_ir_executor_tests.cpp`. Build explicit IR with the `make_ir` helper from Task 3:

```cpp
R5900IrExecutionState state{};
state.gpr[3].low64 = 0xdeadbeef12345678ull;
auto mtc1 = make_ir(R5900IrOpcode::MoveBits32,
                    {R5900IrDestinationKind::Fpr, 5u},
                    R5900IrGprWriteMode::None,
                    {gpr(3)}, 0x00103000u);
expect(execute_r5900_ir({mtc1}, state).ok(), "MTC1 reference IR must execute");
expect(state.fpr[5] == 0x12345678u, "MTC1 must copy raw low32 bits");
```

For CTC1, copy `0xa5a5c3c3` to `fcr31`. For ADDA.S, place raw bits of `1.5f` and `2.25f` in two FPRs and expect raw bits of `3.75f` in `fp_acc`; set `fcr31 = 0x12345678` before ADDA.S and assert it is unchanged. Add `+0.0f + -0.0f` expecting raw `+0.0f`.

- [ ] **Step 5: Verify RED, implement reference COP1, verify GREEN**

Before implementation:

```powershell
cmake --build build --config Release --target r5900_ir_executor_tests --parallel
ctest --test-dir build -C Release -R "^r5900_ir_executor_tests$" --output-on-failure
```

Expected: FAIL.

In `src/recompiler/r5900_ir_executor.cpp`, include `<bit>` and implement:

```cpp
case R5900IrOpcode::MoveBits32: {
    const auto raw = static_cast<std::uint32_t>(state.gpr[ir.inputs[0].gpr_index].low64);
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

After implementation:

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

### Task 5: Emit native x64 for integer/special/MMI startup semantics

**Files:**
- Modify: `src/recompiler/windows/r5900_x64_backend.cpp`
- Modify: `tests/r5900_x64_backend_windows_tests.cpp`

**Interfaces:**
- Consumes: validated/reference semantics from Tasks 2-3.
- Produces: native `ANDI/LUI/MTHI/MTLO/MTHI1/MTLO1/MTSAH/PADDUW/SYNC` behavior.

- [ ] **Step 1: Extend differential state comparison and write RED program**

Extend `expect_states_equal` to compare all modeled fields:

```cpp
for (std::size_t i = 0; i < expected.gpr.size(); ++i) {
    expect(expected.gpr[i].low64 == actual.gpr[i].low64 &&
           expected.gpr[i].high64 == actual.gpr[i].high64, message);
}
expect(expected.hi == actual.hi && expected.lo == actual.lo, message);
expect(expected.hi1 == actual.hi1 && expected.lo1 == actual.lo1, message);
expect(expected.sa == actual.sa, message);
for (std::size_t i = 0; i < expected.fpr.size(); ++i) {
    expect(expected.fpr[i] == actual.fpr[i], message);
}
expect(expected.fcr31 == actual.fcr31 && expected.fp_acc == actual.fp_acc, message);
```

Build one IR vector with `And64`, two `LoadUpperImmediateSignExtend` cases, four `MoveGprLow64` destinations, `ComputeMtsah`, one saturating `AddPackedU32Saturate128`, and `Nop`. Clone initial state to `expected` and `actual`, run reference on `expected`, compile/run native on `actual`, and compare full state.

- [ ] **Step 2: Verify RED**

```powershell
cmake --build build --config Release --target r5900_x64_backend_windows_tests --parallel
ctest --test-dir build -C Release -R "^r5900_x64_backend_windows_tests$" --output-on-failure
```

Expected: backend returns `UnsupportedOpcode`.

- [ ] **Step 3: Add `offsetof`-derived state helpers**

In `r5900_x64_backend.cpp`:

```cpp
constexpr std::uint32_t state_offset(std::size_t value) {
    return static_cast<std::uint32_t>(value);
}
constexpr std::uint32_t hi_offset()  { return state_offset(offsetof(R5900IrExecutionState, hi)); }
constexpr std::uint32_t lo_offset()  { return state_offset(offsetof(R5900IrExecutionState, lo)); }
constexpr std::uint32_t hi1_offset() { return state_offset(offsetof(R5900IrExecutionState, hi1)); }
constexpr std::uint32_t lo1_offset() { return state_offset(offsetof(R5900IrExecutionState, lo1)); }
constexpr std::uint32_t sa_offset()  { return state_offset(offsetof(R5900IrExecutionState, sa)); }
```

Add 32-bit store helper:

```cpp
void emit_store_eax_to_state(std::vector<std::uint8_t>& bytes, std::uint32_t displacement) {
    bytes.push_back(0x89u);
    bytes.push_back(0x81u);
    emit_u32(bytes, displacement);
}
```

- [ ] **Step 4: Emit simple integer/special operations**

- `MoveGprLow64`: load source low64 into RAX; store to `hi/lo/hi1/lo1` offset selected from validated destination kind.
- `And64`: load source low64 into RAX; emit `AND EAX,imm32` as `0x25 <imm32>`; store RAX to GPR low64. Because the IR immediate is a zero-extended 16-bit value, the architectural result must have bits 16-63 clear.
- `LoadUpperImmediateSignExtend`: compute `std::uint32_t word = (std::uint32_t(ir.inputs[0].immediate) & 0xffffu) << 16u`; emit `MOV EAX,word`, `CDQE` (`48 98`), store RAX.
- `ComputeMtsah`: load EAX from source GPR low32; emit `83 E0 07`, `83 F0 imm8`, `D1 E0`, then store EAX to `sa`.
- `Nop`: no instruction-specific bytes.

- [ ] **Step 5: Emit alias-safe PADDUW**

Load both complete 128-bit sources before any destination store:

```text
MOVDQU XMM0,[RCX+src0]  F3 0F 6F 81 <disp32>
MOVDQU XMM1,[RCX+src1]  F3 0F 6F 89 <disp32>
```

For each lane:

```text
MOVD EAX,XMM0           66 0F 7E C0
MOVD EDX,XMM1           66 0F 7E CA
ADD EAX,EDX             01 D0
SBB EDX,EDX             19 D2
OR  EAX,EDX             09 D0
MOV [RCX+dst_lane],EAX  89 81 <disp32>
```

After lanes 0-2:

```text
PSRLDQ XMM0,4           66 0F 73 D8 04
PSRLDQ XMM1,4           66 0F 73 D9 04
```

`SBB EDX,EDX` converts carry into `0xffffffff`; OR saturates the overflowing result. Source aliasing is safe because XMM0/XMM1 contain both source GPRs before stores begin.

- [ ] **Step 6: Wire compile switch, verify GREEN, commit**

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

### Task 6: Emit native COP1 startup operations and prove full-state equivalence

**Files:**
- Modify: `src/recompiler/windows/r5900_x64_backend.cpp`
- Modify: `tests/r5900_x64_backend_windows_tests.cpp`

**Interfaces:**
- Consumes: Task 4 COP1 reference semantics and Task 5 full-state comparator.
- Produces: native `MTC1/CTC1/ADDA.S` support.

- [ ] **Step 1: Write explicit native COP1 RED program**

Include `<bit>` in the test. Build these IR operations with the typed test builder used by current backend tests after Task 1:

```text
MoveBits32: Fpr(5) <- Gpr(3)
MoveBits32: Fpr(6) <- Gpr(4)
MoveBits32: Fcr31 <- Gpr(7)
AddF32ToAccumulator: FpAccumulator <- Fpr(5),Fpr(6)
```

Set GPR3 low32 to raw `1.5f`, GPR4 low32 to raw `2.25f`, and GPR7 low32 to `0xa5a5c3c3`. Reference execution must produce FPR5/FPR6 raw copies, FCR31 `0xa5a5c3c3`, and FP accumulator raw `3.75f`. Native result must match all modeled state fields. Add a second program for `+0.0f + -0.0f` and preserve an existing nonzero FCR31 across ADDA.S.

- [ ] **Step 2: Verify RED**

```powershell
cmake --build build --config Release --target r5900_x64_backend_windows_tests --parallel
ctest --test-dir build -C Release -R "^r5900_x64_backend_windows_tests$" --output-on-failure
```

Expected: `UnsupportedOpcode` for `MoveBits32` or `AddF32ToAccumulator`.

- [ ] **Step 3: Add FPR/control offsets and raw move emitter**

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

`MoveBits32` loads EAX from the validated GPR source low32 and stores EAX either to `fpr_offset(destination.index)` or `fcr31_offset()`.

- [ ] **Step 4: Emit scalar single-precision ADDA.S**

Use exactly:

```text
MOVSS XMM0,[RCX+fpr(fs)]   F3 0F 10 81 <disp32>
ADDSS XMM0,[RCX+fpr(ft)]   F3 0F 58 81 <disp32>
MOVSS [RCX+fp_acc],XMM0    F3 0F 11 81 <disp32>
```

Do not read/write MXCSR and do not modify `fcr31`.

- [ ] **Step 5: Wire cases, verify GREEN, commit**

Add compile-switch cases for `MoveBits32` and `AddF32ToAccumulator`.

```powershell
cmake --build build --config Release --target r5900_x64_backend_windows_tests --parallel
ctest --test-dir build -C Release -R "^r5900_x64_backend_windows_tests$" --output-on-failure
```

Expected: PASS; native/reference states match across 32 GPRs, HI/LO/HI1/LO1, SA, 32 FPRs, FCR31, and FP accumulator.

```bash
git add src/recompiler/windows/r5900_x64_backend.cpp tests/r5900_x64_backend_windows_tests.cpp
git commit -m "feat: emit R5900 startup COP1 x64 code"
```

---

### Task 7: Integrate dispatcher, execute the 74-instruction synthetic startup, validate external ELF when available, and record evidence

**Files:**
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Modify: `tests/r5900_block_dispatcher_windows_tests.cpp`
- Modify: `README.md`
- Modify: `docs/PROGRESS.md`

**Interfaces:**
- Consumes: complete Task 1-6 lowering/validation/reference/native support.
- Produces: exact dispatcher stop at `0x00100130`, repeatable optional external validation, final documentation and CI gate.

- [ ] **Step 1: Add synthetic MMI/COP1 encoders to dispatcher tests**

Add the same `mmi_type(...)` and `cop1_type(...)` implementations used in decoder/lowering tests; both operate only on synthetic ISA fields.

- [ ] **Step 2: Build the exact 74-instruction synthetic prefix**

Add:

```cpp
std::vector<std::uint32_t> make_startup_prefix_words(std::uint32_t base) {
    std::vector<std::uint32_t> words;

    for (std::uint8_t rd = 1u; rd <= 30u; ++rd) {
        words.push_back(mmi_type(0, 0, rd, 0x10, 0x28));
    }

    words.push_back(r_type(0, 0, 0, 0, 0x11));
    words.push_back(r_type(0, 0, 0, 0, 0x13));
    words.push_back(mmi_type(0, 0, 0, 0, 0x11));
    words.push_back(mmi_type(0, 0, 0, 0, 0x13));
    words.push_back(i_type(0x01, 0, 0x19, 0));
    words.push_back(r_type(0, 0, 0, 16, 0x0f));

    for (std::uint8_t fs = 0u; fs < 32u; ++fs) {
        words.push_back(cop1_type(0x04, 0, fs, 0, 0));
    }

    words.push_back(cop1_type(0x06, 0, 31, 0, 0));
    words.push_back(cop1_type(0x10, 0, 0, 0, 0x18));
    words.push_back(i_type(0x0f, 0, 2, 0x004e));
    words.push_back(i_type(0x0d, 2, 2, 0x2680));
    words.push_back(i_type(0x0f, 0, 3, 0x01ec));
    words.push_back(i_type(0x0d, 3, 3, 0xea00));

    expect(words.size() == 74u, "startup fixture must have 74 executable instructions");
    expect(base + static_cast<std::uint32_t>(words.size() * 4u) == 0x00100130u,
           "startup fixture BEQ boundary must be 0x00100130");

    words.push_back(i_type(0x04, 0, 0, 1u));
    words.push_back(i_type(0x09, 0, 4, 1u));
    return words;
}
```

With `base = 0x00100008`, the first 74 instructions occupy exactly `0x128` bytes and the synthetic BEQ is exactly at `0x00100130`.

- [ ] **Step 3: Write dispatcher RED assertions**

Run one block with `max_instructions = 128` and require:

```cpp
expect(result.reason == R5900DispatchStopReason::ControlFlow,
       "startup must stop before BEQ");
expect(result.next_pc == 0x00100130u,
       "startup BEQ PC mismatch");
expect(result.blocks_executed == 1u && result.instructions_executed == 74u,
       "startup must execute exactly 74 instructions");
expect(result.cache_misses == 1u && result.cache_hits == 0u,
       "first startup dispatch must compile once");
expect(state.gpr[2].low64 == 0x00000000004e2680ull,
       "startup r2 mismatch");
expect(state.gpr[3].low64 == 0x0000000001ecea00ull,
       "startup r3 mismatch");
expect(state.gpr[4].low64 == 0u,
       "BEQ delay slot must not execute");
expect(state.hi == 0u && state.lo == 0u && state.hi1 == 0u && state.lo1 == 0u,
       "startup special state mismatch");
expect(state.sa == 0u && state.fcr31 == 0u && state.fp_acc == 0u,
       "startup SA/FCR/ACC mismatch");
for (const auto raw : state.fpr) {
    expect(raw == 0u, "startup FPR must be zero");
}
```

Initialize GPR31 to a sentinel and assert it remains unchanged.

- [ ] **Step 4: Verify RED at dispatcher eligibility**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_windows_tests --parallel
ctest --test-dir build -C Release -R "^r5900_block_dispatcher_windows_tests$" --output-on-failure
```

Expected: `UnsupportedInstruction` at the first newly eligible startup instruction.

- [ ] **Step 5: Expand eligibility only to completed instructions**

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

Do not alter cache/budget/boundary logic.

- [ ] **Step 6: Replace obsolete ANDI unsupported tests**

The current prefix test that stops before `ANDI` must use an unsupported `XORI` sentinel instead. The current standalone `ANDI at entry must be unsupported` case becomes a positive native `ANDI` test asserting zero extension and unchanged high64.

- [ ] **Step 7: Verify synthetic startup GREEN**

```powershell
cmake --build build --config Release --target r5900_block_dispatcher_windows_tests --parallel
ctest --test-dir build -C Release -R "^r5900_block_dispatcher_windows_tests$" --output-on-failure
```

Expected: PASS with 74 executed instructions, exact `0x00100130` `ControlFlow` stop, and no delay-slot execution.

- [ ] **Step 8: Add optional external ELF path support to the existing test executable**

Change `main()` to `int main(int argc, char** argv)`, include `<fstream>`, and add:

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
    expect(input.gcount() == static_cast<std::streamsize>(size),
           "external ELF must read completely");
    return bytes;
}
```

Add:

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
           "real startup must stop at control flow");
    expect(result.next_pc == 0x00100130u,
           "real startup boundary must be 0x00100130");
    expect(result.blocks_executed == 1u && result.instructions_executed == 74u,
           "real startup must execute 74 native guest instructions");
    expect(state.gpr[2].low64 == 0x00000000004e2680ull,
           "real startup r2 mismatch");
    expect(state.gpr[3].low64 == 0x0000000001ecea00ull,
           "real startup r3 mismatch");
    expect(state.gpr[4].low64 == 0u,
           "real startup delay slot must not execute");

    std::cout << "REAL_ELF_STARTUP_VALIDATED start=0x00100008 stop=0x00100130 instructions=74\n";
}
```

After normal synthetic tests:

```cpp
if (argc == 2) {
    validate_external_startup(argv[1]);
} else if (argc != 1) {
    fail("usage: r5900_block_dispatcher_windows_tests.exe [external-elf-path]");
}
```

CTest invokes the executable with no external path, so CI remains fully synthetic and proprietary-data-free.

- [ ] **Step 9: Run the complete Windows regression**

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\frame_pacer_windows_tests.exe
.\build\Release\Burnout3PacingProbe.exe --seconds 1
```

Expected: all 28 CTest targets PASS; frame-pacing telemetry and the 120-frame probe remain green.

- [ ] **Step 10: Run real external validation only when the supplied ELF is present on the Windows host**

```powershell
.\build\Release\r5900_block_dispatcher_windows_tests.exe "D:\Games\Burnout3\SLUS_210.50"
```

Required output:

```text
REAL_ELF_STARTUP_VALIDATED start=0x00100008 stop=0x00100130 instructions=74
r5900_block_dispatcher_windows_tests: PASS
```

Do not infer external success from the synthetic fixture. If this executable cannot be run with the supplied file during implementation, the external validation status remains pending.

- [ ] **Step 11: Update README/PROGRESS honestly**

`README.md` must state the narrow startup subset can execute natively through the first branch boundary only after the synthetic native test is green. It must continue to say branch execution, delay slots, guest loads/stores, syscall HLE, graphics, audio, input, game boot, menu, and gameplay are not implemented.

`docs/PROGRESS.md` must use one of these evidence states based on Step 10:

```text
EXTERNALLY_VALIDATED
```

only if the real external invocation succeeded, otherwise:

```text
CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION
```

Record the final Windows CI run ID and exact 74-instruction synthetic result in either case.

- [ ] **Step 12: Commit, push, and require final CI**

```bash
git add src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_windows_tests.cpp README.md docs/PROGRESS.md
git commit -m "feat: execute R5900 startup prefix natively"
git push origin feature/r5900-startup-execution-v0
```

Required final gate: branch-head Windows Server 2022/MSVC 19.44 workflow completes successfully with configure/build/test/pacing/package stages green. Do not merge this branch into `main`; integration remains a separate user decision.

---

## Completion Checklist

- [ ] Execution-state ABI preserved: `gpr` offset zero, GPR value size 16 bytes.
- [ ] HI/LO/HI1/LO1/SA/FPR/FCR31/FP_ACC appended and compared differentially.
- [ ] Typed IR validates destination kind/index, operands, write mode, and opcode shape.
- [ ] `ANDI/LUI/MTHI/MTLO/MTHI1/MTLO1/MTSAH/PADDUW/MTC1/CTC1 r31/ADDA.S/SYNC` lower with provenance.
- [ ] Reference semantics pass edge/alias/raw-bit tests.
- [ ] Native x64 semantics match reference state bit-for-bit within the defined v0 FP contract.
- [ ] PADDUW saturates four unsigned 32-bit lanes and is alias-safe.
- [ ] Dispatcher executes exactly 74 synthetic instructions from `0x00100008` and stops at `0x00100130` before BEQ/delay slot.
- [ ] Existing cache, stale-code, budget, failure, trap, and control-flow tests remain green.
- [ ] Optional external validation never commits or uploads the supplied game executable.
- [ ] Documentation distinguishes synthetic CI validation from actual external-ELF validation.
- [ ] Final Windows CI is green at branch head.
