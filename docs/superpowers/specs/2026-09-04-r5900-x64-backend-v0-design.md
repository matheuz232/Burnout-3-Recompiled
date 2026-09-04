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
- Win32 W^X memory transition (`RW` while copying emitted bytes, then `RX` before execution; never persistent `RWX`);
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

The validation logic currently embedded inside `r5900_ir_executor.cpp` will be extracted into:

- `src/recompiler/r5900_ir_validation.h`
- `src/recompiler/r5900_ir_validation.cpp`

The public validation API is:

```cpp
enum class R5900IrValidationError {
    None = 0,
    MalformedInstruction,
    InvalidRegister,
    UnsupportedOpcode,
};

struct R5900IrValidationResult {
    R5900IrValidationError error{R5900IrValidationError::None};
    std::string message{};

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] R5900IrValidationResult validate_r5900_ir_instruction(
    const R5900IrInstruction& instruction,
    std::size_t instruction_index);
```

The validator owns structural checks only:

- known opcode;
- correct destination presence/absence;
- destination GPR index in `[0, 31]`;
- correct write mode;
- exact operand count;
- supported operand kinds;
- source GPR indices in `[0, 31]`.

The reference executor calls the validator immediately before each instruction executes, preserving its existing fail-fast/partial-commit behavior: earlier valid instructions remain committed if a later instruction is malformed. The x86-64 compiler validates the complete input vector before emission/allocation, because failed compilation must expose no partial native block.

### Backend files

The Windows-specific backend is:

- `src/recompiler/windows/r5900_x64_backend.h`
- `src/recompiler/windows/r5900_x64_backend.cpp`

The namespace remains `b3r::recompiler`.

### Backend public API

The backend API is:

```cpp
class R5900X64CompiledBlock {
public:
    R5900X64CompiledBlock() noexcept = default;
    ~R5900X64CompiledBlock();

    R5900X64CompiledBlock(const R5900X64CompiledBlock&) = delete;
    R5900X64CompiledBlock& operator=(const R5900X64CompiledBlock&) = delete;
    R5900X64CompiledBlock(R5900X64CompiledBlock&& other) noexcept;
    R5900X64CompiledBlock& operator=(R5900X64CompiledBlock&& other) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    void execute(R5900IrExecutionState& state) const noexcept;

private:
    friend struct R5900X64CompileResult;
    friend R5900X64CompileResult compile_r5900_ir_x64(
        const std::vector<R5900IrInstruction>& instructions);

    void* code_{};
    std::size_t size_{};
};

enum class R5900X64CompileError {
    None = 0,
    MalformedInstruction,
    InvalidRegister,
    UnsupportedOpcode,
    AllocationFailed,
    ProtectionFailed,
    CacheFlushFailed,
};

struct R5900X64CompileResult {
    R5900X64CompileError error{R5900X64CompileError::None};
    std::string message{};
    std::optional<R5900X64CompiledBlock> block{};

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] R5900X64CompileResult compile_r5900_ir_x64(
    const std::vector<R5900IrInstruction>& instructions);
```

Implementation may use a private constructor/helper rather than the shown friend declaration if required for valid C++ declaration ordering, but these observable semantics are fixed:

- successful compilation returns `error == None` and a valid owned block;
- failed compilation returns no block;
- compiled blocks are movable and non-copyable;
- `execute()` accepts `R5900IrExecutionState&` and mutates it in place;
- block destruction releases its Win32 allocation;
- `execute()` is only valid on a valid block; tests never invoke an empty/moved-from block.

### Emission staging and executable buffer

Machine bytes are first emitted into a normal `std::vector<std::uint8_t>`. Only after the entire IR vector validates and all bytes are available does the backend create executable storage:

1. `VirtualAlloc` with `PAGE_READWRITE`;
2. copy the completed byte vector into the allocation;
3. `VirtualProtect` to `PAGE_EXECUTE_READ`;
4. `FlushInstructionCache` for the generated range;
5. expose the compiled block;
6. `VirtualFree` in RAII destruction.

If allocation, protection, or instruction-cache flushing fails, compilation returns the corresponding explicit error and releases any allocation already made.

No page is requested or retained as writable and executable simultaneously.

This is deliberately Windows-specific and is built only under the existing Windows CMake path. Portable analysis/tests remain unaffected on non-Windows hosts.

## Calling convention and state layout

The generated function uses the Windows x64 ABI and has the logical signature:

```cpp
void generated(R5900IrExecutionState* state);
```

The state pointer arrives in `RCX`.

`R5900IrExecutionState::gpr` contains 32 entries of two consecutive `uint64_t` values:

- offset `index * 16 + 0`: low64;
- offset `index * 16 + 8`: high64.

The implementation must use `static_assert`/`offsetof` checks so generated displacement assumptions are tied to the C++ layout. The v0 backend uses only volatile scratch registers `RAX` and `RDX`; it does not require a stack frame and does not modify nonvolatile registers.

To minimize encoder surface area, register operands use `[RCX + disp32]` memory forms even where a shorter displacement is possible.

## Opcode lowering

### Nop

Emits no guest-semantic instruction bytes. Entry/exit architectural housekeeping remains independent of the guest NOP.

### AddWordSignExtend

Semantics must exactly match the reference executor:

1. materialize lhs low32 into `EAX`;
2. materialize rhs low32 into `EDX`;
3. execute 32-bit `ADD EAX, EDX`, which wraps modulo 2^32;
4. execute `CDQE` to sign-extend EAX into RAX;
5. if destination is not GPR zero, store RAX only to destination low64;
6. never modify destination high64.

