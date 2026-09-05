# R5900 SQ + Guest Memory Writes v0 — Dependency Amendment

Date: 2026-09-05
Applies to: `docs/superpowers/specs/2026-09-05-r5900-sq-guest-memory-v0-design.md`

## Reason

Implementation-plan review found an existing dependency constraint that the original design's concrete `runtime::Ps2MemoryMap*` execution-context sketch would violate:

```text
b3r_runtime -> b3r_recompiler
```

`b3r_recompiler` therefore must not call `Ps2MemoryMap` methods directly or link back to `b3r_runtime`.

This amendment changes only the dependency mechanism. The approved `SQ`, fault, atomicity, x64-helper, dispatcher, and startup semantics remain unchanged.

## Corrected memory execution interface

`b3r_recompiler` owns a narrow guest-memory write bridge with no dependency on `runtime`:

```cpp
using R5900GuestWrite128Fn = bool (*)(void* user,
                                      std::uint32_t address,
                                      std::uint64_t low64,
                                      std::uint64_t high64) noexcept;

struct R5900GuestMemoryAccess {
    void* user{};
    R5900GuestWrite128Fn write128{};
};

struct R5900IrExecutionContext {
    R5900IrExecutionState* state{};
    R5900GuestMemoryAccess memory{};
    R5900IrMemoryFault memory_fault{};
    std::uint32_t current_memory_guest_pc{};
};
```

A memory access is available only when both `memory.user` and `memory.write128` are non-null.

## Runtime adapter

`b3r_recompiler_dispatcher_x64`, which already links through `b3r_analysis` to `b3r_runtime`, supplies the adapter:

```cpp
bool ps2_memory_write128_adapter(void* user,
                                 std::uint32_t address,
                                 std::uint64_t low64,
                                 std::uint64_t high64) noexcept {
    auto* memory = static_cast<runtime::Ps2MemoryMap*>(user);
    return memory != nullptr &&
           memory->write_u128(address, {low64, high64});
}
```

The dispatcher constructs the execution context with:

```cpp
R5900IrExecutionContext context{};
context.state = &state;
context.memory.user = &memory_;
context.memory.write128 = &ps2_memory_write128_adapter;
```

## Reference executor

The reference executor uses only the bridge:

```cpp
const bool ok = context.memory.write128(
    context.memory.user,
    aligned_address,
    source.low64,
    source.high64);
```

It never includes `runtime/ps2_memory_map.h` and never links to `b3r_runtime`.

Reference tests that require memory use a tiny in-test callback fixture. `Ps2MemoryMap` behavior remains covered separately by `ps2_memory_map_tests`.

## Native helper

The x64 backend helper also uses the same bridge, so generated native execution and the reference executor observe one memory contract without introducing a library cycle.

The helper records `current_memory_guest_pc`, aligned address, store kind, and 16-byte width in `memory_fault` when the callback is absent or returns false.

## CMake invariant

The library dependency direction remains:

```text
b3r_recompiler
    ^
    |
b3r_runtime
    ^
    |
b3r_analysis
    ^
    |
b3r_recompiler_dispatcher_x64
```

No new `b3r_recompiler -> b3r_runtime` edge may be added.

## Scope status

All other requirements in the approved design remain authoritative, including:

- `SQ` lowering to `Store128`;
- 32-bit wrapped effective address and 16-byte alignment;
- complete 128-bit little-endian store;
- atomic `Ps2MemoryMap::write_u128`;
- deterministic `MemoryAccessFailure`;
- Win64 helper-call ABI discipline;
- no memory operations in delay slots for v0;
- existing cache/fingerprint behavior;
- startup execution must advance beyond `0x00100160`.