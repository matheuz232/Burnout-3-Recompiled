# R5900 SQ + Guest Memory Writes v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the Windows-native R5900 startup path past `SQ 0x00100160` by adding atomic 128-bit guest-memory writes from IR lowering through reference execution, x86-64 code generation, dispatcher integration, and startup E2E validation.

**Architecture:** `SQ` lowers to a destination-less `Store128` IR operation. `b3r_recompiler` accesses guest memory only through an opaque callback bridge carried in `R5900IrExecutionContext`, preserving the existing `b3r_runtime -> b3r_recompiler` dependency direction. The dispatcher adapts its mutable `Ps2MemoryMap` to that bridge; generated Win64 code calls a narrow helper while preserving ABI stack/shadow-space rules and reports runtime mapping failures through a structured fault/result contract.

**Tech Stack:** C++20, CMake 3.25+, Visual Studio 2022 x64/MSVC, Win64 ABI, existing PS2 ELF/memory-map runtime, existing hand-emitted x86-64 backend, CTest/GitHub Actions `windows-2022`.

**Spec:** `docs/superpowers/specs/2026-09-05-r5900-sq-guest-memory-v0-design.md`

**Dependency amendment:** `docs/superpowers/specs/2026-09-05-r5900-sq-guest-memory-v0-dependency-amendment.md`

## Global Constraints

- Windows x64 remains the only native backend target.
- Do not add a `b3r_recompiler -> b3r_runtime` dependency; `b3r_runtime` already depends on `b3r_recompiler`.
- `SQ` effective address is `uint32(gpr[rs].low64) + uint32(sign_extend_16(imm))`, modulo `2^32`, then aligned down with `& 0xfffffff0u`.
- `SQ` stores all 128 bits of GPR `rt` in little-endian order: low64 first, high64 second.
- GPR0 as an `SQ` source is observable and must store sixteen zero bytes; never lower it to `Nop`.
- `Ps2MemoryMap::write_u128` is all-or-nothing at its API boundary.
- A failed guest store is `MemoryAccessFailure`, not lowering/compile failure and not a host exception.
- Memory operations in branch delay slots remain unsupported for this v0 increment.
- Existing FNV-1a + exact-word cache validation remains authoritative; no direct host pointer to guest code is embedded in compiled blocks.
- Do not add MMU/TLB/MMIO/page-permission behavior in this milestone.
- Preserve all existing BEQ + delay-slot semantics and the 120 Hz/bootstrap test gates.

---

## File Structure

### Existing files modified

- `src/runtime/ps2_memory_map.h` — typed `u64/u128` guest-memory API and 128-bit value alias.
- `src/runtime/ps2_memory_map.cpp` — little-endian 64/128-bit read/write implementation using full-span `translate()`.
- `src/recompiler/r5900_ir.h` — `Store128` opcode.
- `src/recompiler/r5900_ir.cpp` — `SQ` lowering.
- `src/recompiler/r5900_ir_validation.cpp` — `Store128` structural validation.
- `src/recompiler/r5900_ir_executor.h` — memory bridge, execution context, memory fault, context-aware execution overloads.
- `src/recompiler/r5900_ir_executor.cpp` — reference `Store128` semantics and structured runtime-memory failure.
- `src/recompiler/windows/r5900_x64_backend.h` — context-aware native execution result/API.
- `src/recompiler/windows/r5900_x64_backend.cpp` — Win64 helper call, `Store128` emission, context/fault propagation.
- `src/recompiler/windows/r5900_block_dispatcher.h` — mutable memory reference and new stop reason.
- `src/recompiler/windows/r5900_block_dispatcher.cpp` — `Ps2MemoryMap` callback adapter, `SQ` eligibility, runtime failure accounting.
- `tests/ps2_memory_map_tests.cpp` — typed 64/128-bit and atomicity coverage.
- `tests/r5900_block_dispatcher_windows_tests.cpp` — `SQ` dispatcher success/failure/accounting/cache regression coverage.
- `tests/r5900_block_dispatcher_startup_windows_tests.cpp` — synthetic mapped data/BSS target and startup continuation beyond `0x00100160`.
- `CMakeLists.txt` — focused Store128 portable/native test executables.
- `README.md` — milestone description after GREEN.
- `docs/PROGRESS.md` — evidence and next boundary after GREEN.

### New focused tests

- `tests/r5900_ir_store128_tests.cpp` — lowering + validator contract.
- `tests/r5900_ir_store128_executor_tests.cpp` — reference execution and callback-memory behavior.
- `tests/r5900_x64_store128_windows_tests.cpp` — native helper/ABI/differential behavior.

No new production library is required.

---

### Task 1: Add atomic 64/128-bit `Ps2MemoryMap` accessors

**Files:**
- Modify: `src/runtime/ps2_memory_map.h`
- Modify: `src/runtime/ps2_memory_map.cpp`
- Test: `tests/ps2_memory_map_tests.cpp`

