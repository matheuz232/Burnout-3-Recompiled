# R5900 SQ + Guest Memory Writes v0 Design

Date: 2026-09-05
Branch: `feature/r5900-sq-guest-memory-v0`
Base: `feature/r5900-beq-delay-slot-v0` at `2c2a1029e011f7b5e6c3c95c0e54b28ffe7dafa1`

## Goal

Extend the native R5900 startup execution path past the current `SQ` boundary at guest PC `0x00100160` by adding the first guest-memory write operation to the IR, reference executor, Windows x86-64 backend, and block dispatcher.

The approved real startup evidence is:

- startup entry: `0x00100008`;
- first `BEQ` at `0x00100130`: taken;
- first delay slot: executed;
- second `BEQ` at `0x00100158`: not taken;
- second delay slot: executed;
- current stop: `SQ` at `0x00100160`;
- current completed execution: 2 guest blocks / 81 guest instructions.

For the supplied startup shape, the `SQ` uses GPR2 as the base, GPR0 as the 128-bit source, and offset zero. The current path therefore needs to write sixteen zero bytes to the guest address derived from GPR2 and then continue until the next unsupported or control-flow boundary selected by static evidence.

This milestone does not implement a generic PS2 MMU, TLB translation, MMIO devices, arbitrary physical-memory mirroring, guest loads, ordinary byte/half/word/doubleword stores, jump/call families, syscall/HLE, graphics, audio, input, or game boot.

## Architectural approach

Use the existing `Ps2MemoryMap` as the authoritative guest-memory backing store and add a typed 128-bit store operation to R5900 IR. Reference execution writes through the memory-map API. Windows x86-64 generated code calls a narrow native helper that performs the same guest-memory write contract.

Selected architecture:

1. `SQ` lowers to semantic IR (`Store128`).
2. IR validation proves the operand contract before execution.
3. Reference execution remains the semantic oracle.
4. Native code calculates the effective guest address and source value, then calls a C++ helper through the Windows x64 ABI.
5. The helper translates the 16-byte range through `Ps2MemoryMap` and performs an all-or-nothing little-endian write.
6. The dispatcher supplies a mutable memory execution context, receives deterministic runtime-memory failures, and continues dispatch only after a successful block.

Rejected alternatives:

1. Directly reserve/map the full 32-bit PS2 address space into the Windows process. This is premature before MMIO, mirroring, page protection, and TLB behavior are modeled.
2. Interpret stores in the dispatcher after native block execution. This would introduce a non-native side-effect seam and make later load/store support harder to reason about.
3. Embed `Ps2MemoryMap` container internals into emitted machine code. This would couple generated code to `std::vector` layout and runtime implementation details.

## Confirmed `SQ` semantics

`SQ` uses the EE GPR low 32-bit base value plus the sign-extended 16-bit immediate, with 32-bit address wrap. `LQ`/`SQ` are special EE operations that silently align the effective address down to a 16-byte boundary rather than raising an alignment exception.

Effective-address definition:

```text
base32      = uint32(gpr[rs].low64)
offset32    = uint32(sign_extend_16(immediate))
effective32 = base32 + offset32        // modulo 2^32
aligned32   = effective32 & 0xfffffff0
```

Store data is the complete 128-bit value of GPR `rt`:

```text
bytes [0..7]   <- rt.low64  little-endian
bytes [8..15]  <- rt.high64 little-endian
```

GPR0 remains architectural zero, so `SQ r0, offset(base)` writes sixteen zero bytes regardless of stale host memory in the backing state structure.

This semantic contract is consistent with the current PCSX2 interpreter implementation in `pcsx2/R5900OpcodeImpl.cpp`, which computes a 32-bit effective address, masks the low four address bits, and writes the complete 128-bit GPR value.

## `Ps2MemoryMap` extension

Add typed 64-bit and 128-bit accessors while preserving the existing byte/half/word API.

Recommended public contract:

```cpp
using Ps2MemoryValue128 = std::array<std::uint64_t, 2>;

[[nodiscard]] std::optional<std::uint64_t>
read_u64(std::uint32_t address) const noexcept;

[[nodiscard]] std::optional<Ps2MemoryValue128>
read_u128(std::uint32_t address) const noexcept;

[[nodiscard]] bool
write_u64(std::uint32_t address, std::uint64_t value) noexcept;

[[nodiscard]] bool
write_u128(std::uint32_t address,
           const Ps2MemoryValue128& value) noexcept;
```

The 128-bit array order is `[low64, high64]`.

All typed accesses are little-endian.

