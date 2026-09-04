# R5900 Startup Execution v0 Design

Status: approved design, pending implementation plan
Date: 2026-09-04
Branch: `feature/r5900-startup-execution-v0`
Base: `feature/r5900-startup-decoder-v1` at `fdf701aec1cf505e4480f5b019fb301bb3054067`

## 1. Goal

Implement the smallest native-execution milestone that can execute the first straight-line startup prefix observed in an externally supplied legal Burnout 3 PS2 ELF, beginning at guest PC `0x00100008` and stopping before the first control-flow instruction at `0x00100130`.

The milestone extends the existing decoder -> IR -> reference executor -> Windows x86-64 backend -> block dispatcher pipeline. It does not add guest memory loads/stores, branch execution, delay-slot execution, syscall HLE, graphics, audio, input, game boot, menu flow, or gameplay.

No proprietary Burnout 3 executable bytes, assets, dumps, symbols, or derived binary blobs are committed to the repository. Versioned tests use synthetic ISA encodings and synthetic startup sequences only. The supplied game ELF is used only as an external validation source.

## 2. Scope

The dispatcher may execute this instruction subset when all instructions in the candidate prefix successfully lower and validate:

- `NOP`
- `ADDU`
- `ADDIU`
- `ORI`
- `ANDI`
- `LUI`
- `MTHI`
- `MTLO`
- `MTHI1`
- `MTLO1`
- `MTSAH`
- `PADDUW`
- `MTC1`
- `CTC1`
- `ADDA.S`
- `SYNC`

Existing control-flow, trap, and unsupported-instruction boundary behavior remains unchanged. In particular, the first `BEQ` at the real startup boundary remains a `ControlFlow` stop and is not executed in v0.

## 3. Architectural execution state

`R5900IrExecutionState` remains the single state object shared by the reference executor and generated native x86-64 blocks.

The existing `gpr[32]` array remains the first field. New fields are appended so current GPR offsets remain stable:

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

Required ABI invariants:

```cpp
static_assert(std::is_standard_layout_v<R5900IrExecutionState>);
static_assert(sizeof(R5900IrGprValue) == 16u);
static_assert(offsetof(R5900IrExecutionState, gpr) == 0u);
```

New backend offsets must be derived from `offsetof(...)`; no hand-maintained magic displacement constants are permitted for appended architectural fields.

FPRs and the floating-point accumulator are stored as raw IEEE-754 32-bit bit patterns rather than persistent host `float` objects.

## 4. Instruction semantics

### 4.1 PADDUW

`PADDUW rd, rs, rt` treats each 128-bit source GPR as four unsigned 32-bit lanes. Each lane computes an unsigned addition and saturates to `0xFFFFFFFF` on overflow. The destination write covers all 128 bits. Writes to GPR zero are discarded and GPR zero remains normalized.

Aliasing such as `rd == rs` or `rd == rt` must produce architecturally correct results; the implementation must not overwrite a source lane before all required source values are consumed.

### 4.2 MTHI / MTLO

`MTHI rs` copies the low 64 bits of GPR `rs` into `hi`.

`MTLO rs` copies the low 64 bits of GPR `rs` into `lo`.

### 4.3 MTHI1 / MTLO1

`MTHI1 rs` copies the low 64 bits of GPR `rs` into `hi1`.

`MTLO1 rs` copies the low 64 bits of GPR `rs` into `lo1`.

### 4.4 MTSAH

`MTSAH rs, imm` computes:

```text
sa = ((GPR[rs].low32 & 0x7) ^ (imm & 0x7)) << 1
```

Only the low three bits of the GPR source and immediate participate.

### 4.5 MTC1

`MTC1 rt, fs` copies the low 32 bits of GPR `rt` bit-for-bit into `FPR[fs]`.

No floating-point conversion is performed.

### 4.6 CTC1

For v0, only control register index 31 is supported. `CTC1 rt, 31` copies the low 32 bits of GPR `rt` into `fcr31`.

Any decoded `CTC1` targeting another control register remains unsupported at lowering time.