**Interfaces:**
- Consumes: existing `translate(std::uint32_t, std::size_t)` mutable/const full-span translation.
- Produces:
  - `using Ps2MemoryValue128 = std::array<std::uint64_t, 2>;`
  - `std::optional<std::uint64_t> read_u64(std::uint32_t) const noexcept;`
  - `std::optional<Ps2MemoryValue128> read_u128(std::uint32_t) const noexcept;`
  - `bool write_u64(std::uint32_t, std::uint64_t) noexcept;`
  - `bool write_u128(std::uint32_t, const Ps2MemoryValue128&) noexcept;`

- [ ] **Step 1: Write the failing typed-memory tests**

Extend the existing read/write fixture to at least 32 mapped bytes and add checks equivalent to:

```cpp
const Ps2MemoryValue128 original{
    0x0123456789abcdefull,
    0xfedcba9876543210ull,
};
expect(memory.write_u64(0x00300008u, 0x8877665544332211ull),
       "write_u64 must succeed");
expect(memory.read_u64(0x00300008u).value_or(0) == 0x8877665544332211ull,
       "read_u64 must round-trip little-endian data");
expect(memory.write_u128(0x00300010u, original),
       "write_u128 must succeed for a complete mapped span");
const auto loaded = memory.read_u128(0x00300010u);
expect(loaded.has_value(), "read_u128 must succeed");
expect((*loaded)[0] == original[0] && (*loaded)[1] == original[1],
       "read_u128 must preserve low/high ordering");
```

Add a separate region or boundary fixture where only the first 8 bytes of a requested 16-byte range are mapped. Snapshot those bytes before `write_u128`, require the call to return `false`, then require every snapshotted byte to remain unchanged.

- [ ] **Step 2: Run the memory-map test and verify RED**

Run on a portable host build if available:

```bash
cmake -S . -B build/host -DB3R_BUILD_TESTS=ON
cmake --build build/host --target ps2_memory_map_tests
ctest --test-dir build/host -R '^ps2_memory_map_tests$' --output-on-failure
```

Expected: compile failure because `Ps2MemoryValue128`, `read_u64`, `read_u128`, `write_u64`, and `write_u128` do not exist.

- [ ] **Step 3: Implement the public API and little-endian helpers**

In `ps2_memory_map.h`, include `<array>` and add the alias/method declarations.

In `ps2_memory_map.cpp`, implement each operation by translating the *complete* required span once. For `write_u128`, use exactly one `translate(address, 16)` call before mutation:

```cpp
bool Ps2MemoryMap::write_u128(std::uint32_t address,
                              const Ps2MemoryValue128& value) noexcept {
    const auto bytes = translate(address, 16u);
    if (!bytes) {
        return false;
    }
    for (unsigned i = 0; i < 8u; ++i) {
        (*bytes)[i] = static_cast<std::uint8_t>((value[0] >> (i * 8u)) & 0xffu);
        (*bytes)[8u + i] = static_cast<std::uint8_t>((value[1] >> (i * 8u)) & 0xffu);
    }
    return true;
}
```

`read_u128` must reconstruct `[low64, high64]` in the inverse order. Do not implement `write_u128` as two `write_u64` calls.

- [ ] **Step 4: Run focused and portable regression tests**

```bash
cmake --build build/host --target ps2_memory_map_tests
ctest --test-dir build/host -R '^ps2_memory_map_tests$' --output-on-failure
ctest --test-dir build/host --output-on-failure
```

Expected: `ps2_memory_map_tests` PASS and all portable tests remain green.

- [ ] **Step 5: Commit**

```bash
git add src/runtime/ps2_memory_map.h src/runtime/ps2_memory_map.cpp tests/ps2_memory_map_tests.cpp
git commit -m "feat: add atomic PS2 128-bit memory access"
```

---

### Task 2: Lower and validate `SQ` as `Store128`

**Files:**
- Modify: `src/recompiler/r5900_ir.h`
- Modify: `src/recompiler/r5900_ir.cpp`
- Modify: `src/recompiler/r5900_ir_validation.cpp`
- Create: `tests/r5900_ir_store128_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: decoded `R5900Instruction::Sq`, existing `R5900IrOperandKind::{Gpr,Immediate}`.
- Produces: `R5900IrOpcode::Store128` with no destination, `write_mode=None`, operands `[Gpr(rs), Gpr(rt), Immediate(sign_extend_16)]`.

- [ ] **Step 1: Add the focused Store128 test target before production code**

Add to `CMakeLists.txt` beside the existing IR tests:

```cmake
add_executable(r5900_ir_store128_tests tests/r5900_ir_store128_tests.cpp)
target_link_libraries(r5900_ir_store128_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_store128_tests COMMAND r5900_ir_store128_tests)
```

Create `tests/r5900_ir_store128_tests.cpp` with a small `expect()` harness. Decode an actual SQ word and require the lowering contract:

```cpp
constexpr std::uint32_t sq_word =
    (0x1fu << 26u) | (2u << 21u) | (7u << 16u) | 0xfff0u;
