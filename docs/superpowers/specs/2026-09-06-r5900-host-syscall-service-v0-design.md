# R5900 Host Syscall Service v0 Design

Date: 2026-09-06
Status: proposed for implementation after review
Base: `feature/r5900-or-v0` (`07f879da636c77eaee4b418f61538e4d4b6bab2e`)

## Context

The native Windows R5900 path now executes the synthetic startup sequence through BSS clearing and register `OR`, then stops at a decoded `SYSCALL` boundary. In the previous real-ELF trace of `SLUS_210.50`, the first syscall boundary was reached at guest PC `0x001001C8` and was identified as the startup `SetupThread` call.

The current dispatcher treats every `R5900InstructionClass::System` instruction as `R5900DispatchStopReason::Trap`. That behavior is safe but prevents startup from advancing through guest kernel services.

The selected architecture is a host syscall service: generated x64 code remains unaware of PS2 kernel calls, while the dispatcher detects a `SYSCALL`, hands guest CPU state and guest memory to a dedicated host-side service, and resumes only when that service explicitly reports a handled call.

## Goals

1. Keep PS2 kernel HLE outside generated x64 blocks.
2. Give syscall handlers controlled access to the current R5900 architectural state and `Ps2MemoryMap`.
3. Preserve deterministic behavior for unknown or unsupported calls.
4. Make a handled syscall resumable at `guest_pc + 4` without pretending the `SYSCALL` executed as ordinary IR.
5. Provide a testable contract that can later host `SetupThread` and additional EE kernel services.
6. Avoid committing game ELF bytes, ISO data, or other proprietary assets.

## Non-goals for v0

- No general PS2 kernel implementation.
- No IOP RPC, GS, VU, DMA, interrupt, semaphore, file-system, or thread scheduler emulation beyond the first startup service work.
- No syscall instruction emitted into R5900 IR or native x64.
- No speculative hardcoding of the `SetupThread` selector or register ABI from memory alone.
- No attempt to continue past an unsupported syscall.

## Architectural placement

The service lives beside the Windows R5900 dispatcher rather than inside the generic runtime namespace:

- `src/recompiler/windows/r5900_host_syscall_service.h`
- `src/recompiler/windows/r5900_host_syscall_service.cpp`

This placement is deliberate. The service needs `R5900IrExecutionState`, currently owned by the recompiler layer, and `runtime::Ps2MemoryMap`. Placing the service under `runtime` would create an undesirable dependency from runtime back into recompiler state. Moving the CPU-state type into runtime is a larger refactor and is not required for this milestone.

The dispatcher remains the only component that decides when guest execution crosses from native block execution into host syscall handling.

## Public contract

The host service exposes a narrow request/result API.

```cpp
enum class R5900HostSyscallStatus {
    Handled,
    Unsupported,
    Fault,
};

struct R5900HostSyscallRequest {
    std::uint32_t guest_pc{};
    std::uint32_t raw_instruction{};
};

struct R5900HostSyscallResult {
    R5900HostSyscallStatus status{R5900HostSyscallStatus::Unsupported};
    std::uint32_t next_pc{};
    std::string message{};
};

class R5900HostSyscallService {
public:
    [[nodiscard]] R5900HostSyscallResult handle(
        const R5900HostSyscallRequest& request,
        R5900IrExecutionState& state,
        runtime::Ps2MemoryMap& memory);
};
```

The request intentionally carries only instruction provenance. Syscall selector and argument extraction belong inside the service because they are part of the EE kernel ABI, not the dispatcher contract.

## Dispatcher integration

`R5900BlockDispatcher` gains a reference or pointer to an `R5900HostSyscallService`. Construction must remain deterministic: production callers provide a service, while tests may use a real service instance or a test double through the same interface if an interface abstraction becomes necessary.

When the analyzer encounters `R5900Instruction::Syscall` in a block:

1. Ordinary supported body instructions before the syscall are compiled and executed exactly once.
2. The syscall itself is not lowered to IR and is not counted as a natively executed instruction.
3. After the body prefix succeeds, the dispatcher calls the host syscall service with the syscall PC, raw instruction, current `R5900IrExecutionState`, and current `Ps2MemoryMap`.
4. `Handled` means the service has committed all guest-visible state changes. The dispatcher sets `next_pc` to the service result and continues execution while block budget remains.
5. `Unsupported` stops execution at the syscall PC with a dedicated deterministic stop reason and diagnostic containing the guest PC and decoded syscall selector when available.
6. `Fault` stops execution at the syscall PC with a dedicated host-syscall failure reason and leaves the diagnostic from the service intact.

A syscall at block entry therefore consumes no native block, but a handled syscall is an execution event and advances the guest PC. Accounting will track it separately from native instruction count so existing block/instruction counters do not silently change semantics.

## Dispatch result changes

Add explicit stop reasons rather than overloading `Trap`:

```cpp
UnsupportedSyscall,
HostSyscallFailure,
```

`Trap` remains available for other system-class instructions such as `BREAK` until those receive their own behavior.

Add a counter:

```cpp
std::size_t syscalls_handled{};
```

This avoids counting a host-handled syscall as a JIT-compiled instruction while still making execution progress measurable.

## Service internals

The v0 service has three internal responsibilities:

### 1. Decode EE syscall ABI

A single helper converts `R5900IrExecutionState` into a syscall selector plus arguments. The exact selector register and argument mapping must be established by an executable regression fixture before a named handler is enabled. The implementation must not infer those fields from the `SetupThread` name alone.