The backend does not translate `FCR31` to the Windows `MXCSR` in this milestone.

### 4.7 ADDA.S

`ADDA.S fs, ft` reads `FPR[fs]` and `FPR[ft]` as single-precision values, performs a single-precision addition, and stores the resulting raw 32-bit representation in `fp_acc`.

The v0 compatibility contract covers finite normal operands and zero cases required by the observed startup path. It does not claim EE-exact NaN, denormal, overflow, underflow, exception-flag, or rounding-mode behavior. `ADDA.S` does not modify `fcr31` in v0.

Tests must avoid ambiguous host-rounding-dependent cases.

### 4.8 SYNC

`SYNC` is represented as a semantic no-op because the runtime does not model EE pipeline stalls or cache synchronization in this milestone.

### 4.9 LUI

`LUI rt, imm` writes the 32-bit value `imm << 16`, interpreted as signed 32-bit and sign-extended to the low 64-bit integer result. The upper 64 bits of the 128-bit GPR remain preserved under the existing integer-write convention.

A write to GPR zero is discarded.

### 4.10 ANDI

`ANDI rt, rs, imm` computes the low-64-bit source value AND a zero-extended 16-bit immediate. The result is written using the existing low64-preserve-upper64 GPR convention.

A write to GPR zero is discarded.

## 5. IR model

The IR must no longer assume every destination is a GPR.

### 5.1 Destination kinds

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

`index` is meaningful only for indexed register files such as GPR and FPR.

### 5.2 Operand kinds

```cpp
enum class R5900IrOperandKind {
    Gpr = 0,
    Fpr,
    Immediate,
};
```

### 5.3 GPR write modes

```cpp
enum class R5900IrGprWriteMode {
    None = 0,
    Low64PreserveUpper64,
    Full128,
};
```

`Full128` is required for packed 128-bit operations such as `PADDUW`.

### 5.4 New semantic opcodes

The IR expresses semantics rather than encoded instruction names:

```cpp
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

### 5.5 Lowering mapping

```text
PADDUW rd,rs,rt
  -> AddPackedU32Saturate128
     destination = Gpr(rd), write_mode = Full128
     inputs = Gpr(rs), Gpr(rt)

MTHI rs
  -> MoveGprLow64, destination = Hi, input = Gpr(rs)

MTLO rs
  -> MoveGprLow64, destination = Lo, input = Gpr(rs)

MTHI1 rs
  -> MoveGprLow64, destination = Hi1, input = Gpr(rs)

MTLO1 rs
  -> MoveGprLow64, destination = Lo1, input = Gpr(rs)

MTSAH rs,imm
  -> ComputeMtsah, destination = Sa
     inputs = Gpr(rs), Immediate(imm)

MTC1 rt,fs
  -> MoveBits32, destination = Fpr(fs), input = Gpr(rt)

CTC1 rt,31
  -> MoveBits32, destination = Fcr31, input = Gpr(rt)

ADDA.S fs,ft
  -> AddF32ToAccumulator, destination = FpAccumulator
     inputs = Fpr(fs), Fpr(ft)

SYNC
  -> Nop

LUI rt,imm
  -> LoadUpperImmediateSignExtend
     destination = Gpr(rt), write_mode = Low64PreserveUpper64
     input = Immediate(imm)

ANDI rt,rs,imm
  -> And64
     destination = Gpr(rt), write_mode = Low64PreserveUpper64
     inputs = Gpr(rs), Immediate(zero_extended_imm16)
