# R5900 x86-64 Backend v0 Design

Status: proposed for implementation after user review
Date: 2026-09-04
Target: Windows 10/11 x64 only

## Goal

Add the first native x86-64 execution backend for the existing R5900 IR v0. The backend is a small semantic test backend, not yet a game runtime. It must compile the currently supported IR subset (`Nop`, `AddWordSignExtend`, `Or64`) into executable Windows x86-64 machine code and prove that the generated code produces the same architectural register state as the existing reference IR executor.

The first milestone is successful when synthetic R5900 instructions can flow through:

`R5900 decode -> IR lowering -> x86-64 compile -> native execution -> state comparison`

for the current NOP/ADDU/ADDIU/ORI subset.

## Scope

Included:

- Windows x64 only;
- manual, dependency-free machine-code emission for the current IR subset;
- executable code-buffer ownership and lifetime;
- Win32 W^X memory transition (`RW` while emitting, then `RX` before execution; never persistent `RWX`);
- shared structural IR validation used by both the reference executor and x86-64 compiler;
- exact 32x128-bit EE GPR state layout already defined by `R5900IrExecutionState`;
- architectural GPR zero normalization;
- differential tests comparing the backend result against the reference executor;
- synthetic decoder -> lowering -> backend integration coverage.

Explicitly excluded:

- real Burnout 3 guest execution;
- basic-block linking, branch dispatch, delay-slot execution, calls or returns;
- loads/stores or guest memory access;
- COP0/COP1/COP2/MMI/VU instructions;
- register allocation;
- optimization passes;
- persistent code cache;
- self-modifying-code handling;
- exceptions, interrupts or PS2 timing;
- graphics, audio, input or game initialization;
- LLVM, AsmJit, DynASM or other external code-generation dependencies.

## Approaches considered

### 1. Minimal manual x86-64 emitter — selected

Emit only the handful of machine instructions required by IR v0. This keeps the backend auditable, dependency-free and small enough that every byte sequence can be covered by execution tests. It also establishes the backend API without committing the project to a larger code-generation framework too early.

Trade-off: instruction encoding helpers are project-owned and must be expanded carefully as IR coverage grows.

### 2. AsmJit-style third-party assembler

This would simplify instruction encoding and future register allocation. It is rejected for v0 because the current backend needs only a tiny instruction subset, and introducing a dependency now would make the initial semantic milestone larger than necessary.

### 3. LLVM backend

This would provide optimization and mature code generation, but it is substantially heavier than the current milestone and would blur the distinction between validating R5900 semantics and designing an optimizing compiler pipeline. It is deferred until real executable coverage justifies the complexity.

## Architecture

### Shared IR validation

The validation logic currently embedded inside `r5900_ir_executor.cpp` will be extracted into a focused reusable unit, tentatively:

- `src/recompiler/r5900_ir_validation.h`
- `src/recompiler/r5900_ir_validation.cpp`

The validator owns structural checks only:

- known opcode;
- correct destination presence/absence;
- destination GPR index in `[0, 31]`;
- correct write mode;
- exact operand count;
- supported operand kinds;
- source GPR indices in `[0, 31]`.

Both the reference executor and the x86-64 compiler must call this validator before reading operands or generating/executing code. The executor keeps its execution-specific result type, while the backend maps validation failures into its own compile result.

### Backend public API

Add a Windows x64 backend API under the recompiler namespace, with a shape equivalent to:

```cpp
struct R5900X64CompiledBlock;

enum class R5900X64CompileError {
    None = 0,
    MalformedInstruction,
    InvalidRegister,
    UnsupportedOpcode,
    AllocationFailed,
    ProtectionFailed,
};

struct R5900X64CompileResult {
    R5900X64CompileError error;
    std::string message;
    R5900X64CompiledBlock block;
    bool ok() const noexcept;
};

R5900X64CompileResult compile_r5900_ir_x64(
    const std::vector<R5900IrInstruction>& instructions);
```

The exact ownership wrapper may differ to preserve move-only RAII cleanly, but the observable contract is fixed:

- successful compilation owns an executable code buffer;
- failed compilation owns no executable buffer;
- the compiled block is movable and non-copyable;
- execution accepts `R5900IrExecutionState&` and mutates it in place;
- destruction releases the Win32 allocation.

### Executable buffer

The backend will use Win32 virtual memory directly:

1. `VirtualAlloc` with read/write pages;
2. emit all machine code;
3. `VirtualProtect` to executable/read;
4. `FlushInstructionCache` before first execution;
5. `VirtualFree` in RAII destruction.

No page remains writable and executable simultaneously after compilation.

This is deliberately Windows-specific and should be built only under the existing `WIN32` CMake path. Portable analysis/tests remain unaffected on non-Windows hosts.

## Calling convention and state layout

The generated function uses the Windows x64 ABI and has the logical signature:

```cpp
void generated(R5900IrExecutionState* state);
```

The state pointer arrives in `RCX`.

`R5900IrExecutionState::gpr` contains 32 entries of two consecutive `uint64_t` values:

- offset `index * 16 + 0`: low64;
- offset `index * 16 + 8`: high64.

The v0 backend may use volatile scratch registers such as `RAX` and `RDX`; it must not depend on preserved registers or require a stack frame.

## Opcode lowering

### Nop

Emits no guest-semantic instruction bytes. Entry/exit architectural housekeeping remains independent of the guest NOP.