`write_u128` must be atomic at the memory-map API boundary: first translate the complete sixteen-byte span, then mutate it. If the full range cannot be translated, return `false` and leave every byte unchanged. No two-step `write_u64` implementation may expose a partial write if the second half is invalid.

The existing `translate(address, length)` range check already provides the required single-region/full-span validation and must remain the primitive used by the typed accessors.

This milestone does not add ELF `PF_W` enforcement. The existing memory map models mapped guest storage rather than runtime page permissions, and introducing permission faults here would be a separate architectural change.

## IR contract

Add one opcode:

```cpp
enum class R5900IrOpcode {
    ...,
    Store128,
};
```

`Store128` has no destination and uses `R5900IrGprWriteMode::None`.

Operand order is fixed:

```text
inputs[0] = Gpr(base / rs)
inputs[1] = Gpr(value / rt)
inputs[2] = Immediate(signed 16-bit offset)
```

The IR keeps the original `guest_pc` and `guest_raw` provenance.

Lowering rule:

```text
SQ rt, imm(rs)
    -> Store128 Gpr(rs), Gpr(rt), Immediate(sign_extend_16(imm))
```

No address alignment is baked into lowering. Alignment is semantic execution behavior so reference and native paths share the same rule.

Writing from GPR0 is valid and must not be lowered to `Nop`, because the memory side effect remains observable.

## IR validation

`Store128` is valid only when all of the following are true:

- no destination is present;
- write mode is `None`;
- exactly three operands exist;
- operand 0 is a valid GPR index `[0,31]`;
- operand 1 is a valid GPR index `[0,31]`;
- operand 2 is an immediate;
- the immediate is representable as a signed 16-bit value after lowering;
- provenance fields are preserved like every other executable instruction.

Malformed `Store128` must fail before reference mutation or native code publication.

The validator must not inspect whether the eventual guest address is mapped; mapping is runtime data and is handled by execution.

## Execution context

Memory operations require execution state plus mutable guest memory. Introduce a shared execution context used by reference and native execution without moving guest PC into architectural CPU state.

Required shape:

```cpp
namespace b3r::runtime {
class Ps2MemoryMap;
}

enum class R5900IrMemoryAccessKind {
    None = 0,
    Store,
};

struct R5900IrMemoryFault {
    bool active{};
    R5900IrMemoryAccessKind access{R5900IrMemoryAccessKind::None};
    std::uint32_t guest_pc{};
    std::uint32_t address{};
    std::uint32_t width_bytes{};
};

struct R5900IrExecutionContext {
    R5900IrExecutionState* state{};
    runtime::Ps2MemoryMap* memory{};
    std::uint32_t current_memory_guest_pc{};
    R5900IrMemoryFault memory_fault{};
};
```

The context uses pointers rather than references so it remains a stable POD-like object suitable for native-helper access and `offsetof`-based layout checks.

Every top-level execution call resets `current_memory_guest_pc` and `memory_fault` before executing generated or reference code.

Immediately before any emitted memory-helper call, generated code stores that IR instruction's constant `guest_pc` into `context.current_memory_guest_pc`. The helper copies that value into `memory_fault.guest_pc` only when a runtime memory failure occurs. This is the v0 provenance mechanism; no alternate helper signature is left open for implementation choice.

Existing state-only reference-executor overloads may remain as compatibility wrappers that create a context with `memory == nullptr`. Memoryless IR continues to work unchanged. A memory opcode executed without a memory map fails deterministically.

## Reference executor

`execute_r5900_ir` and block execution gain context-aware overloads.

For `Store128`:

1. validate the complete IR before mutation using the existing fail-fast model;
2. read `rs.low64` and derive `base32`;
3. sign-extend the immediate and perform modulo-32-bit addition;
4. align the result down to 16 bytes;
5. read the complete 128-bit source GPR value;
6. set `context.current_memory_guest_pc = instruction.guest_pc`;
7. call `memory->write_u128(aligned_address, {low64, high64})`;
8. if the write fails, populate `memory_fault` and return a memory-access execution error;
9. if it succeeds, continue to the next IR instruction.

The source GPR value and effective address are evaluated before the memory write. This preserves correct behavior when source and base use the same GPR.

Reference execution must never partially mutate guest memory on a failed 128-bit store.

## Execution error model

Extend `R5900IrExecutionError` with:

```cpp
MemoryAccessFailure,
```

A failed `SQ` reports at minimum:

```text
stage        = runtime-memory
operation    = store
width        = 128 bits
guest_pc     = faulting SQ PC
address      = aligned guest address
```