const auto decoded = decode_r5900(sq_word);
expect(decoded.instruction == R5900Instruction::Sq, "fixture must decode as SQ");
const auto lowered = lower_r5900_instruction(decoded, 0x00100160u);
expect(lowered.ok(), "SQ must lower");
expect(lowered.instructions.size() == 1u, "SQ must lower to one IR instruction");
const auto& ir = lowered.instructions.front();
expect(ir.opcode == R5900IrOpcode::Store128, "SQ must lower to Store128");
expect(!ir.destination.has_value(), "Store128 must have no destination");
expect(ir.write_mode == R5900IrGprWriteMode::None, "Store128 write mode mismatch");
expect(ir.inputs.size() == 3u, "Store128 operand count mismatch");
expect(ir.inputs[0].kind == R5900IrOperandKind::Gpr && ir.inputs[0].gpr_index == 2u,
       "Store128 base operand mismatch");
expect(ir.inputs[1].kind == R5900IrOperandKind::Gpr && ir.inputs[1].gpr_index == 7u,
       "Store128 value operand mismatch");
expect(ir.inputs[2].kind == R5900IrOperandKind::Immediate && ir.inputs[2].immediate == -16,
       "Store128 signed offset mismatch");
```

Also construct malformed `Store128` values and require validator rejection for destination present, wrong operand count, FPR operands, GPR index 32, and immediate `32768`/`-32769`. Include a valid `rt=0` case to prove the store is not discarded.

- [ ] **Step 2: Run the focused test and verify RED**

```bash
cmake -S . -B build/host -DB3R_BUILD_TESTS=ON
cmake --build build/host --target r5900_ir_store128_tests
```

Expected: compile failure because `R5900IrOpcode::Store128` does not exist.

- [ ] **Step 3: Add `Store128` and SQ lowering**

Append `Store128` to `R5900IrOpcode` in `r5900_ir.h`.

Add this lowering case in `r5900_ir.cpp`:

```cpp
case R5900Instruction::Sq: {
    auto ir = base_instruction(decoded, guest_pc, R5900IrOpcode::Store128);
    ir.inputs.push_back(gpr(decoded.rs));
    ir.inputs.push_back(gpr(decoded.rt));
    ir.inputs.push_back(immediate(decoded.signed_immediate()));
    result.instructions.push_back(ir);
    return result;
}
```

Do not special-case `decoded.rt == 0`.

- [ ] **Step 4: Add strict Store128 validation**

Implement a focused helper in `r5900_ir_validation.cpp`:

```cpp
R5900IrValidationResult validate_store128(const R5900IrInstruction& ir,
                                          std::size_t index) {
    if (ir.destination.has_value() || ir.write_mode != R5900IrGprWriteMode::None) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index, ir.guest_pc,
                       "Store128 must not have a destination or GPR write mode");
    }
    if (ir.inputs.size() != 3u ||
        ir.inputs[0].kind != R5900IrOperandKind::Gpr ||
        ir.inputs[1].kind != R5900IrOperandKind::Gpr ||
        ir.inputs[2].kind != R5900IrOperandKind::Immediate) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index, ir.guest_pc,
                       "Store128 expects base GPR, value GPR, signed immediate");
    }
    if (ir.inputs[0].gpr_index >= 32u || ir.inputs[1].gpr_index >= 32u) {
        return failure(R5900IrValidationError::InvalidRegister,
                       index, ir.guest_pc,
                       "Store128 GPR index out of range");
    }
    if (ir.inputs[2].immediate < -32768 || ir.inputs[2].immediate > 32767) {
        return failure(R5900IrValidationError::MalformedInstruction,
                       index, ir.guest_pc,
                       "Store128 immediate must fit signed 16 bits");
    }
    return {};
}
```

Wire it into `validate_r5900_ir_instruction`.

- [ ] **Step 5: Run focused and existing IR tests**

```bash
cmake --build build/host --target r5900_ir_store128_tests r5900_ir_tests r5900_ir_validation_tests
ctest --test-dir build/host -R 'r5900_ir_(store128|tests|validation_tests)' --output-on-failure
```

Expected: all selected tests PASS.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/recompiler/r5900_ir.h src/recompiler/r5900_ir.cpp src/recompiler/r5900_ir_validation.cpp tests/r5900_ir_store128_tests.cpp
git commit -m "feat: lower and validate R5900 SQ"
```

---

### Task 3: Add the guest-memory execution bridge and reference `Store128`

**Files:**
- Modify: `src/recompiler/r5900_ir_executor.h`
- Modify: `src/recompiler/r5900_ir_executor.cpp`
- Create: `tests/r5900_ir_store128_executor_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Store128` IR from Task 2.
- Produces:

```cpp
using R5900GuestWrite128Fn = bool (*)(void*, std::uint32_t,
                                      std::uint64_t, std::uint64_t) noexcept;
struct R5900GuestMemoryAccess { void* user{}; R5900GuestWrite128Fn write128{}; };
enum class R5900IrMemoryAccessKind { None = 0, Store };
struct R5900IrMemoryFault {
    bool active{};
    R5900IrMemoryAccessKind access{R5900IrMemoryAccessKind::None};
    std::uint32_t guest_pc{};
    std::uint32_t address{};
    std::uint32_t width_bytes{};
};
struct R5900IrExecutionContext {
    R5900IrExecutionState* state{};
    R5900GuestMemoryAccess memory{};
    R5900IrMemoryFault memory_fault{};
    std::uint32_t current_memory_guest_pc{};
};
```

Add `R5900IrExecutionError::MemoryAccessFailure` and context-aware overloads:

