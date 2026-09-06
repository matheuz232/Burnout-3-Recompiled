# R5900 SetupThread HLE v0 — Design

## Status

Approved in-chat design, formalized for implementation planning.

## Context

The Windows R5900 dispatcher already supports an injectable host-syscall boundary. A guest `SYSCALL` is never lowered into IR/x64; instead, the dispatcher forwards the exact guest PC/raw instruction plus mutable R5900 execution state and PS2 memory to `IR5900HostSyscallService`.

The production `R5900HostSyscallService` currently validates the raw instruction, reads the EE syscall selector from GPR3 (`v1`), and returns `Unsupported` for every selector. The next real Burnout 3 startup boundary is `SetupThread`, selector `0x3c`, observed at guest PC `0x001001c8`.

Public PS2SDK startup code establishes the relevant EE ABI:

- GPR3 (`v1`) = syscall selector `0x3c`
- GPR4 (`a0`) = `gp`
- GPR5 (`a1`) = `stack`
- GPR6 (`a2`) = `stack_size`
- GPR7 (`a3`) = `args`
- GPR8 (`t0`) = `root_func`
- return value in GPR2 (`v0`)
- startup copies returned GPR2 into GPR29 (`sp`) immediately after the syscall

The external Burnout 3 startup harness has already observed these real arguments at the boundary:

- `gp = 0x004e8670`
- `stack = 0x01ff0000`
- `stack_size = 0x00010000`
- `args = 0x01d9ce80`
- `root_func = 0x00100220`

For the explicit-stack mode used by Burnout 3, the required return is the top of the supplied stack: `stack + stack_size`, which yields `0x02000000` for the observed values.

The special automatic-stack sentinel `stack == 0xffffffff` has additional kernel-visible semantics and is intentionally outside v0.

## Goal

Implement the smallest validated HLE behavior required for Burnout 3 to cross `SetupThread` while preserving a clean boundary for later syscalls such as `SetupHeap`.

Success means selector `0x3c` can be handled by the production host-syscall service for the explicit-stack case, returning the correct stack top in guest GPR2 and allowing the dispatcher to resume at `syscall_pc + 4` without pretending to emulate a complete PS2 thread scheduler.

## Non-goals

This increment does **not** implement:

- `SetupHeap` (`0x3d`)
- automatic-stack mode (`stack == 0xffffffff`)
- thread IDs
- scheduler/run queues
- WAIT/SUSPEND/RUN state
- thread creation/deletion syscalls
- kernel memory reservations
- guest stack writes
- direct mutation of GPR29 (`sp`) inside the syscall handler
- proprietary game data or ELF bytes in the repository

The guest startup code remains responsible for copying returned GPR2 into GPR29.

## Selected architecture

Use a lightweight, stateful main-thread context inside `R5900HostSyscallService`.

This is deliberately between two extremes:

1. A stateless arithmetic-only handler would cross the immediate boundary but discard ABI inputs that are useful for subsequent startup HLE and diagnostics.
2. A full kernel/thread subsystem would over-model behavior not yet required by the executable.

The service therefore records only the validated `SetupThread` inputs/result needed to describe the initialized main-thread context. It does not schedule anything.

## Public service state

Add a small value type, tentatively `R5900SetupThreadContext`, containing 32-bit guest-address/ABI fields:

- `gp`
- `stack_base`
- `stack_size`
- `stack_top`
- `args`
- `root_func`

`R5900HostSyscallService` stores the latest successfully handled context as an `std::optional<R5900SetupThreadContext>` and exposes a const inspection accessor for tests/diagnostics.

No dispatcher interface change is required. `IR5900HostSyscallService` remains unchanged.

## Input interpretation

For selector `0x3c`, read only the low 32 bits of the relevant guest GPR low64 values, consistent with EE syscall ABI values being 32-bit addresses/scalars at this boundary:

- `gp = uint32(GPR4.low64)`
- `stack = uint32(GPR5.low64)`
- `stack_size_raw = uint32(GPR6.low64)` then interpret as signed 32-bit for validity
- `args = uint32(GPR7.low64)`
- `root_func = uint32(GPR8.low64)`

The handler does not modify the input argument registers.

## Supported explicit-stack semantics

The v0 supported path requires:

1. `stack != 0xffffffff`
2. `stack_size > 0` when interpreted as signed 32-bit
3. `stack + uint32(stack_size)` must not overflow 32-bit guest address space

When all conditions hold:

1. compute `stack_top = stack + uint32(stack_size)`
2. construct the complete candidate `R5900SetupThreadContext`
3. only after all validation succeeds, commit the context to service state
4. write `stack_top` to `state.gpr[2].low64`
5. preserve `state.gpr[2].high64`
6. preserve every other R5900 architectural field
7. perform no guest-memory writes
8. return `R5900HostSyscallStatus::Handled`

The dispatcher already owns PC advancement and will resume at `request.guest_pc + 4` only after `Handled`.

## Unsupported automatic-stack sentinel

If `stack == 0xffffffff`, return `R5900HostSyscallStatus::Unsupported` with a diagnostic that identifies `SetupThread` automatic-stack mode as unsupported in v0.

This path must:

- not alter GPR2
- not alter any other guest state
- not alter guest memory
- not replace an existing previously successful stored context

The dispatcher therefore stops at the exact syscall PC and does not advance.

## Fault semantics

Return `R5900HostSyscallStatus::Fault` for malformed explicit-stack requests that cannot produce a valid deterministic stack top, including:

- `stack_size <= 0`
- 32-bit overflow in `stack + stack_size`

Fault processing is transactional: no guest-state mutation, memory mutation, or stored-context mutation occurs before validation is complete.

A raw instruction that is not `SYSCALL` continues to use the existing generic host-syscall `Fault` path.

Unknown/unimplemented selectors continue to return `Unsupported` exactly as today.

## State mutation contract

For a successful `SetupThread` call, the only architectural R5900 mutation performed by the service is:

`GPR2.low64 = zero_extend_32(stack_top)`

The following are explicitly preserved:

- GPR2.high64
- GPR3..GPR31, including GPR29/sp
- GPR0 invariant behavior as currently owned by execution machinery
- HI/LO/HI1/LO1
- SA
- all FPR raw values
- FCR31
- FP accumulator
- guest memory

This avoids silently performing the startup instruction `move $sp,$v0` inside HLE.

## Real Burnout 3 expected result

Observed real inputs:

- stack base `0x01ff0000`
- stack size `0x00010000`

Expected successful HLE result:

- `GPR2.low64 = 0x0000000002000000`
- stored `stack_top = 0x02000000`
- dispatcher resumes at `0x001001cc`

The next startup syscall is expected to be `SetupHeap` (`0x3d`), but crossing that later boundary is not part of this design.

## Testing strategy

Implementation follows TDD.

### Unit RED/GREEN: production host service

Add/extend Windows tests to prove:

1. selector `0x3c` starts RED because production currently returns `Unsupported`
2. explicit Burnout-like inputs produce `Handled`
3. `0x01ff0000 + 0x00010000 == 0x02000000`
4. GPR2.low64 changes to zero-extended stack top
5. GPR2.high64 is preserved
6. argument registers and unrelated architectural state are preserved
7. guest memory remains unchanged
8. recorded context exactly matches all five arguments plus computed top
9. automatic-stack sentinel returns `Unsupported` with no partial mutation
10. zero/negative stack size returns `Fault` with no partial mutation
11. address overflow returns `Fault` with no partial mutation
12. previously committed context survives later Unsupported/Fault requests unchanged
13. unrelated selectors remain `Unsupported`
14. non-SYSCALL raw instruction remains `Fault`

### Dispatcher integration

Extend syscall-dispatch tests with the real production `R5900HostSyscallService` to prove:

- native prefix commits before the host boundary
- successful `SetupThread` increments `syscalls_handled` exactly once
- syscall itself is not counted as a native block or selected guest word
- next guest PC becomes `syscall_pc + 4`
- a following deliberately unsupported instruction is reached, proving actual resume
- the service-produced GPR2 result survives dispatcher resume

### Startup-shaped integration

Extend the BSS/OR/startup fixture so the flow reaches:

`BSS clear -> OR/setup arguments -> SYSCALL 0x3c -> resume`

The fixture must verify:

- BSS writes are already committed before HLE
- OR/register setup is already committed before HLE
- `SetupThread` sees the exact intended arguments
- returned GPR2 stack top is correct
- cache/native accounting remains unchanged except `syscalls_handled + 1`
- no compiled/cached block is created for the syscall itself

### External ELF harness

The external-ELF path must remain optional and must not require proprietary data in CI.

When a user-supplied lawful Burnout 3 executable is supplied locally, the harness should be capable of attaching `R5900HostSyscallService` and validating that execution crosses `0x001001c8` using the observed arguments and produces `GPR2 = 0x02000000`.

CI success must not depend on the proprietary ELF.

## CI and acceptance gates

The milestone is complete only when:

- Visual Studio 2022 x64 Release build succeeds
- complete CTest suite passes with zero failures
- new SetupThread tests are included in CTest
- existing host-syscall null/Unsupported/Fault behavior remains green
- frame-pacing telemetry remains green
- 120 Hz pacing probe remains green
- analyzer and pacing-probe package validation remain green
- README/PROGRESS accurately state that SetupThread explicit-stack HLE is implemented while automatic-stack mode and SetupHeap remain unsupported

Do not claim the game boots from this milestone alone.

## Files expected to change

Primary implementation/test surface:

- `src/recompiler/windows/r5900_host_syscall_service.h`
- `src/recompiler/windows/r5900_host_syscall_service.cpp`
- `tests/r5900_host_syscall_service_windows_tests.cpp`
- `tests/r5900_block_dispatcher_syscall_windows_tests.cpp`
- optionally the existing startup/BSS integration test where it gives stronger coverage without duplicating fixtures
- `README.md`
- `docs/PROGRESS.md`

No change is expected to `r5900_block_dispatcher.h` or the generic `IR5900HostSyscallService` contract unless implementation discovers a concrete requirement that this design cannot satisfy. Such a discovery requires stopping and revising the design rather than silently widening scope.

## Follow-up milestone

After SetupThread explicit-stack HLE is CI-validated, the next separate design/implementation milestone is `SetupHeap` (`0x3d`).