The dispatcher adds a corresponding stop reason:

```cpp
R5900DispatchStopReason::MemoryAccessFailure
```

A mapping failure is not a lowering or compilation failure and must not be reported as `UnsupportedInstruction`.

## Windows x64 backend ABI

Generated code must not dereference `Ps2MemoryMap` internals. It calls a C++ helper in the backend translation unit.

Required helper contract:

```cpp
bool r5900_native_store128(R5900IrExecutionContext* context,
                           std::uint32_t address,
                           std::uint64_t low64,
                           std::uint64_t high64) noexcept;
```

The helper:

- rejects a null context or null memory pointer deterministically;
- writes through `Ps2MemoryMap::write_u128`;
- on failure copies `context.current_memory_guest_pc` into the fault record and records store kind, aligned address and width 16;
- returns `true` only after the complete 16-byte write succeeds.

### Public compiled-block execution result

Replace the state-only public execution call with a structured context-aware result:

```cpp
enum class R5900X64ExecutionError {
    None = 0,
    InvalidContext,
    MemoryAccessFailure,
};

struct R5900X64ExecutionResult {
    R5900X64ExecutionError error{R5900X64ExecutionError::None};
    std::uint32_t next_pc{};
    std::string message{};

    [[nodiscard]] bool ok() const noexcept {
        return error == R5900X64ExecutionError::None;
    }
};

[[nodiscard]] R5900X64ExecutionResult
R5900X64CompiledBlock::execute(R5900IrExecutionContext& context) const noexcept;
```

All existing native-backend tests/callers migrate to the context API. Memoryless blocks may use `context.memory == nullptr`; they only require a valid `context.state`.

This deliberately avoids retaining a state-only overload that could silently lose runtime memory errors on a memory-bearing block.

### Raw generated-function calling convention

Internally the machine-code function keeps state addressing efficient by receiving two arguments:

```text
RCX = R5900IrExecutionState*
RDX = R5900IrExecutionContext*
EAX = next guest PC
```

`R5900X64CompiledBlock::execute(context)` verifies `context.state`, resets the current-memory/fault fields, and calls the native function with both pointers.

This preserves the current generated state-offset model based on `RCX` for memoryless instructions while making the context available to memory helpers.

### Windows x64 helper-call discipline

The first emitted helper call introduces a real nested Win64 call into generated code. The backend must obey the ABI:

- reserve the required 32-byte shadow space;
- maintain 16-byte stack alignment at every `CALL`;
- do not rely on volatile registers surviving the helper;
- preserve/reload the execution-state pointer after the helper returns;
- restore `RSP` before every return path;
- retain existing RW -> RX protection and instruction-cache flush behavior.

A straightforward v0 strategy is to reserve one fixed stack frame for any block containing memory helpers, save the context pointer in the local stack area, materialize helper arguments in `RCX/RDX/R8/R9`, call through an absolute helper address, reload state/context, test the helper result, and return immediately on failure.

Memoryless blocks retain the current no-helper fast path.

## Native failure propagation

The raw generated function returns the normal next guest PC in `EAX` on success.

On a helper failure:

1. the helper populates `context.memory_fault`;
2. generated code stops immediately and returns through a dedicated failure epilogue;
3. `R5900X64CompiledBlock::execute(context)` observes the active fault and returns `R5900X64ExecutionError::MemoryAccessFailure`;
4. `next_pc` is zero on failure and is ignored by callers;
5. the dispatcher reports `MemoryAccessFailure` with the recorded provenance.

No host access violation or C++ exception is used for normal guest mapping failure.

## Dispatcher integration

Change the dispatcher memory reference from const to mutable:

```cpp
R5900BlockDispatcher(runtime::Ps2MemoryMap& memory,
                     R5900BlockDispatcherOptions options = {});
```

Read-only analysis/cache validation still uses const reads logically; only native guest-memory operations require mutation.

Add `SQ` to dispatcher eligibility only after IR lowering, validation, reference semantics and native backend support are green.

For a block containing supported `SQ`:

1. analyze the block exactly as today;
2. lower the complete body/terminator/delay-slot contract before execution;
3. compile or retrieve a cached native block;
4. build an execution context referencing the caller's CPU state and dispatcher memory map;
5. execute the native block;
6. if a memory fault is reported, stop immediately with deterministic diagnostics;
7. if successful, consume the returned `next_pc` and continue within the block budget.