```cpp
R5900IrExecutionResult execute_r5900_ir(
    const std::vector<R5900IrInstruction>&,
    R5900IrExecutionContext&);
R5900IrBlockExecutionResult execute_r5900_ir_block(
    const R5900IrBlock&,
    R5900IrExecutionContext&);
```

Keep state-only overloads as wrappers.

- [ ] **Step 1: Add the focused reference-executor target and callback fixture**

Add:

```cmake
add_executable(r5900_ir_store128_executor_tests
  tests/r5900_ir_store128_executor_tests.cpp)
target_link_libraries(r5900_ir_store128_executor_tests PRIVATE b3r_recompiler)
add_test(NAME r5900_ir_store128_executor_tests
  COMMAND r5900_ir_store128_executor_tests)
```

The test fixture must not include `runtime/ps2_memory_map.h`. Use an opaque callback recorder:

```cpp
struct WriteRecorder {
    bool allow{true};
    std::uint32_t address{};
    std::uint64_t low64{};
    std::uint64_t high64{};
    std::size_t calls{};
};

bool record_write128(void* user, std::uint32_t address,
                     std::uint64_t low64, std::uint64_t high64) noexcept {
    auto& recorder = *static_cast<WriteRecorder*>(user);
    ++recorder.calls;
    recorder.address = address;
    recorder.low64 = low64;
    recorder.high64 = high64;
    return recorder.allow;
}
```

Test all of these cases:

1. base low32 + negative offset + 32-bit wrap;
2. low four effective-address bits are cleared;
3. source low64/high64 both reach the callback unchanged;
4. source GPR0 produces both halves zero even if `state.gpr[0]` is prefilled stale;
5. missing callback returns `MemoryAccessFailure` and fills fault PC/address/width=16;
6. callback returning false returns `MemoryAccessFailure` with the same provenance;
7. later instructions do not execute after a failing store;
8. state-only overload still executes memoryless IR exactly as before.

- [ ] **Step 2: Run and verify RED**

```bash
cmake --build build/host --target r5900_ir_store128_executor_tests
```

Expected: compile failure because execution-context/fault types do not exist.

- [ ] **Step 3: Add context/fault types and wrapper overloads**

Put the bridge types in `r5900_ir_executor.h`; do not forward-declare or include `Ps2MemoryMap`.

State-only wrappers must construct:

```cpp
R5900IrExecutionContext context{};
context.state = &state;
return execute_r5900_ir(instructions, context);
```

For block wrappers, do the equivalent.

- [ ] **Step 4: Prevalidate and implement Store128 reference semantics**

At the start of the context-aware sequence executor:

```cpp
if (context.state == nullptr) {
    return {R5900IrExecutionError::MalformedInstruction,
            "R5900 execution context has no CPU state"};
}
context.memory_fault = {};
for (std::size_t i = 0; i < instructions.size(); ++i) {
    const auto validation = validate_r5900_ir_instruction(instructions[i], i);
    if (!validation.ok()) {
        return map_validation_failure(validation);
    }
}
```

Then execute using `auto& state = *context.state`.

Implement `Store128`:

```cpp
case R5900IrOpcode::Store128: {
    const auto base = static_cast<std::uint32_t>(
        state.gpr[ir.inputs[0].gpr_index].low64);
    const auto offset = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(ir.inputs[2].immediate));
    const auto address = static_cast<std::uint32_t>(base + offset) & 0xfffffff0u;
    const auto source = state.gpr[ir.inputs[1].gpr_index];
    context.current_memory_guest_pc = ir.guest_pc;

    const bool available = context.memory.user != nullptr &&
                           context.memory.write128 != nullptr;
    const bool written = available &&
        context.memory.write128(context.memory.user,
                                address,
                                source.low64,
                                source.high64);
    if (!written) {
        context.memory_fault = {
            true,
            R5900IrMemoryAccessKind::Store,
            ir.guest_pc,
            address,
            16u,
        };
        normalize_zero(state);
        return {R5900IrExecutionError::MemoryAccessFailure,
                "R5900 Store128 guest-memory write failed"};
    }
    break;
}
```

Normalize GPR0 before source read, preserving architectural zero.

- [ ] **Step 5: Make block execution use one shared context**

The block executor must call the context-aware body and delay-slot paths. Keep BEQ predicate snapshot-before-delay behavior unchanged. Because memory delay slots are excluded by the dispatcher but valid IR may exist, the reference executor itself may execute Store128 in a delay slot correctly; only dispatcher admission is restricted.

- [ ] **Step 6: Run focused and all portable recompiler tests**

```bash
cmake --build build/host --target r5900_ir_store128_executor_tests r5900_ir_executor_tests r5900_ir_block_executor_tests
ctest --test-dir build/host -R 'r5900_ir_.*executor' --output-on-failure
ctest --test-dir build/host --output-on-failure
```

Expected: focused executor test and all portable tests PASS.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/recompiler/r5900_ir_executor.h src/recompiler/r5900_ir_executor.cpp tests/r5900_ir_store128_executor_tests.cpp
git commit -m "feat: execute R5900 Store128 through memory bridge"
```

---

### Task 4: Make native compiled blocks context-aware and emit Win64 `Store128`

**Files:**
- Modify: `src/recompiler/windows/r5900_x64_backend.h`
- Modify: `src/recompiler/windows/r5900_x64_backend.cpp`
- Create: `tests/r5900_x64_store128_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `R5900IrExecutionContext`, `Store128`.
- Produces:

```cpp
struct R5900X64ExecutionResult {
    R5900IrExecutionError error{R5900IrExecutionError::None};
    std::string message{};
    std::uint32_t next_pc{};
    [[nodiscard]] bool ok() const noexcept;
};

R5900X64ExecutionResult
R5900X64CompiledBlock::execute(R5900IrExecutionContext& context) const noexcept;
```

Keep a compatibility `execute(R5900IrExecutionState&)` wrapper only if existing tests need it; it must delegate through an empty-memory context.

Internal generated function ABI:

```text
RCX = R5900IrExecutionState*
RDX = R5900IrExecutionContext*
EAX = next guest PC on normal return
```

Helper ABI:

```cpp
bool r5900_native_store128(R5900IrExecutionContext* context,
                           std::uint32_t address,
                           std::uint64_t low64,
                           std::uint64_t high64) noexcept;
```

- [ ] **Step 1: Add Windows Store128 native test target**

Inside `if(WIN32)`:

```cmake
add_executable(r5900_x64_store128_windows_tests
  tests/r5900_x64_store128_windows_tests.cpp)
target_link_libraries(r5900_x64_store128_windows_tests PRIVATE b3r_recompiler_x64)
add_test(NAME r5900_x64_store128_windows_tests
  COMMAND r5900_x64_store128_windows_tests)
```

Use the same callback-recorder pattern as Task 3. Build one fallthrough IR block containing `Store128`, execute it natively, and compare with reference execution for:

- address wrap/alignment;
- low/high 64-bit data;
- GPR0 source;
- callback failure;
- missing callback;
- next-PC on success;
- no execution after failing store;
- memoryless legacy arithmetic block remains differential-equal.

- [ ] **Step 2: Push test-only RED gate and run Windows CI**

Before backend production changes, commit only `CMakeLists.txt` + new Windows test if needed for hosted execution:

```bash
git add CMakeLists.txt tests/r5900_x64_store128_windows_tests.cpp
git commit -m "test: require native R5900 Store128 execution"
git push
```

Expected Windows workflow: build/test failure specifically because the x64 compiled-block context API or `Store128` emission is not implemented. Record the run ID in working notes for `docs/PROGRESS.md` later.

- [ ] **Step 3: Add structured native execution API**

Change generated-function invocation to:

```cpp
using GeneratedFunction = std::uint32_t (*)(R5900IrExecutionState*,
                                             R5900IrExecutionContext*);
```

`execute(context)` must:

1. reject `context.state == nullptr`;
2. reset `context.memory_fault = {}` and `current_memory_guest_pc = 0`;
3. invoke generated code with both pointers;
4. if `memory_fault.active`, return `MemoryAccessFailure` with `next_pc=0`;
5. otherwise return success with EAX as `next_pc`.

- [ ] **Step 4: Add the C++ native Store128 helper**

Keep it in `r5900_x64_backend.cpp` and use only the bridge:

```cpp
bool r5900_native_store128(R5900IrExecutionContext* context,
                           std::uint32_t address,
                           std::uint64_t low64,
                           std::uint64_t high64) noexcept {
    if (context == nullptr || context->memory.user == nullptr ||
        context->memory.write128 == nullptr) {
        if (context != nullptr) {
            context->memory_fault = {true, R5900IrMemoryAccessKind::Store,
                                     context->current_memory_guest_pc,
                                     address, 16u};
        }
        return false;
    }
    if (!context->memory.write128(context->memory.user, address, low64, high64)) {
        context->memory_fault = {true, R5900IrMemoryAccessKind::Store,
                                 context->current_memory_guest_pc,
                                 address, 16u};
        return false;
    }
    return true;
}
```

- [ ] **Step 5: Add Win64 call-frame emission primitives**

For blocks containing any `Store128`, emit one prologue that keeps 16-byte alignment and reserves shadow/local space. On Win64 function entry `RSP % 16 == 8`; `sub rsp, 0x38` yields 16-byte alignment before nested `CALL` and provides 32-byte shadow space plus locals.

Use locals such as:

```text
[rsp+0x20] saved state pointer
[rsp+0x28] saved context pointer
[rsp+0x30] temporary source high64 if needed
```

At prologue:

```text
sub rsp, 0x38
mov [rsp+0x20], rcx
mov [rsp+0x28], rdx
```

Before every normal/failure `RET`, restore with `add rsp, 0x38`.

Memoryless blocks keep the existing frameless fast path.

- [ ] **Step 6: Emit Store128 helper arguments without losing architectural state**

For each `Store128`:

1. write `instruction.guest_pc` to `context.current_memory_guest_pc`;
2. compute effective low32 address from base GPR and signed immediate in a volatile register;
3. mask with `0xfffffff0`;
4. load source low/high halves before repurposing argument registers;
5. set `RCX=context`, `EDX=aligned address`, `R8=source.low64`, `R9=source.high64`;
6. materialize helper address in `RAX` and `call rax`;
7. `test al, al`; on zero, restore stack and return immediately;
8. reload state/context pointers from locals before further emitted guest instructions.

