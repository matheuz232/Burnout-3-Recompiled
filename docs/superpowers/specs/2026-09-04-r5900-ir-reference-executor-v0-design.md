# R5900 IR Reference Executor v0 Design

Status: approved in chat on 2026-09-04

## Purpose

Add a deterministic reference executor for the existing non-executable R5900 IR v0. The executor is a semantic oracle for future x86-64 code generation: given the same initial EE register state and IR sequence, the reference executor defines the expected architectural result.

This milestone does not add a native backend, guest memory execution, branches, syscalls, COP/MMI behavior, graphics, audio, input, or Burnout 3 boot/gameplay.

## Current context

The repository already has:

- an R5900 decoder;
- provenance-carrying IR v0;
- lowering for `NOP`, `ADDU`, `ADDIU`, and `ORI`;
- `R5900IrOpcode::Nop`, `R5900IrOpcode::AddWord`, and `R5900IrOpcode::Or64`-style semantics represented by the current IR contract;
- explicit `Low64PreserveUpper64` writes for EE GPRs;
- lowering-time elision of side-effect-free writes to GPR zero.

The IR is currently non-executable. This design adds only an isolated interpreter for that IR subset.

## Chosen approach

Implement a small stateful reference executor that consumes `std::vector<R5900IrInstruction>` and updates an explicit EE register state.

Rejected alternatives:

1. Execute decoded R5900 instructions directly. This would duplicate semantics outside the IR and would not validate the IR contract used by the eventual backend.
2. Jump directly to x86-64 code generation. This would make backend bugs harder to localize because there would be no independent semantic oracle.

## Public data model

Introduce an EE register-state representation with exactly 32 GPRs. Each GPR stores two 64-bit halves:

```cpp
struct R5900IrGprValue {
    std::uint64_t low64{};
    std::uint64_t high64{};
};

struct R5900IrExecutionState {
    std::array<R5900IrGprValue, 32> gpr{};
};
```

The executor treats GPR index 0 as architecturally immutable zero. Before and after execution, `$zero.low64 == 0` and `$zero.high64 == 0` must hold regardless of caller-provided initial contents.

## Execution API

Add a result type with explicit failure reporting:

```cpp
enum class R5900IrExecutionError {
    None = 0,
    MalformedInstruction,
    InvalidRegister,
    UnsupportedOpcode,
};

struct R5900IrExecutionResult {
    R5900IrExecutionError error{R5900IrExecutionError::None};
    std::string message{};

    bool ok() const { return error == R5900IrExecutionError::None; }
};
```

Primary API:

```cpp
R5900IrExecutionResult execute_r5900_ir(
    const std::vector<R5900IrInstruction>& instructions,
    R5900IrExecutionState& state);
```

The executor mutates the supplied state in instruction order.

## Operand semantics

Operands supported by v0:

- GPR operands, reading `low64` from register indices 0-31;
- immediate operands, interpreted according to the opcode contract already encoded by lowering.

Any GPR operand with an index greater than 31 is an explicit `InvalidRegister` error.

## Opcode semantics

### `Nop`

Requirements:

- no destination;
- no register-state changes;
- provenance fields are ignored by execution but remain available for diagnostics.

### `AddWordSignExtend`

Requirements:

- exactly two inputs;
- destination must exist and be a valid GPR;
- write mode must be `Low64PreserveUpper64`;
- each input may be a GPR or immediate as produced by current lowering;
- arithmetic is 32-bit wrapping addition;
- the 32-bit result is sign-extended to 64 bits;
- only destination `low64` is updated;
- destination `high64` is preserved exactly;
- a destination of GPR zero produces no architectural write.

Conceptually:

```text
lhs32 = low32(input0)
rhs32 = low32(input1)
result32 = (lhs32 + rhs32) mod 2^32
result64 = sign_extend_32_to_64(result32)
dst.low64 = result64
dst.high64 = unchanged
```

### `Or64`

Requirements:

- exactly two inputs;
- destination must exist and be a valid GPR;
- write mode must be `Low64PreserveUpper64`;
- result is a 64-bit bitwise OR of the two operand values;
- immediate operands are represented as the already-lowered integer value;
- only destination `low64` is updated;
- destination `high64` is preserved exactly;
- a destination of GPR zero produces no architectural write.

## Structural validation

The executor must reject malformed IR rather than guessing intent.

Validation includes:

- invalid GPR indices;
- missing destination for write opcodes;
- unexpected destination for `Nop` if one would make the instruction structurally ambiguous;
- incorrect operand counts;
- unsupported operand kinds;
- write opcodes using a write mode other than `Low64PreserveUpper64`;
- opcodes not implemented by executor v0.

Error messages should include the IR instruction index and, when available, guest PC to make CI failures diagnosable.

## Failure and mutation policy

Execution is sequential and fail-fast.

If instruction N fails structural validation, instructions before N remain committed to the supplied state and instruction N and later instructions do not execute. This policy is intentional because it matches an ordinary sequential interpreter and keeps the executor simple.

The executor must still restore/enforce architectural GPR zero invariants before returning, including on failure.

## File layout

Expected implementation files:

- `src/recompiler/r5900_ir_executor.h`
- `src/recompiler/r5900_ir_executor.cpp`
- `tests/r5900_ir_executor_tests.cpp`
- `CMakeLists.txt` updates to compile/link/register the executor tests
- `README.md` and/or `docs/PROGRESS.md` updates only after CI validation

The executor remains part of `b3r_recompiler`; it does not belong in the runtime, graphics, or analysis layers.

## Test strategy

Use TDD with an explicit RED commit before implementation.

Minimum tests:

1. `Nop` preserves all nonzero register state.
2. `ADDU`-style IR adds low 32-bit words and sign-extends the result to low64.
3. Positive word result sign-extension.
4. Negative word result sign-extension.
5. 32-bit wraparound behavior.
6. `ADDIU`-style GPR + signed immediate execution.
7. `ORI` GPR + zero-extended immediate semantics as represented by lowering.
8. `ORI` operates on the full low 64 bits.
9. Every low64 write preserves destination high64.
10. Writes to GPR zero are ignored.
11. GPR zero is normalized to zero even if caller initializes it nonzero.
12. Invalid source register is rejected.
13. Invalid destination register is rejected.
14. Missing destination is rejected.
15. Incorrect operand count is rejected.
16. Wrong write mode is rejected.
17. Unsupported opcode is rejected explicitly.
18. A multi-instruction sequence produces deterministic final state.
19. Failure is fail-fast: earlier instructions remain committed and later instructions do not execute.
20. End-to-end synthetic path: R5900 decode -> existing IR lowering -> reference execution -> expected EE register state.

Existing repository tests must continue to pass unchanged.

## CI acceptance criteria

The milestone is complete only when:

- the RED test commit fails for the intended missing executor contract;
- the GREEN implementation builds with the repository's supported toolchains;
- all executor tests pass on Windows Server 2022 / VS2022 MSVC CI;
- all pre-existing tests and packaging/pacing validation remain green;
- documentation records exact CI evidence;
- no proprietary Burnout 3 code or data is added;
- a PR is reviewed/validated and merged to `main`;
- post-merge `main` CI is green.

## Out of scope

Explicitly out of scope for this milestone:

- x86-64 machine-code emission;
- executable memory/JIT allocation;
- host register allocation;
- guest memory loads/stores;
- branches, delay slots, calls, returns, exceptions, or PC stepping;
- HI/LO, COP0/COP1/COP2, MMI, VU behavior;
- PS2 device emulation;
- ISO parsing;
- Burnout 3 ELF execution;
- game initialization;
- graphics, audio, input, menu, or gameplay;
- 120 FPS simulation changes.

## Next milestone

Once the reference executor is CI-validated, implement a minimal x86-64 backend for the same IR subset and run differential tests:

```text
same initial EE state
        |\
        | \-> reference IR executor -> expected state
        |
        \----> x86-64 backend         -> actual state

expected state == actual state
```

That differential gate is the first safe step toward native execution of guest code.