```

Existing guest PC and raw-word provenance must remain attached to every lowered IR instruction.

## 6. IR validation

The validator remains the single authority for structural correctness. Neither the reference executor nor x64 backend should silently reinterpret malformed operand/destination combinations.

Required validation rules include:

- every GPR index is `< 32`;
- every FPR index is `< 32`;
- unindexed destinations (`Hi`, `Lo`, `Hi1`, `Lo1`, `Sa`, `Fcr31`, `FpAccumulator`) reject arbitrary index use;
- `AddPackedU32Saturate128` requires `Gpr <- Gpr,Gpr` and `Full128`;
- `MoveGprLow64` allows only `Hi/Lo/Hi1/Lo1 <- Gpr`;
- `ComputeMtsah` requires `Sa <- Gpr,Immediate`;
- `MoveBits32` in this milestone allows only `Fpr/Fcr31 <- Gpr`;
- `AddF32ToAccumulator` requires `FpAccumulator <- Fpr,Fpr`;
- `LoadUpperImmediateSignExtend` requires `Gpr <- Immediate` and low64-preserve-upper64 mode;
- `And64` requires `Gpr <- Gpr,Immediate` and low64-preserve-upper64 mode;
- `Nop` has no destination, operands, or write mode;
- malformed instructions fail before execution or native-code publication.

`CTC1` to control register values other than 31 is rejected during lowering and produces no partial IR.

## 7. Reference executor

The reference executor must implement the full v0 semantic subset before the native backend is considered complete.

It operates on the same `R5900IrExecutionState` and must preserve GPR zero normalization.

The reference path is the semantic oracle for differential tests. It is not an emulator fallback used by the production dispatcher for unsupported native opcodes.

## 8. Windows x86-64 backend

Generated blocks retain the existing callable ABI:

```cpp
void generated(R5900IrExecutionState* state);
```

The state pointer arrives in `RCX` under the Windows x64 ABI.

The emitter should use volatile scratch registers only. The intended scratch set is `RAX`, `RDX`, `XMM0`, and `XMM1`; generated code must not require a stack frame for this milestone and must not clobber nonvolatile registers.

Implementation direction:

- `PADDUW`: explicit four-lane unsigned 32-bit saturation; no SSE4.1 dependency is required;
- `MTHI/MTLO/MTHI1/MTLO1`: direct 64-bit state moves;
- `MTSAH`: integer operations on the low 32-bit GPR source and immediate;
- `MTC1/CTC1`: raw 32-bit state moves;
- `ADDA.S`: load raw FPR values into XMM temporaries, use scalar single-precision add, store the raw result to `fp_acc`;
- `SYNC`: emit no instruction-specific machine code;
- `LUI`: construct the correctly sign-extended low64 result;
- `ANDI`: bitwise AND low64 source with zero-extended immediate.

All IR instructions must pass validation before executable memory is allocated or a compiled block is returned.

Existing RW -> RX memory protection, cache flush, RAII ownership, and failure reporting remain unchanged.

## 9. Dispatcher integration

The dispatcher eligibility table expands only after lowering, validation, reference execution, and x64 support exist for the corresponding instruction.

The dispatcher continues to:

- stop before branch/jump terminators;
- stop before traps/syscalls;
- stop before unsupported instructions;
- execute no delay slot;
- preserve exact cache stale detection;
- install no partial native block after compile failure;
- commit already executed prior blocks when a later iteration fails.

The first real `BEQ` boundary at guest PC `0x00100130` must be returned as `ControlFlow` and must not execute.

## 10. Error and atomicity contract

- Lowering one guest instruction is all-or-nothing.
- IR validation occurs before execution or native-code publication.
- A native block is published only after every instruction validates and code allocation/protection/cache-flush completes.
- Failed recompilation does not replace an existing valid cached block.
- Existing stale-code rules remain intact.
- GPR zero is normalized before and after native execution.
- `PADDUW` targeting GPR zero produces no architectural write.
- No production path interprets unsupported special/FPU instructions as a silent no-op, except the explicitly designed `SYNC` semantic no-op.

## 11. Test strategy

Implementation follows RED -> GREEN TDD.

### 11.1 ABI/layout tests

Compile-time and runtime checks protect the existing GPR ABI and new state offsets.

### 11.2 Lowering tests

Each newly supported guest instruction gets decode -> lower tests validating:

- semantic opcode;
- destination kind/index;
- operand kinds/indices;
- immediate treatment;
- guest PC/raw provenance;
- GPR-zero behavior where applicable.

COP1 lowering explicitly maps encoded register fields rather than introducing unnecessary decoder-struct aliases.

### 11.3 Validator tests

Positive and negative tests cover invalid register indices, incompatible destination kinds, wrong operand counts/types, illegal `Full128` use, invalid `CTC1` control index, and malformed `Nop`.

### 11.4 Reference executor tests

Required cases include:

- `PADDUW` four-lane arithmetic, saturation, and source/destination aliasing;
- `MTHI`, `MTLO`, `MTHI1`, `MTLO1`;
- `MTSAH` formula;
- raw `MTC1` bit copies;
- `CTC1 -> FCR31`;
- `ADDA.S` with zero and exactly representable finite values;
- `SYNC` no-side-effect behavior;
- `LUI` positive and negative sign-extension cases;
- `ANDI` zero-extension semantics.

### 11.5 Native differential tests

For identical initial state and IR, execute both the reference executor and native x64 block and compare the entire modeled architectural state:

- all 32 GPR low/high halves;
- `hi`, `lo`, `hi1`, `lo1`;
- `sa`;
- all 32 raw FPR values;
- `fcr31`;
- `fp_acc`.

### 11.6 Synthetic startup end-to-end test

A Windows-only synthetic sequence mirrors the instruction classes and dependencies required by the externally observed startup prefix without embedding proprietary game bytes.

Pipeline under test:

```text
decoder -> CFG/basic-block analysis -> IR lowering -> dispatcher -> x64
```

It must execute the full synthetic straight-line prefix and stop before a synthetic `BEQ` with `ControlFlow`, with exact instruction count and state invariants asserted.

### 11.7 External real-ELF validation

After CI is green, the externally supplied legal Burnout 3 ELF is used outside the repository to validate the real startup range:

```text
start: 0x00100008
stop before: 0x00100130 (BEQ)
```

The current analysis indicates a 74-instruction straight-line prefix. The validation run must confirm the count and final state before the milestone is recorded as real-ELF validated.

Expected checkpoints from the current external analysis include:

```text
r2.low64 = 0x00000000004E2680
r3.low64 = 0x0000000001ECEA00
r4.low64 = 0
hi = 0
lo = 0
hi1 = 0
lo1 = 0
sa = 0
FPR[0..31] = 0
fcr31 = 0
fp_acc = +0.0 raw
next_pc = 0x00100130
reason = ControlFlow
```

These values are acceptance checkpoints to be re-confirmed from the external ELF during implementation validation, not proprietary fixtures committed to the repository.

## 12. Completion criteria

The milestone is complete only when all of the following are true:

1. The approved architectural state extension is implemented without moving `gpr[32]` from offset zero.
2. Every scoped instruction lowers to validated semantic IR.
3. The reference executor implements the full scoped semantics.
4. The Windows x86-64 backend implements the same semantics natively.
5. Full-state differential tests pass.
6. The dispatcher executes the scoped subset and still stops before control flow, traps, and unsupported instructions.
7. The synthetic startup end-to-end test passes on Windows CI.
8. The full repository Windows test suite remains green.
9. External validation confirms the supplied legal Burnout 3 ELF executes natively from `0x00100008` through the instruction immediately before `0x00100130` and stops at that `BEQ` without executing a delay slot.
10. README/PROGRESS documentation states the exact achieved boundary and retains the explicit non-playable limitations.

## 13. Explicit non-goals

This milestone does not implement:

- guest loads/stores (`SB`, `SQ`, or other memory access);
- branch/jump/call execution;
- delay slots;
- branch-likely semantics;
- syscalls or PS2 kernel HLE;
- complete EE FPU exception/rounding/NaN/denormal behavior;
- MXCSR synchronization with `FCR31`;
- general MMI coverage beyond `PADDUW`;
- ISO reading;
- graphics/GS;
- audio;
- input;
- game initialization beyond the validated straight-line startup prefix;
- menu/frontend;
- race/gameplay.

The correct claim after completion is: **the first real straight-line startup prefix is recompiled and executed natively on Windows up to the first control-flow boundary**. It is not a game boot or playable build.