The emitted helper failure path may return `EAX=0`; the public wrapper uses `context.memory_fault.active` as the authoritative error signal.

- [ ] **Step 7: Update `emit_ir_instruction` and block compilation**

Add `Store128` emission. Determine whether a sequence needs a helper frame by scanning body instructions before emission. Continue rejecting memory operations in delay slots at dispatcher level; backend compilation itself may support them if the common emitter can do so without breaking branch duplication.

- [ ] **Step 8: Run Windows GREEN gate**

```bash
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug -R 'r5900_x64_(store128|backend|startup)' --output-on-failure
ctest --preset vs2022-debug --output-on-failure
```

Expected: new Store128 native test PASS and all existing Windows tests remain green.

- [ ] **Step 9: Commit backend GREEN**

```bash
git add src/recompiler/windows/r5900_x64_backend.h src/recompiler/windows/r5900_x64_backend.cpp
git commit -m "feat: emit native R5900 Store128 writes"
git push
```

Record the GREEN workflow run ID.

---

### Task 5: Integrate mutable guest memory into the block dispatcher

**Files:**
- Modify: `src/recompiler/windows/r5900_block_dispatcher.h`
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Modify: `tests/r5900_block_dispatcher_windows_tests.cpp`

**Interfaces:**
- Consumes: mutable `runtime::Ps2MemoryMap`, context-aware x64 execute result.
- Produces:
  - `R5900DispatchStopReason::MemoryAccessFailure`
  - constructor `R5900BlockDispatcher(runtime::Ps2MemoryMap&, R5900BlockDispatcherOptions = {})`
  - adapter `ps2_memory_write128_adapter(void*, address, low64, high64)` in dispatcher translation unit.

- [ ] **Step 1: Add dispatcher RED tests for successful and failed SQ**

Build a synthetic executable block with:

```text
SQ r7, 0(r2)
J sentinel
NOP
```

and a mapped writable/data region at the address contained in r2. Initialize 16 bytes to non-zero values and set `r7` to distinct low/high halves. Require:

```cpp
result.reason == R5900DispatchStopReason::ControlFlow
result.blocks_executed == 1u
result.instructions_executed == 1u
result.next_pc == j_pc
```

and verify all 16 data bytes match `r7` little-endian.

Add a failure fixture where r2 points to an unmapped 16-byte range. Require:

```cpp
result.reason == R5900DispatchStopReason::MemoryAccessFailure
result.blocks_executed == 0u
result.instructions_executed == 0u
result.next_pc == sq_pc
```

Require diagnostics to contain stage `runtime-memory`, guest PC, store width, and aligned address.

Add a prefix-failure fixture such as `ADDIU r5,r0,9; SQ ...` where the store fails. Require r5 mutation to remain committed, `instructions_executed == 1`, `blocks_executed == 0`, and the faulting SQ not counted.

- [ ] **Step 2: Run Windows dispatcher test and verify RED**

```bash
cmake --build --preset vs2022-debug --target r5900_block_dispatcher_windows_tests
ctest --preset vs2022-debug -R '^r5900_block_dispatcher_windows_tests$' --output-on-failure
```

Expected: failure because SQ is still rejected before execution and no memory-failure stop reason exists.

- [ ] **Step 3: Make dispatcher memory mutable and add adapter**

Header:

```cpp
explicit R5900BlockDispatcher(runtime::Ps2MemoryMap& memory,
                              R5900BlockDispatcherOptions options = {});
...
runtime::Ps2MemoryMap& memory_;
```

CPP adapter:

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

Do not expose the adapter in a public header.

- [ ] **Step 4: Admit SQ only in block bodies**

Add `R5900Instruction::Sq` to `is_dispatcher_v0_eligible`.

Before lowering a BEQ delay slot, explicitly reject `delay.decoded.instruction == R5900Instruction::Sq` with a lowering/unsupported diagnostic stating that memory delay slots are outside v0. This keeps the scope rule explicit rather than accidental.

- [ ] **Step 5: Execute cached/new blocks through one context and handle faults**

For both cache-hit and cache-miss paths construct:

```cpp
R5900IrExecutionContext context{};
context.state = &state;
context.memory.user = &memory_;
context.memory.write128 = &ps2_memory_write128_adapter;
const auto native = cached_block.execute(context);
```

On `!native.ok()` with `MemoryAccessFailure`, calculate completed prefix instructions from the fault PC:

```cpp
const auto completed_prefix =
    static_cast<std::size_t>((context.memory_fault.guest_pc - current_pc) / 4u);
result.instructions_executed += completed_prefix;
result.reason = R5900DispatchStopReason::MemoryAccessFailure;
result.next_pc = context.memory_fault.guest_pc;
```

Do not increment `blocks_executed`. Return immediately. The faulting store is not counted.

On success, use `native.next_pc` and existing full guest-word count.

- [ ] **Step 6: Preserve cache accounting under runtime failure**

A successfully compiled block may remain cached even if execution later faults due to runtime address data. Tests must prove a second run with corrected GPR/mapping state can hit the same compiled block and succeed. Runtime memory failure must not be treated as stale code or force recompilation.