For a GPR operand, materialization loads from `[RCX + gpr_offset]`. For an immediate operand, materialization uses the low 32 bits of the IR immediate, matching the current reference executor.

Aliasing such as `sp = sp + immediate` works because both operands are materialized before the destination store.

### Or64

Semantics:

1. materialize lhs low64 into `RAX`;
2. materialize rhs low64 into `RDX`;
3. execute 64-bit `OR RAX, RDX`;
4. if destination is not GPR zero, store only destination low64;
5. preserve destination high64.

For 64-bit immediate operands, use `MOV r64, imm64` so the complete IR immediate bit pattern is preserved. Do not rely on x86-64 `OR r64, imm32`, whose immediate is sign-extended and cannot represent every 64-bit IR operand.

## GPR zero behavior

The reference executor normalizes GPR0 to zero on entry and on every return path. The generated backend must produce the same observable state:

- clear GPR0 low64 and high64 at generated-function entry;
- discard destination writes targeting GPR0;
- clear GPR0 low64 and high64 again before `RET`.

This guarantees source reads from GPR0 observe zero even if a test intentionally supplies a corrupt incoming state.

## Error handling

Compilation is fail-fast and deterministic.

For malformed or unsupported IR:

- no native function/block is returned;
- no executable allocation is created because validation occurs before emission allocation;
- diagnostics include the IR instruction index and guest PC;
- no partial native code is exposed for execution.

Win32 allocation/protection/cache-flush failures return explicit backend errors and release any temporary allocation.

Execution itself has no recoverable error path in v0: only successfully compiled blocks are invoked.

## Testing strategy

Development follows TDD.

### RED 1 — public backend contract

Add Windows-only tests that include `recompiler/windows/r5900_x64_backend.h` and declare the required compile/execute behavior before implementation exists. CI must fail for the missing backend API, not for unrelated infrastructure.

### GREEN 1 — minimal native execution

Implement enough buffer/emitter functionality to execute deterministic NOP, AddWordSignExtend and Or64 examples.

Required cases:

- empty program normalizes GPR0 and otherwise preserves state;
- NOP preserves nonzero registers;
- ADDU-like positive result;
- ADDU-like negative 32-bit result with sign extension;
- 32-bit wraparound;
- ADDIU-style negative immediate;
- ORI-style low immediate;
- full 64-bit immediate OR case to prevent accidental imm32-sign-extension semantics;
- destination/source aliasing;
- high64 preservation;
- GPR0 normalization and discarded writes;
- move construction/assignment preserve executable ownership and leave moved-from blocks invalid.

### RED/GREEN 2 — validation sharing

Tests must demonstrate that malformed IR rejected by the reference executor is also rejected by backend compilation with no executable block returned. This includes invalid source/destination GPRs, wrong operand count, wrong write mode, malformed NOP and unknown operand/opcode values.

### Differential oracle tests

For each valid test vector:

1. clone identical initial EE register state;
2. run one copy through `execute_r5900_ir`;
3. compile and run the same IR through the x86-64 backend;
4. compare all 32 GPRs, both low64 and high64, bit-for-bit.

At least one multi-instruction sequence includes destination/source aliasing and a discarded write to GPR0.

### Decoder -> IR -> x86-64 integration

Use synthetic R5900 words for current supported guest instructions. Decode and lower them through production code, compile the resulting IR, execute natively, and compare against the reference executor result.

No proprietary game binary is used.

## CMake integration

The existing portable `b3r_recompiler` target gains only the shared portable validation source.

On 64-bit Windows, add:

```text
b3r_recompiler_x64
  src/recompiler/windows/r5900_x64_backend.cpp
  links: b3r_recompiler
```

The backend header is exposed through the existing `src` include root. The Windows backend test target is:

```text
r5900_x64_backend_windows_tests
```

and exists only inside the current `if(WIN32)` test section.

On non-Windows systems the x86-64 backend library and native-execution tests are skipped; portable recompiler tests still build. On Windows, `CMAKE_SIZEOF_VOID_P` must equal 8; a 32-bit Windows configuration fails at CMake configure time with a clear unsupported-target message.

## Security and correctness constraints

- never request or retain RWX memory;
- generated code only dereferences the caller-provided execution-state pointer at compile-time-known GPR offsets;
- no guest-controlled native addresses exist in v0;
- no native branches/calls are generated from guest targets in v0;
- validate every GPR index before emission;
- emitted arithmetic matches the reference executor’s defined unsigned/wrapping semantics;
- preserve all high64 GPR halves except GPR0 architectural normalization;
- emitted code uses only Windows-x64 volatile registers RAX/RDX/RCX and therefore needs no prologue/epilogue beyond `RET`;
- no stack access is emitted.

## Completion criteria

This milestone is complete only when all of the following are observed on the final branch head and again on post-merge `main`:

1. Windows x64 Release build succeeds with MSVC;
2. the full CTest suite is green;
3. `r5900_x64_backend_windows_tests` is green and executes generated machine code;
4. differential comparisons cover all current IR opcodes and compare every GPR low64/high64;
5. synthetic decoder -> lowering -> native execution coverage is green;
6. existing frame-pacing telemetry and pacing-probe gates remain green;
7. analyzer/probe package staging and validation remain green;
8. PR review finds no unresolved critical/important correctness issue;
9. documentation states clearly that this is native execution of the synthetic/current IR subset, not Burnout 3 gameplay or general guest execution.

## Next milestone after v0

Only after this backend is validated should the project design basic-block compilation/linking and a dispatch/runtime bridge. Real ELF analysis should continue to drive opcode expansion before broadening the backend instruction set.
