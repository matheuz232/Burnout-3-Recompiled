# R5900 SetupHeap HLE v0 — Design

## Status

Approved in-chat design, corrected to preserve GPR2/v0, and formalized for implementation planning.

## Context

The Windows R5900 host-syscall path is already integrated and CI-validated. `R5900HostSyscallService` currently handles `SetupThread` (`0x3c`) for Burnout 3's explicit-stack startup path and stores a lightweight `R5900SetupThreadContext` containing the validated main-thread stack bounds and related startup arguments.

The next real Burnout 3 startup syscall is `SetupHeap`, selector `0x3d`, observed at guest PC `0x001001e4` after the successful `SetupThread` path.

The previously supplied lawful Burnout 3 executable (`SLUS_210.50`) establishes the startup values used by this design:

- `SetupThread.stack_base = 0x01ff0000`
- `SetupThread.stack_size = 0x00010000`
- `SetupThread.stack_top = 0x02000000`
- `SetupHeap.heap_start = 0x01ecea00`
- `SetupHeap.heap_size = 0xffffffff` (`-1` as signed 32-bit)

Public PS2SDK startup code calls `SetupHeap(&_end, (int)&_heap_size)`, and the default linker script provides `_heap_size = -1`. The public kernel API declares `SetupHeap` as returning `void`, not a heap pointer. Therefore this HLE must not invent a return value in GPR2/v0.

Independent public emulator behavior also corroborates the `heap_size == 0xffffffff` convention as a heap extending from the requested heap base to the current thread's reserved stack base.

For Burnout 3, that produces:

- `heap_start = 0x01ecea00`
- `heap_end = 0x01ff0000`
- effective heap size `0x00121600` bytes (`1,185,280` bytes)

## Goal

Implement the smallest deterministic `SetupHeap` HLE required for Burnout 3 to cross syscall `0x3d` while preserving the existing lightweight main-thread model.

Success means the production host-syscall service can validate and record the main thread's heap bounds, return `Handled`, and let the dispatcher resume at `syscall_pc + 4` without mutating guest architectural state or guest memory.

## Non-goals

This increment does **not** implement:

- `EndOfHeap` (`0x3e`)
- malloc/newlib allocation behavior
- a guest heap allocator
- scheduler/run queues
- multiple active thread contexts
- `CreateThread` or other thread-management syscalls
- automatic-stack `SetupThread` mode
- RDRAM expansion or kernel-reserved-memory modeling
- guest-memory writes for heap metadata
- graphics, GS, DMA, IOP, audio, or input
- proprietary game data or ELF bytes in the repository

The milestone must not claim that the game boots or is playable.

## Selected architecture

Extend the existing concrete `R5900HostSyscallService` with one additional lightweight stored context for the latest successfully initialized main-thread heap.

This keeps `SetupHeap` beside the existing `SetupThread` HLE because the heap boundary depends directly on the main thread's reserved stack base. It does not introduce a separate kernel object model or scheduler.

No change is required to `IR5900HostSyscallService` or the dispatcher public interface.

## Public service state

Add the exact value type:

```cpp
struct R5900SetupHeapContext {
    std::uint32_t heap_start{};
    std::uint32_t requested_heap_size{};
    std::uint32_t heap_end{};
};
```

`requested_heap_size` stores the raw low-32-bit ABI value. For Burnout 3's automatic-size convention this remains `0xffffffff`; it is not replaced by the derived effective size.

`R5900HostSyscallService` stores the latest successfully handled heap context as:

```cpp
std::optional<R5900SetupHeapContext> setup_heap_context_{};
```

and exposes it through:

```cpp
[[nodiscard]] const std::optional<R5900SetupHeapContext>&
setup_heap_context() const noexcept;
```

The effective size, when needed, is derivable as `heap_end - heap_start` and does not need a second authoritative stored field.

## Selector and ABI input interpretation

Add selector:

```text
SetupHeap = 0x3d
```

For selector `0x3d`, read only the low 32 bits of the EE ABI argument registers:

- `heap_start = uint32(GPR4.low64)`
- `heap_size_raw = uint32(GPR5.low64)`
- interpret `heap_size_raw` as signed 32-bit for validation

The handler must not modify either argument register.

A valid `SetupThread` context is required before any `SetupHeap` v0 request can succeed because the current design models only the initialized main thread and uses its reserved stack base as the upper heap boundary.

## Automatic-size semantics (`heap_size == -1`)

If `heap_size_raw == 0xffffffff`:

1. require an existing `setup_thread_context()`
2. set candidate `heap_end = setup_thread_context.stack_base`
3. require `heap_start < heap_end`
4. construct the complete candidate `R5900SetupHeapContext`
5. commit the context only after all validation succeeds
6. do not mutate any R5900 architectural state
7. do not write guest memory
8. return `R5900HostSyscallStatus::Handled`

For the real Burnout 3 startup values:

```text
heap_start = 0x01ecea00
heap_end   = 0x01ff0000
effective = 0x00121600 bytes
```

This leaves the already reserved stack range untouched:

```text
heap:  0x01ecea00 .. 0x01feffff
stack: 0x01ff0000 .. 0x01ffffff
```