Memory operations in branch delay slots are explicitly outside this v0 dispatcher scope. Although `Store128` is valid instruction IR, dispatcher block construction must reject an `SQ` delay slot until memory-fault/accounting behavior for delay slots is designed separately.

## Runtime progress accounting

Compile/lowering/validation failures retain the existing atomic block behavior: no guest instruction from that block executes.

Runtime memory failures are different because prior native instructions in the same block may already have committed architectural state.

For a failing body `SQ` at contiguous guest PC `fault_pc` inside a block beginning at `start_pc`:

```text
completed_prefix = (fault_pc - start_pc) / 4
```

Accounting rules:

- the faulting `SQ` is not counted as completed;
- instructions before it are counted;
- `blocks_executed` does not increment because the block did not complete;
- no instruction after the failed store executes;
- prior CPU-state mutations remain committed;
- guest memory remains unchanged by the failed `SQ` itself.

The initial startup `SQ` at `0x00100160` is the first instruction of its block, so a mapping failure there contributes zero additional completed instructions.

Memory operations in delay slots are excluded in v0 specifically to avoid ambiguous branch-progress accounting.

## Cache and guest-code mutation

The current dispatcher cache fingerprints and byte-compares guest instruction words before reuse. Guest writes continue to use the same `Ps2MemoryMap`, so modifications to previously compiled guest code become visible to future cache validation.

Required behavior:

- a successful store to ordinary data does not invalidate unrelated cached blocks;
- if guest code bytes are changed, the next entry to the affected block observes the changed guest words and recompiles using the existing exact-word/FNV-1a contract;
- no direct host pointers to guest code are embedded in cached native blocks.

Self-modification of the block currently executing is outside this milestone. No new current-block coherence guarantee is claimed. The approved startup store targets data/BSS rather than its own code.

## Startup E2E contract

The synthetic startup fixture is extended so `SQ` at `0x00100160` is followed by a deterministic analyzer sentinel/control-flow boundary while preserving the known real startup address.

Before native dispatch, initialize the target 16 bytes with non-zero sentinels.

After dispatch, assert:

- the existing first two BEQ blocks still execute exactly as before;
- execution reaches and executes `SQ` at `0x00100160`;
- the target address is derived from the modeled GPR2 value using 32-bit + signed-offset + 16-byte-align semantics;
- all sixteen target bytes become zero for the startup `SQ r0,0(r2)` shape;
- neighboring bytes remain unchanged;
- the dispatcher advances beyond `0x00100160` and stops only at the next intentionally unsupported/control-flow test boundary;
- instruction/block accounting includes the completed `SQ` block/prefix according to the fixture shape.

The external legal-ELF Windows harness remains optional in CI because game data is never committed. When run manually against the supplied ELF, the milestone must prove that native execution no longer stops at `SQ 0x00100160` and must print the next observed stop PC/reason without embedding proprietary bytes in logs or repository data.

No `EXTERNALLY_VALIDATED` claim is made until that manual Windows path is actually executed with the legal ELF.

## TDD gates

Implementation must use explicit RED -> GREEN evidence.

### Gate 1: memory-map typed access

RED tests require missing 64/128-bit APIs.

GREEN tests cover:

- `read_u64`/`write_u64` little-endian behavior;
- `read_u128`/`write_u128` low/high ordering;
- unaligned API addresses as raw byte ranges;
- end-of-region exact fit;
- cross-boundary rejection;
- address-overflow rejection;
- failed `write_u128` leaves the whole destination unchanged.

Note: alignment belongs to `SQ`, not to generic memory-map accessors.

### Gate 2: `SQ` lowering and validation

RED first proves `Store128` is absent/unsupported.

GREEN covers:

- normal `SQ rt,imm(rs)`;
- negative immediate;
- `rs == 0`;
- `rt == 0` still lowers to a side-effecting store;
- no destination/write mode;
- malformed operand kinds/counts/registers fail.

### Gate 3: reference execution

RED requires missing memory-aware execution.

GREEN covers:

- aligned store;
- silent 16-byte align-down;
- signed negative offset;
- 32-bit address wrap;
- full low64/high64 write;
- GPR0 source writes zeros;
- base/source alias;
- unmapped full-span failure;
- no partial memory write;
- correct fault PC/address/width metadata;
- no later IR instruction executes after a failed store.

### Gate 4: native helper / Win64 ABI

RED requires x64 backend inability to compile/execute `Store128`.

GREEN differential tests compare reference and native execution for:

- state equality;
- target memory byte equality;
- aligned and unaligned effective addresses;
- positive/negative offsets;
- 32-bit wrap;
- GPR0 source;
- non-zero 128-bit values;
- base/source alias;
- memory fault result and metadata;
- instruction after store executes only on success.