The generic service can be integrated before `SetupThread` is enabled: if the selector cannot be mapped to a registered handler, it returns `Unsupported` deterministically.

### 2. Dispatch by selector

Use an explicit `switch` or small table keyed by the validated syscall selector. v0 does not need a dynamic registry. A static dispatch table is simpler, auditable, and sufficient for startup work.

Each handler receives a small context containing references to CPU state and guest memory. Handlers must not call back into the block dispatcher.

### 3. Commit result

A successful handler writes guest-visible return values directly into `R5900IrExecutionState` and any guest-memory outputs through `Ps2MemoryMap`. It then returns `Handled` with `next_pc = request.guest_pc + 4`.

A handler that detects invalid guest pointers or invalid parameters returns `Fault`; it must not report `Handled` after a partial failure unless the documented PS2 syscall semantics explicitly require partial side effects.

## SetupThread milestone

The first named handler is `SetupThread`, because the previous real-ELF trace identified the first startup syscall at `0x001001C8` as that service.

Enabling it requires a regression fixture that captures, from the user-supplied executable at that boundary:

- the syscall selector value and the GPR that carries it;
- every argument GPR consumed by `SetupThread`;
- the guest-visible return GPR(s);
- any memory address passed by the call that the handler must validate or modify.

The fixture stores only synthetic or numeric ABI observations needed for tests. It must not embed original game code bytes or asset payloads.

Once the ABI observation is locked by tests, `SetupThread` is implemented as the smallest host-side state transition required to let startup continue. v0 does not implement preemptive scheduling: it records the thread bootstrap state required by the guest-visible contract and returns the documented guest result. Any later syscall that requires actual scheduling becomes a separate milestone.

## State ownership and invariants

The following invariants apply to every handler:

- `gpr[0]` remains zero after handling.
- GPR high 64-bit halves are preserved unless the verified syscall ABI explicitly defines a wider write.
- Unrelated HI/LO/HI1/LO1, SA, FPR, FCR31, and FP accumulator state remains unchanged.
- Guest memory accesses go through `Ps2MemoryMap`; handlers do not retain raw translated spans after returning.
- A handler cannot mutate dispatcher cache state.
- Unknown selectors never advance `next_pc`.

## Error handling

Diagnostics are stage-specific and include the syscall guest PC.

Examples of categories, not literal required strings:

- unsupported selector at `0x001001C8`;
- malformed/unsupported syscall instruction encoding;
- invalid guest pointer supplied to a handled service;
- service contract violation such as a successful result that does not advance the PC.

The dispatcher validates service results. `Handled` with `next_pc == guest_pc` is rejected as `HostSyscallFailure` to prevent accidental infinite loops.

## Testing strategy

Implementation follows TDD in two layers.

### Service unit tests

Create synthetic `R5900IrExecutionState` and `Ps2MemoryMap` fixtures to verify:

- unknown selector returns `Unsupported` without mutating state or memory;
- handled synthetic/validated selector returns `Handled` and advances exactly four bytes;
- `gpr[0]` remains zero;
- unrelated CPU state is preserved;
- invalid guest-memory access produces `Fault` and a deterministic diagnostic;
- `SetupThread` ABI extraction and return-state behavior once the selector/register mapping is revalidated.

### Dispatcher integration tests

Extend the synthetic BSS-clear → OR → SYSCALL fixture so that:

- RED: the syscall boundary still stops before host integration;
- GREEN: a handled synthetic syscall is dispatched after the OR and execution resumes at `PC + 4`;
- an unsupported syscall stops with `UnsupportedSyscall` at the exact syscall PC;
- a service fault stops with `HostSyscallFailure` at the exact syscall PC;
- native block/cache accounting remains stable and `syscalls_handled` increments independently;
- no syscall is compiled into an x64 block.

The full Windows CI remains the completion gate.

## Dependency and build changes

Add the service source to the existing Windows recompiler/dispatcher target rather than creating a new library in v0. The dispatcher target already depends on recompiler state and runtime memory, so this introduces no new dependency direction.

Add a focused Windows test executable for the host syscall service. Dispatcher integration coverage stays in the existing dispatcher startup/BSS test family unless separation materially improves test isolation during implementation.

## Rollout sequence

1. Add host service contract and unknown-selector unit test.
2. Integrate syscall boundary dispatch into `R5900BlockDispatcher` with deterministic unsupported/fault stop reasons.
3. Add a synthetic handled-syscall test to prove dispatcher resume behavior without relying on proprietary inputs.
4. Re-observe the real startup boundary ABI for `SLUS_210.50` and encode only the required numeric observations in a regression fixture.
5. Implement the minimal `SetupThread` handler and verify the real startup proceeds to the next unsupported boundary.
6. Treat that newly observed boundary as the next independent milestone.

## Acceptance criteria

The milestone is complete when all of the following are true:

- generated x64 contains no syscall-specific host logic;
- dispatcher can hand a `SYSCALL` to the host service and resume after a handled result;
- unknown selectors stop deterministically without advancing PC;
- handler faults stop deterministically without corrupting unrelated state;
- `SetupThread` is enabled only after its selector/register ABI is captured by a regression fixture;
- the first real startup syscall can be handled and execution reaches the next boundary when the user-supplied ELF is available for local verification;
- Windows CI passes with all existing tests plus new syscall tests;
- no proprietary game bytes or assets are committed.