### AddWordSignExtend

Semantics must exactly match the reference executor:

1. read both operands as low 32-bit values;
2. perform unsigned modulo-2^32 addition;
3. sign-extend the resulting 32-bit word to 64 bits;
4. if destination is not GPR zero, store only destination `low64`;
5. never modify destination `high64`.

A representative sequence is:

- materialize lhs into `EAX`;
- `ADD EAX, rhs`;
- `CDQE` to sign-extend EAX into RAX;
- store RAX to destination low64 when destination != 0.

Aliasing such as `sp = sp + immediate` must work naturally because operands are read before the destination store.

### Or64

Semantics:

1. read both operands as full low64 values;
2. bitwise OR in 64 bits;
3. if destination is not GPR zero, store only destination low64;
4. preserve destination high64.

Register operands may use memory forms. Immediate operands must preserve the full IR immediate bit pattern. The emitter must not incorrectly rely on x86-64 sign-extended imm32 OR encoding for arbitrary 64-bit IR immediates; loading a 64-bit immediate into a scratch register is acceptable for v0.

## GPR zero behavior

The reference executor normalizes GPR0 to zero on entry and on every return path. The generated backend must produce the same observable state:

- clear GPR0 low64 and high64 at generated-function entry;
- discard destination writes targeting GPR0;
- clear GPR0 again before returning.

This guarantees source reads from GPR0 observe zero even if a test intentionally supplies a corrupt incoming state.

## Error handling

Compilation is fail-fast and deterministic.

For malformed or unsupported IR:

- no native function is returned;
- no executable buffer survives the failed compile;
- diagnostics include the IR instruction index and guest PC when available;
- no partial native code is exposed for execution.

Win32 allocation/protection failures return explicit backend errors and release any temporary allocation.

Execution itself has no recoverable error path in v0: only successfully compiled blocks may be invoked.

## Testing strategy

Development follows TDD.

### RED 1 — public backend contract

Add Windows-only tests that include the backend header and declare the required compile/execute behavior before implementation exists. CI must fail for the missing backend API, not for unrelated infrastructure.

### GREEN 1 — minimal native execution

Implement enough buffer/emitter functionality to execute deterministic NOP, AddWordSignExtend and Or64 examples.

Required cases:

- NOP preserves nonzero registers;
- ADDU-like positive result;
- ADDU-like negative 32-bit result with sign extension;
- 32-bit wraparound;
- ADDIU-style negative immediate;
- ORI-style low immediate;
- full 64-bit immediate OR case to prevent accidental imm32-sign-extension semantics;
- destination/source aliasing;
- high64 preservation;
- GPR0 normalization and discarded writes.

### RED/GREEN 2 — validation sharing

Tests must demonstrate that malformed IR rejected by the reference executor is also rejected by backend compilation with no executable block returned. This includes invalid source/destination GPRs, wrong operand count, wrong write mode, malformed NOP and unknown operand/opcode values.

### Differential oracle tests

For each valid test vector:

1. clone identical initial EE register state;
2. run one copy through `execute_r5900_ir`;
3. compile and run the same IR through the x86-64 backend;
4. compare all 32 GPRs, both low64 and high64, bit-for-bit.

At least one multi-instruction sequence must include destination/source aliasing and writes to GPR0.

### Decoder -> IR -> x86-64 integration

Use synthetic R5900 words for current supported guest instructions. Decode and lower them through production code, compile the resulting IR, execute natively, and compare against the reference executor result.

No proprietary game binary is used.

## CMake integration

The backend source and its native-execution tests are Windows-only. The existing portable `b3r_recompiler` target must continue to configure/build on non-Windows systems without including Win32 headers.

Preferred structure:

- keep portable IR/validation/executor in `b3r_recompiler`;
- add a Windows-only `b3r_recompiler_x64` static library containing the executable buffer and x86-64 emitter;
- link it publicly/private as appropriate to `b3r_recompiler`;
- add `r5900_x64_backend_windows_tests` only inside the existing `if(WIN32)` test block.

The backend must compile only for a 64-bit Windows target. Configuration should fail clearly or skip the backend if a 32-bit Windows generator is used; the project’s supported CI remains VS2022 x64.

## Security and correctness constraints

- never emit or retain RWX memory;
- generated code only dereferences the caller-provided execution-state pointer at compile-time-known GPR offsets;
- no guest-controlled native addresses exist in v0;
- no native branches/calls are generated from guest targets in v0;
- validate every GPR index before code generation;
- integer arithmetic uses defined unsigned host operations/encodings consistent with the reference semantics;
- preserve all high64 GPR halves except GPR0 architectural normalization.

## Completion criteria

This milestone is complete only when all of the following are observed on the final branch head and again on post-merge `main`:

1. Windows x64 Release build succeeds with MSVC;
2. the full CTest suite is green;
3. the new backend differential/native execution test is green;
4. existing frame-pacing telemetry and pacing-probe gates remain green;
5. package staging/validation remains green;
6. PR review finds no unresolved critical/important correctness issue;
7. documentation states clearly that this is native execution of the synthetic/current IR subset, not Burnout 3 gameplay or general guest execution.

## Next milestone after v0

Only after this backend is validated should the project design basic-block compilation/linking and a dispatch/runtime bridge. Real ELF analysis should continue to drive opcode expansion before broadening the backend instruction set.