Add stress coverage with multiple helper calls in one compiled block to expose stack-alignment/register-preservation bugs even if the startup path initially contains one `SQ`.

### Gate 5: dispatcher memory integration

RED requires current `UnsupportedInstruction` stop at `SQ`.

GREEN covers:

- mutable memory context reaches generated code;
- successful `SQ` advances execution;
- unmapped `SQ` produces `MemoryAccessFailure`;
- failing store does not increment completed block count;
- completed prefix accounting is deterministic;
- cached block re-executes with different runtime addresses/data without recompilation;
- memory writes do not alter cache-hit/miss accounting by themselves;
- code mutation becomes stale on the next cache lookup through existing exact-word validation;
- `SQ` in a delay slot remains explicitly rejected in v0.

### Gate 6: startup E2E

Extend `r5900_block_dispatcher_startup_windows_tests`.

Required synthetic proof:

```text
BEQ #1 taken
slot #1 executed
BEQ #2 not taken
slot #2 executed
SQ at 0x00100160 executed
16-byte zero write observed at startup target
execution proceeds to next controlled boundary
```

Preserve all existing startup register assertions and add memory-neighbor sentinels.

### Gate 7: full Windows regression

The final feature gate must pass:

- every existing test that was green on `feature/r5900-beq-delay-slot-v0`;
- all new memory/SQ tests;
- frame-pacing telemetry;
- one-second pacing probe smoke;
- analyzer package staging/validation;
- pacing-probe package staging/validation.

Hosted CI remains evidence of correctness, not physical-desktop performance certification.

## Files expected to change

Core implementation:

- `src/runtime/ps2_memory_map.h`
- `src/runtime/ps2_memory_map.cpp`
- `src/recompiler/r5900_ir.h`
- `src/recompiler/r5900_ir.cpp`
- `src/recompiler/r5900_ir_validation.cpp`
- `src/recompiler/r5900_ir_executor.h`
- `src/recompiler/r5900_ir_executor.cpp`
- `src/recompiler/windows/r5900_x64_backend.h`
- `src/recompiler/windows/r5900_x64_backend.cpp`
- `src/recompiler/windows/r5900_block_dispatcher.h`
- `src/recompiler/windows/r5900_block_dispatcher.cpp`

Tests/configuration as needed:

- `tests/ps2_memory_map_tests.cpp`
- `tests/r5900_ir_tests.cpp` or focused SQ lowering tests
- `tests/r5900_ir_validation_tests.cpp`
- `tests/r5900_ir_executor_tests.cpp` or focused memory tests
- `tests/r5900_x64_backend_windows_tests.cpp` or focused SQ backend tests
- `tests/r5900_block_dispatcher_windows_tests.cpp`
- `tests/r5900_block_dispatcher_startup_windows_tests.cpp`
- `CMakeLists.txt` when new test targets are split out

Documentation after GREEN:

- `README.md`
- `docs/PROGRESS.md`
- this spec if implementation evidence reveals a required clarification

## Explicit non-goals

This milestone does not implement:

- `LQ`;
- `SB`, `SH`, `SW`, `SD` or other store families;
- memory-mapped hardware/I/O;
- TLB/address-translation exceptions;
- ELF page-write permission enforcement;
- branch-delay-slot memory operations in the dispatcher;
- self-modifying current-block coherence;
- J/JAL/JR/JALR;
- branch-likely families;
- syscall/HLE;
- graphics/audio/input;
- a bootable Burnout 3 build.

## Completion criteria

`SQ + Guest Memory Writes v0` is complete only when all of the following are true:

1. `Ps2MemoryMap` provides tested atomic little-endian 128-bit access.
2. `SQ` lowers to validated side-effecting `Store128` IR.
3. Reference execution implements exact EE effective-address/alignment semantics.
4. Windows x64 generated code performs the same write through a tested ABI-safe helper.
5. Runtime guest-memory failures are deterministic and do not crash the host or partially write the 128-bit target.
6. Dispatcher executes supported body `SQ`, preserves cache correctness, and reports memory faults distinctly.
7. Synthetic startup native execution advances beyond `0x00100160` and proves the expected 16-byte zero write.
8. The complete Windows/MSVC CI gate remains green with no regression to the existing BEQ/delay-slot milestone.
9. `README.md` and `docs/PROGRESS.md` accurately record the new boundary and do not claim external real-ELF execution unless it has actually been performed.