- [ ] **Step 7: Run dispatcher and complete Windows suite**

```bash
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug -R 'r5900_block_dispatcher_windows_tests|r5900_x64_store128_windows_tests' --output-on-failure
ctest --preset vs2022-debug --output-on-failure
```

Expected: focused tests PASS; existing BEQ/cache tests remain green.

- [ ] **Step 8: Commit**

```bash
git add src/recompiler/windows/r5900_block_dispatcher.h src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "feat: dispatch R5900 SQ guest-memory writes"
git push
```

---

### Task 6: Extend startup E2E beyond `SQ 0x00100160`

**Files:**
- Modify: `tests/r5900_block_dispatcher_startup_windows_tests.cpp`

**Interfaces:**
- Consumes: dispatcher `SQ` execution from Task 5.
- Produces synthetic startup contract:

```text
entry                  = 0x00100008
SQ                     = 0x00100160
next sentinel J        = 0x00100164
completed blocks       = 3
completed instructions = 82
stop reason            = ControlFlow
next_pc                = 0x00100164
```

- [ ] **Step 1: Convert the synthetic ELF helper to include a separate mapped data/BSS segment**

The current startup helper maps only code. Change it to emit two PT_LOAD headers:

1. executable code at `base`, containing the 89 instruction/sentinel words;
2. writable data/BSS region that covers `0x004e2680` through at least `0x004e2690` plus neighboring sentinel bytes.

A practical synthetic data segment is:

```text
guest_base = 0x004e2600
file_size  = 0x100
mem_size   = 0x200
flags      = 6   // PF_R | PF_W metadata; runtime does not enforce permissions
```

Initialize bytes around `0x004e2670..0x004e269f` to `0xa5` so neighbor-preservation can be asserted.

- [ ] **Step 2: Change synthetic startup expectations to require SQ execution**

After dispatch with `max_blocks >= 3`, assert:

```cpp
expect(result.reason == R5900DispatchStopReason::ControlFlow,
       "synthetic startup must advance past SQ to sentinel J");
expect(result.next_pc == 0x00100164u,
       "synthetic startup next boundary mismatch");
expect(result.blocks_executed == 3u,
       "synthetic startup must complete the SQ block");
expect(result.instructions_executed == 82u,
       "synthetic startup must execute 81 prior instructions plus SQ");
```

Require `memory.translate(0x004e2680u, 16)` to contain sixteen zero bytes, and require bytes immediately before/after the range to retain `0xa5`.

- [ ] **Step 3: Update the external-ELF harness from exact-SQ-stop to SQ-progress proof**

Keep all existing entry/register assertions. Before dispatch, snapshot/read the 16-byte target at `0x004e2680` if mapped. Run with a larger bounded budget such as 8 blocks.

The external path must assert:

```cpp
expect(result.instructions_executed >= 82u,
       "real startup must execute SQ after the two BEQ blocks");
expect(result.next_pc != 0x00100160u,
       "real startup must no longer stop at SQ");
```

Require the target 16 bytes to be zero after execution. Do **not** hard-code a later real boundary until static/native evidence identifies it. Print a line containing the dynamic stop PC, reason, blocks, and instructions, for example:

```text
REAL_ELF_SQ_VALIDATED sq=0x00100160 target=0x004e2680 stop=0x........ blocks=N instructions=N
```

CI continues to run only synthetic mode because no game ELF is stored in the repository.

- [ ] **Step 4: Run startup E2E GREEN gate**

```bash
cmake --build --preset vs2022-debug --target r5900_block_dispatcher_startup_windows_tests
ctest --preset vs2022-debug -R '^r5900_block_dispatcher_startup_windows_tests$' --output-on-failure
```

Expected synthetic output includes a new `SYNTHETIC_STARTUP_SQ_VALIDATED` line and PASS.

- [ ] **Step 5: Run the complete Windows workflow locally/CI**

```bash
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug --output-on-failure
```

Push and require GitHub Actions Windows `windows-2022` GREEN. Record the final run ID and exact test count.

- [ ] **Step 6: Commit**

```bash
git add tests/r5900_block_dispatcher_startup_windows_tests.cpp
git commit -m "test: advance startup execution through SQ"
git push
```

---

### Task 7: Record milestone evidence and next execution boundary

**Files:**
- Modify: `README.md`
- Modify: `docs/PROGRESS.md`

**Interfaces:**
- Consumes: actual final Windows CI run IDs/test count and synthetic startup output from Task 6.
- Produces: repository status that truthfully distinguishes CI-validated synthetic SQ execution from optional external real-ELF validation.

- [ ] **Step 1: Update README capability statement**

Replace wording that says native execution stops at `SQ 0x00100160`. State that the current path supports `Store128/SQ` through the guest-memory bridge, executes the startup SQ synthetically, and stops at the next unsupported/control-flow boundary. Keep game boot, loads, broader stores, MMU/MMIO, syscall/HLE, graphics/audio/input explicitly unimplemented.

- [ ] **Step 2: Update `docs/PROGRESS.md` component rows**

Update at least these rows:

- `PS2 memory mapping` — atomic typed 128-bit access now covered.
- `R5900 IR v0` — `Store128` lowering/validation included.
- `R5900 IR reference executor v0` — memory bridge and runtime fault semantics included.
- `R5900 Windows x86-64 backend v0` — first nested Win64 helper call and Store128 differential tests included.
- `R5900 native block dispatcher v0` — SQ body support, mutable memory adapter, runtime-fault prefix accounting included.
- `R5900 startup execution v0` — synthetic path executes 3 blocks / 82 instructions through SQ and reaches the next sentinel boundary.
- `Static/binary recompiler` — guest-memory store support exists; guest loads, broader stores/control flow/syscall/HLE remain pending.

- [ ] **Step 3: Add explicit RED/GREEN evidence paragraph**

Use the actual run IDs from Tasks 4–6, not invented values. Record what the RED run failed on and what the GREEN run passed, including the final complete Windows test count.

Also record the synthetic proof:

```text
SQ PC          0x00100160
store target   0x004e2680
store width    16 bytes
source         GPR0 => all zero
next boundary  synthetic J at 0x00100164
```

- [ ] **Step 4: Update Test Build 0.1 gates**

Replace the old external requirement that expected stop at SQ with the new manual external check:

```text
r5900_block_dispatcher_startup_windows_tests.exe <SLUS_210.50>
```

must report `REAL_ELF_SQ_VALIDATED`, prove the 16-byte target was zeroed, and show a stop PC beyond `0x00100160`.

Keep interactive GUI and physical 60-second pacing validation gates unchanged.

- [ ] **Step 5: Commit docs**

```bash
git add README.md docs/PROGRESS.md
git commit -m "docs: record R5900 SQ guest-memory milestone"
git push
```

---

### Task 8: Final verification and feature readiness gate

**Files:**
- No production change expected.
- Modify documentation only if final verification produces evidence that must be corrected.

**Interfaces:**
- Consumes: all previous tasks.
- Produces: verified branch ready for review/PR or next approved increment.

- [ ] **Step 1: Configure from a clean Windows x64 preset**

```bash
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug --clean-first
```

Expected: successful MSVC build with no new warnings promoted to errors.

- [ ] **Step 2: Run the complete CTest suite**

```bash
ctest --preset vs2022-debug --output-on-failure
```

Expected: every test PASS. The count must be at least the previous 35 tests plus the three new focused targets unless task consolidation changes target count deliberately; record the actual count rather than assuming a number.

- [ ] **Step 3: Run focused feature tests again**

```bash
ctest --preset vs2022-debug -R 'ps2_memory_map_tests|r5900_ir_store128_tests|r5900_ir_store128_executor_tests|r5900_x64_store128_windows_tests|r5900_block_dispatcher_windows_tests|r5900_block_dispatcher_startup_windows_tests' --output-on-failure
```

Expected: all selected tests PASS.

- [ ] **Step 4: Verify architecture dependency invariant**

Inspect `CMakeLists.txt` and require:

```text
b3r_runtime -> b3r_recompiler
b3r_recompiler_x64 -> b3r_recompiler
b3r_recompiler_dispatcher_x64 -> b3r_analysis + b3r_recompiler_x64
```

There must be no `target_link_libraries(b3r_recompiler ... b3r_runtime ...)` and no runtime header included from `src/recompiler/r5900_ir_executor.cpp` or `src/recompiler/windows/r5900_x64_backend.cpp`.

- [ ] **Step 5: Verify repository contains no proprietary game data**

Review changed filenames and ensure no ELF/ISO/game asset/dump was added. Only source, tests, and docs may be part of this milestone.

- [ ] **Step 6: Compare feature branch against its BEQ base**

```bash
git diff --stat 2c2a1029e011f7b5e6c3c95c0e54b28ffe7dafa1..HEAD
git diff --check 2c2a1029e011f7b5e6c3c95c0e54b28ffe7dafa1..HEAD
```

Expected: no whitespace errors; changes are confined to the SQ/memory execution scope plus its spec/plan/docs/tests.

- [ ] **Step 7: Final evidence commit only if needed**

If CI run IDs or final counts changed after the documentation commit, update only `docs/PROGRESS.md` with the verified values and commit:

```bash
git add docs/PROGRESS.md
git commit -m "docs: finalize R5900 SQ validation evidence"
git push
```

If documentation already contains the exact final evidence, do not create an empty commit.

---

## Self-Review Result

- Spec coverage: all approved SQ semantics, memory-map atomicity, callback dependency bridge, reference execution, Win64 helper ABI, structured faults, dispatcher accounting/cache behavior, startup E2E, external legal-ELF path, and documentation gates are assigned to explicit tasks.
- Dependency check: plan preserves `b3r_runtime -> b3r_recompiler`; no circular link is introduced.
- Type consistency: `R5900GuestWrite128Fn`, `R5900GuestMemoryAccess`, `R5900IrExecutionContext`, `R5900IrMemoryFault`, `R5900X64ExecutionResult`, `Store128`, and `MemoryAccessFailure` names are used consistently throughout.
- Placeholder scan: no implementation step relies on TBD/TODO/unspecified error handling.
- Scope check: guest loads, other stores, memory delay slots, MMU/TLB/MMIO, syscall/HLE, and broader control flow remain intentionally excluded.