## Explicit positive-size semantics

If `heap_size_raw` interpreted as signed 32-bit is positive:

1. require an existing `setup_thread_context()`
2. validate `heap_start + heap_size_raw` for 32-bit unsigned overflow
3. compute candidate `heap_end = heap_start + heap_size_raw`
4. require `heap_end <= setup_thread_context.stack_base`
5. construct and commit the complete candidate context only after validation succeeds
6. preserve all R5900 architectural state and guest memory
7. return `Handled`

An explicit positive-size heap may end exactly at `stack_base`; it may not overlap the reserved stack.

## Fault semantics

Return `R5900HostSyscallStatus::Fault` for requests that cannot produce a valid heap context, including:

- `SetupHeap` before a successful `SetupThread`
- `heap_size == 0`
- any negative signed size other than exactly `-1`
- 32-bit overflow in `heap_start + heap_size`
- automatic-size mode with `heap_start >= stack_base`
- explicit-size mode with `heap_end > stack_base`

Fault handling is transactional:

- do not mutate any guest GPR
- do not mutate HI/LO/HI1/LO1
- do not mutate SA
- do not mutate FPR/FCR31/FP accumulator
- do not mutate guest memory
- do not replace an existing successful `setup_heap_context_`

The existing non-SYSCALL raw-instruction path remains a generic `Fault`.

Unknown/unimplemented selectors remain `Unsupported`.

## Architectural state mutation contract

A successful `SetupHeap` call performs **zero guest architectural mutations**.

In particular, the handler must preserve:

- GPR0..GPR31 low64 and high64, including GPR2/v0
- GPR29/sp
- HI/LO/HI1/LO1
- SA
- every FPR raw value
- FCR31
- FP accumulator
- all guest memory

This is intentionally different from `SetupThread`: public PS2SDK declares `SetupHeap` as `void`, and the startup code does not consume a return value.

The only successful mutation is host-side service metadata: `setup_heap_context_` becomes the newly validated context.

## Relationship to SetupThread context

`SetupHeap` depends on `R5900SetupThreadContext::stack_base` as the maximum heap boundary.

To prevent stale HLE state, a later successful `SetupThread` call invalidates any previously stored `setup_heap_context_` before/when committing the new thread context. The new heap context remains empty until a subsequent successful `SetupHeap` call.

An unsuccessful `SetupThread` request (`Unsupported` or `Fault`) must not clear an existing heap context because no new thread context was committed.

This rule avoids associating a heap with obsolete stack bounds without introducing multi-thread scheduling state.

## Repeated SetupHeap calls

A later successful `SetupHeap` call replaces the previous heap context atomically.

A later `Fault` or `Unsupported` request preserves the most recent successful heap context unchanged.

## Dispatcher behavior

No dispatcher code or public contract change is expected.

The existing host-syscall dispatcher behavior remains authoritative:

- `Handled` increments `syscalls_handled`
- syscall is not lowered/compiled/cached as a native block
- syscall is not counted as an executed native guest instruction
- dispatcher advances to `request.guest_pc + 4`
- `Fault` stops at the exact syscall PC with `HostSyscallFailure`
- `Unsupported` stops at the exact syscall PC with `UnsupportedSyscall`

## Real Burnout 3 expected startup result

At guest PC `0x001001e4`, after the already handled `SetupThread` path, Burnout 3 presents:

```text
v1/GPR3 = 0x0000003d
a0/GPR4 = 0x01ecea00
a1/GPR5 = 0xffffffff
```

With stored main-thread stack base `0x01ff0000`, expected HLE behavior is:

- status `Handled`
- stored `heap_start = 0x01ecea00`
- stored `requested_heap_size = 0xffffffff`
- stored `heap_end = 0x01ff0000`
- effective heap size derivable as `0x00121600`
- GPR2/v0 unchanged
- every other guest architectural field unchanged
- guest memory unchanged
- dispatcher resumes at `0x001001e8`

## Testing strategy

Implementation follows TDD and must preserve the existing 54-test Windows suite topology unless a concrete requirement forces a revision.

### Unit RED/GREEN: production host service

Extend `tests/r5900_host_syscall_service_windows_tests.cpp` to prove:

1. selector `0x3d` starts RED because production currently returns `Unsupported`
2. Burnout-like automatic-size inputs return `Handled` after a valid `SetupThread`
3. resulting context exactly stores `0x01ecea00`, `0xffffffff`, and `0x01ff0000`
4. effective size derives to `0x00121600`
5. **all** GPR low/high halves, including GPR2/v0, are preserved
6. HI/LO/HI1/LO1/SA/FPR/FCR31/FP accumulator are preserved
7. guest memory is unchanged
8. `SetupHeap` before `SetupThread` returns `Fault` with no mutation
9. zero size returns `Fault` with no mutation
10. negative size other than `-1` returns `Fault` with no mutation
11. explicit-size 32-bit overflow returns `Fault`
12. automatic-size `heap_start >= stack_base` returns `Fault`
13. explicit heap overlap past `stack_base` returns `Fault`
14. explicit positive size ending exactly at `stack_base` succeeds
15. repeated successful `SetupHeap` replaces the stored context
16. a later `SetupHeap` fault preserves the previous valid context
17. a later successful `SetupThread` clears the previous heap context
18. a failed/unsupported `SetupThread` does not clear a valid heap context
19. unrelated selectors remain `Unsupported`
20. non-SYSCALL raw instructions remain `Fault`

### Dispatcher integration

Extend `tests/r5900_block_dispatcher_syscall_windows_tests.cpp` using the production service to prove a two-syscall startup-shaped sequence:

```text
SetupThread 0x3c
    -> resume
move/copy returned stack top into guest sp
    -> prepare SetupHeap arguments
SetupHeap 0x3d
    -> resume
following deliberate unsupported instruction
```

Required assertions:

- `syscalls_handled == 2`
- SetupThread still returns `0x02000000` in GPR2 before the guest copies it to sp
- guest instruction, not HLE, writes GPR29/sp
- SetupHeap preserves GPR2/v0 at the value present on entry
- SetupHeap context matches the Burnout-shaped heap values
- final stop occurs after SetupHeap at the deliberate following boundary
- neither syscall is counted as a native block/instruction
- no syscall block is compiled or cached

### Startup-shaped BSS integration

Extend the existing startup-shaped/BSS integration so the production service crosses both startup syscalls in order:

```text
BSS clear
 -> register/setup work
 -> SetupThread 0x3c
 -> guest stack assignment
 -> SetupHeap 0x3d
 -> next deliberate boundary
```

Verify:

- BSS writes remain committed
- existing native/cache accounting remains stable except for the additional handled syscall and any intentionally added native setup words
- SetupThread context remains exact
- SetupHeap observes the expected stack base from SetupThread
- SetupHeap context is exact
- SetupHeap does not alter mapped guest data memory
- no native/cache entry is created for either syscall

If keeping historical block/instruction counts requires awkward fixture distortion, prefer explicitly updating expected counts to match the minimal added guest setup instructions rather than hiding those instructions in HLE.

### External ELF validation

The lawful user-supplied `SLUS_210.50` may be used for manual validation but is never committed and is not required by CI.

A manual external harness may be extended to:

1. execute to `SetupThread` at `0x001001c8`
2. handle `SetupThread`
3. resume through the guest's stack assignment and heap argument preparation
4. reach/handle `SetupHeap` at `0x001001e4`
5. verify the exact Burnout heap context
6. continue to the first real unsupported boundary after `0x001001e4`

This external run is diagnostic evidence, not a CI acceptance dependency.

## CI and acceptance gates

The milestone is complete only when:

- Visual Studio 2022 x64 Release configure/build succeeds
- complete CTest suite passes with zero failures
- SetupHeap tests are included in existing Windows test targets
- existing SetupThread tests remain green
- existing host null/Unsupported/Fault paths remain green
- frame-pacing telemetry remains green
- 120 Hz pacing probe remains green
- analyzer package validation remains green
- pacing-probe package validation remains green
- README and `docs/PROGRESS.md` accurately state:
  - `SetupThread 0x3c` explicit-stack HLE is implemented
  - `SetupHeap 0x3d` main-thread HLE is implemented for `-1` and validated explicit positive sizes
  - SetupHeap preserves guest GPR state including v0
  - `EndOfHeap 0x3e` remains unimplemented
  - automatic-stack SetupThread remains unsupported

Do not claim game boot, rendering, menu, or gameplay from this milestone alone.

## Expected files to change

Required implementation/test/documentation surface:

- `src/recompiler/windows/r5900_host_syscall_service.h`
- `src/recompiler/windows/r5900_host_syscall_service.cpp`
- `tests/r5900_host_syscall_service_windows_tests.cpp`
- `tests/r5900_block_dispatcher_syscall_windows_tests.cpp`
- `tests/r5900_block_dispatcher_bss_clear_windows_tests.cpp`
- `README.md`
- `docs/PROGRESS.md`

The existing external startup test may be updated only if useful for optional lawful local validation.

No change is expected to:

- `IR5900HostSyscallService`
- `r5900_block_dispatcher.h`
- `r5900_block_dispatcher.cpp`
- CMake target topology

If implementation discovers a concrete need to change those contracts, stop and revise this design instead of silently widening scope.

## Public evidence used to constrain semantics

The design relies on behavior visible in public/open-source material rather than copied proprietary implementation:

- PS2SDK `ee/startup/src/crt0.c`: startup calls `SetupHeap(&_end, (int)&_heap_size)`
- PS2SDK `ee/startup/src/linkfile`: default `_heap_size = -1`
- PS2SDK public kernel API: `SetupHeap` has `void` return type
- Play! PS2 OS HLE: `heapSize == 0xffffffff` uses the current thread stack base as the heap upper boundary

These sources constrain observable ABI/HLE behavior only. No emulator source is copied into the implementation.

## Follow-up milestone

After SetupHeap is CI-validated, rerun the real startup path and treat the first unsupported boundary after guest PC `0x001001e4` as the next implementation target.

`EndOfHeap` (`0x3e`) is a likely future dependency but must not be implemented speculatively unless the real execution path requires it.
