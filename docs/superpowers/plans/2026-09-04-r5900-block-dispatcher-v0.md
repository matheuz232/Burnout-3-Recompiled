# R5900 Block Dispatcher v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Windows x86-64 R5900 dispatcher that analyzes guest basic blocks, executes only the current supported straight-line prefix through the native backend, caches compiled blocks by guest PC plus exact-code fingerprint, and returns deterministic stop/accounting data.

**Architecture:** Add a Windows-only dispatcher target layered over `b3r_analysis`, `b3r_runtime`, and `b3r_recompiler_x64`. The dispatcher owns guest-PC progression and native-block cache lifetime; it does not change `R5900IrExecutionState`, the generated x64 ABI, CFG semantics, or executable-memory policy. Control-flow instructions, traps, delay slots, guest loads/stores, and decoded instructions outside NOP/ADDU/ADDIU/ORI remain non-executed boundaries.

**Tech Stack:** C++20, CMake 3.25+, Visual Studio 2022 x64 / MSVC, CTest, GitHub Actions `windows-2022`, existing Windows x64 backend.

**Spec:** `docs/superpowers/specs/2026-09-04-r5900-block-dispatcher-v0-design.md`

## Global Constraints

- Windows x86-64 only; portable analysis/recompiler targets must remain buildable on non-Windows hosts.
- Add no third-party dependency.
- Do not modify `R5900IrExecutionState`; guest PC remains dispatcher-owned.
- Do not execute branches, jumps, calls, traps, branch-likely semantics, or delay slots in v0.
- Do not add guest load/store execution, direct native block chaining, register allocation, cache eviction, or thread-safe dispatch.
- Preserve the x64 backend's RW -> RX W^X publication, instruction-cache flush, and RAII ownership; the dispatcher allocates no executable memory itself.
- Cache validity requires `start_pc`, instruction count, FNV-1a fingerprint, and exact guest-word vector equality.
- `R5900DispatchResult` metrics are per `run()` call.
- A known semantic boundary (`ControlFlow`, `UnsupportedInstruction`, `Trap`) takes precedence over `BlockBudgetExhausted` after an executed prefix.
- Existing 27 tests must remain green; the dispatcher suite becomes the 28th Windows CTest unless another independent test is added first.
- Do not claim real Burnout 3 ELF/game execution after this milestone.

---

## File Structure

**Create**

- `src/recompiler/windows/r5900_block_dispatcher.h` — public contract and native cache ownership.
- `src/recompiler/windows/r5900_block_dispatcher.cpp` — prefix selection, compile-on-demand, cache validation, native execution, PC/stop/accounting logic.
- `tests/r5900_block_dispatcher_windows_tests.cpp` — synthetic ELF/memory fixtures and all dispatcher tests.

**Modify**

- `CMakeLists.txt` — Windows-only dispatcher library and test target.
- `README.md` — user-facing capability and limitations.
- `PROGRESS.md` — milestone status and exact CI evidence.

The existing CFG analyzer, IR lowering/executor/validator, `Ps2MemoryMap`, and x64 backend remain behaviorally unchanged.

---

### Task 1: Public contract, build target, and failure-safe entry path

**Files:**
- Create: `src/recompiler/windows/r5900_block_dispatcher.h`
- Create: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Create: `tests/r5900_block_dispatcher_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `analysis::R5900ControlFlowOptions`, `runtime::Ps2MemoryMap`, `R5900IrExecutionState`, `R5900X64CompiledBlock`.
- Produces: `R5900DispatchStopReason`, `R5900DispatchResult`, `R5900BlockDispatcherOptions`, `R5900BlockDispatcher`.

- [ ] **Step 1: Write the failing build/contract test**

Add the production target inside the existing `if(WIN32)` block after `b3r_recompiler_x64`:

```cmake
add_library(b3r_recompiler_dispatcher_x64 STATIC
  src/recompiler/windows/r5900_block_dispatcher.cpp
)
target_include_directories(b3r_recompiler_dispatcher_x64 PUBLIC src)
target_link_libraries(b3r_recompiler_dispatcher_x64 PUBLIC
  b3r_analysis
  b3r_recompiler_x64
)
if(MSVC)
  target_compile_options(b3r_recompiler_dispatcher_x64 PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()
```

Add the test target inside the existing Windows test block:

```cmake
add_executable(r5900_block_dispatcher_windows_tests
  tests/r5900_block_dispatcher_windows_tests.cpp
)
target_link_libraries(r5900_block_dispatcher_windows_tests PRIVATE
  b3r_recompiler_dispatcher_x64
)
add_test(NAME r5900_block_dispatcher_windows_tests COMMAND r5900_block_dispatcher_windows_tests)
```

Create `tests/r5900_block_dispatcher_windows_tests.cpp` with these exact reusable fixture helpers:

```cpp
#include "recompiler/ps2_elf.h"
#include "recompiler/windows/r5900_block_dispatcher.h"
#include "runtime/ps2_memory_map.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_block_dispatcher_windows_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

b3r::runtime::Ps2MemoryMap make_memory(const std::vector<std::uint32_t>& words,
                                       std::uint32_t base,
                                       std::uint32_t flags = 5u) {
    constexpr std::uint32_t kProgramHeaderOffset = 52u;
    constexpr std::uint32_t kPayloadOffset = 0x100u;
    const std::uint32_t payload_size = static_cast<std::uint32_t>(words.size() * 4u);
    Bytes bytes(static_cast<std::size_t>(kPayloadOffset + payload_size + 0x40u), 0u);

    bytes[0] = 0x7fu;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 1u;
    bytes[5] = 1u;
    bytes[6] = 1u;
    put_u16(bytes, 16u, 2u);
    put_u16(bytes, 18u, 8u);
    put_u32(bytes, 20u, 1u);
    put_u32(bytes, 24u, base);
    put_u32(bytes, 28u, kProgramHeaderOffset);
    put_u16(bytes, 40u, 52u);
    put_u16(bytes, 42u, 32u);
    put_u16(bytes, 44u, 1u);

    put_u32(bytes, kProgramHeaderOffset + 0u, 1u);
    put_u32(bytes, kProgramHeaderOffset + 4u, kPayloadOffset);
    put_u32(bytes, kProgramHeaderOffset + 8u, base);
    put_u32(bytes, kProgramHeaderOffset + 12u, base);
    put_u32(bytes, kProgramHeaderOffset + 16u, payload_size);
    put_u32(bytes, kProgramHeaderOffset + 20u, payload_size);
    put_u32(bytes, kProgramHeaderOffset + 24u, flags);
    put_u32(bytes, kProgramHeaderOffset + 28u, 0x1000u);

    for (std::size_t index = 0; index < words.size(); ++index) {
        put_u32(bytes, static_cast<std::size_t>(kPayloadOffset) + index * 4u, words[index]);
    }

    auto parsed = b3r::recompiler::parse_ps2_elf(bytes);
    expect(parsed.ok(), "synthetic dispatcher ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "synthetic dispatcher memory must map");
    return std::move(*built.memory);
}

} // namespace
```

Then add the first contract cases:

```cpp
int main() {
    using namespace b3r::recompiler;

    constexpr std::uint32_t base = 0x00100000u;
    auto memory = make_memory({0u}, base);
    R5900BlockDispatcher dispatcher(memory);
    R5900IrExecutionState state{};
    state.gpr[1] = {0x1122334455667788ull, 0x8877665544332211ull};

    const auto zero_budget = dispatcher.run(base, state, 0u);
    expect(zero_budget.reason == R5900DispatchStopReason::InvalidBlockBudget,
           "zero block budget must reject explicitly");
    expect(zero_budget.next_pc == base,
           "zero block budget must retain start PC");
    expect(zero_budget.blocks_executed == 0u && zero_budget.instructions_executed == 0u,
           "zero block budget must execute nothing");
    expect(zero_budget.cache_hits == 0u && zero_budget.cache_misses == 0u &&
               zero_budget.recompilations == 0u,
           "zero block budget must not touch cache accounting");
    expect(state.gpr[1].low64 == 0x1122334455667788ull &&
               state.gpr[1].high64 == 0x8877665544332211ull,
           "zero block budget must not mutate state");

    const auto unaligned = dispatcher.run(base + 2u, state, 1u);
    expect(unaligned.reason == R5900DispatchStopReason::AnalysisFailure,
           "unaligned PC must map analyzer failure");
    expect(unaligned.next_pc == base + 2u,
           "analysis failure must report failing PC");
    expect(unaligned.blocks_executed == 0u,
           "analysis failure must not consume budget");

    std::cout << "r5900_block_dispatcher_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
```

- [ ] **Step 2: Run RED verification**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --parallel
```

Expected: build fails because `r5900_block_dispatcher.h/.cpp` do not exist.

Commit RED:

```bash
git add CMakeLists.txt tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "test: define R5900 block dispatcher contract"
```

For GitHub-only execution, verify the feature-branch Windows CI failure is the expected missing-dispatcher build failure.

- [ ] **Step 3: Implement the public header exactly**

Create `src/recompiler/windows/r5900_block_dispatcher.h`:

```cpp
#pragma once

#include "analysis/r5900_control_flow.h"
#include "recompiler/r5900_ir_executor.h"
#include "recompiler/windows/r5900_x64_backend.h"
#include "runtime/ps2_memory_map.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace b3r::recompiler {

enum class R5900DispatchStopReason {
    BlockBudgetExhausted,
    ControlFlow,
    UnsupportedInstruction,
    Trap,
    InvalidBlockBudget,
    AnalysisFailure,
    LoweringFailure,
    CompileFailure,
};

struct R5900DispatchResult {
    R5900DispatchStopReason reason{R5900DispatchStopReason::AnalysisFailure};
    std::uint32_t next_pc{};
    std::size_t blocks_executed{};
    std::size_t instructions_executed{};
    std::size_t cache_hits{};
    std::size_t cache_misses{};
    std::size_t recompilations{};
    std::string message{};
};

struct R5900BlockDispatcherOptions {
    analysis::R5900ControlFlowOptions block_options{};
};

class R5900BlockDispatcher {
public:
    explicit R5900BlockDispatcher(const runtime::Ps2MemoryMap& memory,
                                  R5900BlockDispatcherOptions options = {});

    [[nodiscard]] R5900DispatchResult run(std::uint32_t start_pc,
                                          R5900IrExecutionState& state,
                                          std::size_t max_blocks);

    void clear_cache() noexcept;
    [[nodiscard]] std::size_t cache_size() const noexcept;

private:
    struct CachedBlock {
        std::uint32_t start_pc{};
        std::uint32_t end_pc_exclusive{};
        std::uint64_t fingerprint{};
        std::vector<std::uint32_t> guest_words{};
        std::size_t guest_instruction_count{};
        R5900X64CompiledBlock native_block{};
    };

    const runtime::Ps2MemoryMap& memory_;
    R5900BlockDispatcherOptions options_{};
    std::unordered_map<std::uint32_t, CachedBlock> cache_{};
};

} // namespace b3r::recompiler
```

- [ ] **Step 4: Implement minimal GREEN entry behavior**

Create `r5900_block_dispatcher.cpp` with constructor, cache methods, PC formatting, budget validation, and analyzer failure mapping:

```cpp
#include "recompiler/windows/r5900_block_dispatcher.h"

#include <iomanip>
#include <sstream>
#include <string_view>

namespace b3r::recompiler {
namespace {

std::string format_stage_error(std::string_view stage,
                               std::uint32_t pc,
                               const std::string& detail) {
    std::ostringstream out;
    out << stage << " at guest PC 0x"
        << std::hex << std::setw(8) << std::setfill('0') << pc
        << ": " << detail;
    return out.str();
}

} // namespace

R5900BlockDispatcher::R5900BlockDispatcher(const runtime::Ps2MemoryMap& memory,
                                           R5900BlockDispatcherOptions options)
    : memory_(memory), options_(options) {}

void R5900BlockDispatcher::clear_cache() noexcept {
    cache_.clear();
}

std::size_t R5900BlockDispatcher::cache_size() const noexcept {
    return cache_.size();
}

R5900DispatchResult R5900BlockDispatcher::run(std::uint32_t start_pc,
                                              R5900IrExecutionState& state,
                                              std::size_t max_blocks) {
    (void)state;
    R5900DispatchResult result{};
    result.next_pc = start_pc;

    if (max_blocks == 0u) {
        result.reason = R5900DispatchStopReason::InvalidBlockBudget;
        result.message = "R5900 dispatcher block budget must be non-zero";
        return result;
    }

    const auto analyzed = analysis::analyze_r5900_basic_block(
        memory_, start_pc, options_.block_options);
    if (!analyzed.ok()) {
        result.reason = R5900DispatchStopReason::AnalysisFailure;
        result.message = format_stage_error("analysis", start_pc, analyzed.message);
        return result;
    }

    result.reason = R5900DispatchStopReason::UnsupportedInstruction;
    result.message = "R5900 dispatcher Task 1 supports entry validation only";
    return result;
}

} // namespace b3r::recompiler
```

- [ ] **Step 5: Run GREEN and commit**

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release -R r5900_block_dispatcher_windows_tests --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

Expected: dispatcher contract tests and all pre-existing tests pass.

```bash
git add CMakeLists.txt src/recompiler/windows/r5900_block_dispatcher.h src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "feat: add R5900 block dispatcher contract"
```

---

### Task 2: Supported-prefix compilation and semantic boundaries

**Files:**
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Modify: `tests/r5900_block_dispatcher_windows_tests.cpp`

**Interfaces:**
- Consumes: `analysis::R5900BasicBlock`, `lower_r5900_instruction()`, `compile_r5900_ir_x64()`.
- Produces: exact v0 eligibility for NOP/ADDU/ADDIU/ORI, semantic boundary mapping, complete-prefix native execution, first-unexecuted `next_pc`.

- [ ] **Step 1: Add failing instruction-encoding and boundary tests**

Add these helpers inside the test namespace:

```cpp
constexpr std::uint32_t r_type(std::uint8_t rs, std::uint8_t rt,
                               std::uint8_t rd, std::uint8_t sa,
                               std::uint8_t funct) {
    return (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) |
           funct;
}

constexpr std::uint32_t i_type(std::uint8_t op, std::uint8_t rs,
                               std::uint8_t rt, std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           imm;
}

constexpr std::uint32_t j_type(std::uint8_t op, std::uint32_t target) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           ((target >> 2u) & 0x03ffffffu);
}
```

Add a supported-prefix test with:

```cpp
const std::vector<std::uint32_t> words = {
    0u,
    r_type(9, 10, 8, 0, 0x21),
    i_type(0x09, 29, 29, 0xfff0),
    i_type(0x0d, 4, 5, 0xff00),
    i_type(0x0c, 1, 2, 0x00ff),
};
```

Initialize `r9 = 5`, `r10 = 7`, `r29.low64 = 0x1000`, `r4.low64 = 0x1234567800000000`, and nonzero high halves for destinations. Expect the first four instructions to execute, then:

```cpp
expect(result.reason == R5900DispatchStopReason::UnsupportedInstruction,
       "supported prefix must stop before ANDI");
expect(result.next_pc == base + 16u,
       "unsupported boundary must report first unexecuted PC");
expect(result.blocks_executed == 1u && result.instructions_executed == 4u,
       "only supported prefix must count as executed");
```

Add boundary-at-entry cases:

```cpp
const auto andi = i_type(0x0c, 1, 2, 0x00ff);
const std::uint32_t syscall_word = 0x0000000cu;
const auto beq = i_type(0x04, 1, 2, 1u);
```

For BEQ include a mapped NOP delay-slot word. Expect `UnsupportedInstruction`, `Trap`, and `ControlFlow` respectively, with zero blocks/instructions and unchanged state.

Add four prefix-plus-control-flow fixtures:

```cpp
const auto prefix = i_type(0x09, 0, 1, 7u);
const auto beq_term = i_type(0x04, 1, 2, 1u);
const auto j_term = j_type(0x02, base + 0x20u);
const auto jal_term = j_type(0x03, base + 0x20u);
const auto jr_term = r_type(31, 0, 0, 0, 0x08);
```

Each fixture is `{prefix, terminator, 0u}`. Expect `r1 == 7`, one block, one executed instruction, `next_pc == base + 4`, `ControlFlow`, and no execution of the NOP delay slot.

- [ ] **Step 2: Run RED and commit the failing tests**

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release -R r5900_block_dispatcher_windows_tests --output-on-failure
```

Expected: dispatcher suite fails because Task 1 has no prefix execution.

```bash
git add tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "test: cover R5900 dispatcher prefix boundaries"
```

- [ ] **Step 3: Implement explicit eligibility and boundary selection**

Add:

```cpp
bool is_dispatcher_v0_eligible(R5900Instruction instruction) noexcept {
    switch (instruction) {
    case R5900Instruction::Nop:
    case R5900Instruction::Addu:
    case R5900Instruction::Addiu:
    case R5900Instruction::Ori:
        return true;
    default:
        return false;
    }
}
```

For each `block.instructions` site, classify control flow and system/trap before eligibility. Store eligible sites in `std::vector<analysis::R5900InstructionSite> prefix`. Keep `std::optional<R5900DispatchStopReason> boundary_reason` and `boundary_pc`.

```cpp
if (site.decoded.is_branch() || site.decoded.is_jump()) {
    boundary_reason = R5900DispatchStopReason::ControlFlow;
    boundary_pc = site.pc;
    break;
}
if (site.decoded.instruction_class == R5900InstructionClass::System) {
    boundary_reason = R5900DispatchStopReason::Trap;
    boundary_pc = site.pc;
    break;
}
if (!is_dispatcher_v0_eligible(site.decoded.instruction)) {
    boundary_reason = R5900DispatchStopReason::UnsupportedInstruction;
    boundary_pc = site.pc;
    break;
}
prefix.push_back(site);
```

Never append `block.delay_slot`. If `prefix.empty()`, return the boundary with `next_pc = boundary_pc` and zero mutation/accounting.

- [ ] **Step 4: Implement complete lowering, compile, and one-candidate execution**

Build one IR vector from the complete prefix:

```cpp
std::vector<R5900IrInstruction> ir;
for (const auto& site : prefix) {
    const auto lowered = lower_r5900_instruction(site.decoded, site.pc);
    if (!lowered.ok()) {
        result.reason = R5900DispatchStopReason::LoweringFailure;
        result.next_pc = site.pc;
        result.message = format_stage_error("lowering", site.pc, lowered.message);
        return result;
    }
    ir.insert(ir.end(), lowered.instructions.begin(), lowered.instructions.end());
}
```

Compile only after all selected instructions lower:

```cpp
auto compiled = compile_r5900_ir_x64(ir);
if (!compiled.ok()) {
    result.reason = R5900DispatchStopReason::CompileFailure;
    result.next_pc = prefix.front().pc;
    result.message = format_stage_error("x64 compile", prefix.front().pc, compiled.message);
    return result;
}

compiled.block->execute(state);
result.blocks_executed = 1u;
result.instructions_executed = prefix.size();
result.next_pc = prefix.front().pc + static_cast<std::uint32_t>(prefix.size() * 4u);
```

If a semantic boundary follows the prefix, return that reason even when the consumed budget is 1. If analysis ended with `InstructionLimit`, return `BlockBudgetExhausted` after this one execution for Task 2.

- [ ] **Step 5: Run GREEN and commit**

```powershell
ctest --test-dir build -C Release -R r5900_block_dispatcher_windows_tests --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

Expected: all tests pass.

```bash
git add src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "feat: execute supported R5900 block prefixes"
```

---

### Task 3: Multi-block run loop and native cache reuse

**Files:**
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Modify: `tests/r5900_block_dispatcher_windows_tests.cpp`

**Interfaces:**
- Consumes: Task 2 prefix selection and compile path.
- Produces: repeated `InstructionLimit` dispatch, budget accounting, start-PC cache reuse, `cache_size()`, `clear_cache()`.

- [ ] **Step 1: Write failing multi-block budget tests**

Use five eligible words followed by unsupported ANDI:

```cpp
const std::vector<std::uint32_t> words = {
    i_type(0x09, 0, 1, 1u),
    i_type(0x09, 1, 1, 1u),
    i_type(0x09, 1, 1, 1u),
    i_type(0x09, 1, 1, 1u),
    i_type(0x09, 1, 1, 1u),
    i_type(0x0c, 1, 2, 0xffu),
};
R5900BlockDispatcherOptions options{};
options.block_options.max_instructions = 2u;
```

With fresh dispatcher/state instances validate:

- budget 1 -> 2 instructions, one block, `next_pc == base + 8`, `BlockBudgetExhausted`;
- budget 2 -> 4 instructions, two blocks, `next_pc == base + 16`, `BlockBudgetExhausted`;
- budget 3 -> fifth supported instruction executes, then `UnsupportedInstruction` at `base + 20`, three blocks, five instructions; boundary wins over exhausted budget.

- [ ] **Step 2: Write failing cache hit/clear tests**

With `max_instructions = 2` and two eligible words, run twice from the same PC using separate execution states:

```cpp
const auto first = dispatcher.run(base, state1, 1u);
expect(first.cache_misses == 1u && first.cache_hits == 0u,
       "first native candidate must be a cache miss");
expect(dispatcher.cache_size() == 1u,
       "successful compile must populate one cache entry");

const auto second = dispatcher.run(base, state2, 1u);
expect(second.cache_hits == 1u && second.cache_misses == 0u,
       "identical native candidate must hit cache");
expect(dispatcher.cache_size() == 1u,
       "cache hit must not duplicate native entry");

dispatcher.clear_cache();
expect(dispatcher.cache_size() == 0u,
       "clear_cache must destroy all native entries");
```

- [ ] **Step 3: Run RED and commit tests**

```powershell
ctest --test-dir build -C Release -R r5900_block_dispatcher_windows_tests --output-on-failure
```

Expected: failures because Task 2 executes at most one candidate and retains no compiled block.

```bash
git add tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "test: cover dispatcher budgets and cache reuse"
```

- [ ] **Step 4: Implement run loop and exact-word cache reuse**

Initialize `current_pc = start_pc`. Each successful native execution updates cumulative metrics:

```cpp
++result.blocks_executed;
result.instructions_executed += prefix.size();
current_pc += static_cast<std::uint32_t>(prefix.size() * 4u);
result.next_pc = current_pc;
```

If a semantic boundary was identified in the just-analyzed block, return it immediately. Otherwise the chunk ended only because of `InstructionLimit`. Return `BlockBudgetExhausted` when `result.blocks_executed == max_blocks`; otherwise analyze again at `current_pc`.

Before compilation, capture the selected words from the analyzed sites:

```cpp
std::vector<std::uint32_t> guest_words;
guest_words.reserve(prefix.size());
for (const auto& site : prefix) {
    guest_words.push_back(site.decoded.raw);
}
```

Task 3 cache-hit condition is exact `guest_words` plus instruction count. Keep `fingerprint = 0` until Task 4.

No entry:

```cpp
++result.cache_misses;
auto compiled = compile_r5900_ir_x64(ir);
if (!compiled.ok()) {
    result.reason = R5900DispatchStopReason::CompileFailure;
    result.next_pc = current_pc;
    result.message = format_stage_error("x64 compile", current_pc, compiled.message);
    return result;
}

CachedBlock cached{};
cached.start_pc = current_pc;
cached.end_pc_exclusive = current_pc + static_cast<std::uint32_t>(prefix.size() * 4u);
cached.guest_words = guest_words;
cached.guest_instruction_count = guest_words.size();
cached.native_block = std::move(*compiled.block);
auto [inserted, did_insert] = cache_.emplace(current_pc, std::move(cached));
(void)did_insert;
inserted->second.native_block.execute(state);
```

Exact Task 3 hit:

```cpp
auto cached = cache_.find(current_pc);
if (cached != cache_.end() &&
    cached->second.guest_instruction_count == guest_words.size() &&
    cached->second.guest_words == guest_words) {
    ++result.cache_hits;
    cached->second.native_block.execute(state);
}
```

Do not execute a mismatched entry. Task 4 supplies stale replacement semantics.

- [ ] **Step 5: Run GREEN and commit**

```powershell
ctest --test-dir build -C Release -R r5900_block_dispatcher_windows_tests --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

Expected: all tests pass.

```bash
git add src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "feat: add R5900 dispatcher run loop and cache"
```

---

### Task 4: FNV fingerprint and automatic stale-code recompilation

**Files:**
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp`
- Modify: `tests/r5900_block_dispatcher_windows_tests.cpp`

**Interfaces:**
- Consumes: Task 3 start-PC cache.
- Produces: spec-defined FNV-1a validation, exact-word collision guard, `recompilations` accounting, stale-code rejection.

- [ ] **Step 1: Write failing stale-code tests**

Configure one instruction per analyzer chunk:

```cpp
R5900BlockDispatcherOptions options{};
options.block_options.max_instructions = 1u;
const auto addiu_one = i_type(0x09, 0, 1, 1u);
const auto addiu_seven = i_type(0x09, 0, 1, 7u);
auto memory = make_memory({addiu_one}, base);
R5900BlockDispatcher dispatcher(memory, options);
```

First run must yield `r1 == 1`, `cache_misses == 1`, `recompilations == 0`.

Mutate through the same non-const memory object:

```cpp
expect(memory.write_u32(base, addiu_seven),
       "guest code mutation must succeed");
```

Run again from reset state and expect:

```cpp
expect(state2.gpr[1].low64 == 7u,
       "stale native code must never execute");
expect(second.recompilations == 1u,
       "changed guest code must count as recompilation");
expect(second.cache_hits == 0u && second.cache_misses == 0u,
       "stale lookup is neither hit nor plain miss");
expect(dispatcher.cache_size() == 1u,
       "successful replacement keeps one entry per start PC");
```

Then mutate the same PC to decoded unsupported ANDI. Run again and expect `UnsupportedInstruction`, zero executed blocks/instructions, zero cache hit/miss/recompile for that call, and unchanged execution state. The old physical cache entry may remain but must not execute.

- [ ] **Step 2: Run RED and commit tests**

```powershell
ctest --test-dir build -C Release -R r5900_block_dispatcher_windows_tests --output-on-failure
```

Expected: stale-code accounting/behavior fails under Task 3 cache rules.

```bash
git add tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "test: require dispatcher cache invalidation"
```

- [ ] **Step 3: Implement deterministic FNV-1a**

Add:

```cpp
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void fnv_byte(std::uint64_t& hash, std::uint8_t byte) noexcept {
    hash ^= byte;
    hash *= kFnvPrime;
}

void fnv_u32_le(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        fnv_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void fnv_u64_le(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        fnv_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

std::uint64_t fingerprint_guest_words(std::uint32_t start_pc,
                                      const std::vector<std::uint32_t>& words) noexcept {
    std::uint64_t hash = kFnvOffset;
    fnv_u32_le(hash, start_pc);
    fnv_u64_le(hash, static_cast<std::uint64_t>(words.size()));
    for (const auto word : words) {
        fnv_u32_le(hash, word);
    }
    return hash;
}
```

Before cache validation, re-read every selected word:

```cpp
std::vector<std::uint32_t> guest_words;
guest_words.reserve(prefix.size());
for (const auto& site : prefix) {
    const auto word = memory_.read_u32(site.pc);
    if (!word.has_value()) {
        result.reason = R5900DispatchStopReason::AnalysisFailure;
        result.next_pc = site.pc;
        result.message = format_stage_error(
            "analysis", site.pc, "selected guest instruction became unreadable");
        return result;
    }
    guest_words.push_back(*word);
}
const auto fingerprint = fingerprint_guest_words(current_pc, guest_words);
```

- [ ] **Step 4: Implement exact hit/miss/stale transaction**

Exact hit requires:

```cpp
const bool exact_match =
    cached.start_pc == current_pc &&
    cached.guest_instruction_count == guest_words.size() &&
    cached.fingerprint == fingerprint &&
    cached.guest_words == guest_words;
```

Rules:

```text
no cache entry                  -> ++cache_misses, compile, insert
exact current candidate match   -> ++cache_hits, execute cached block
existing entry but mismatch     -> ++recompilations, compile replacement, never execute old block
no non-empty eligible prefix    -> return boundary before cache lookup
```

For stale replacement, compile into a temporary result first. Only after success assign metadata and move the new native block into the existing cache entry:

```cpp
CachedBlock replacement{};
replacement.start_pc = current_pc;
replacement.end_pc_exclusive = current_pc + static_cast<std::uint32_t>(prefix.size() * 4u);
replacement.fingerprint = fingerprint;
replacement.guest_words = guest_words;
replacement.guest_instruction_count = guest_words.size();
replacement.native_block = std::move(*compiled.block);
cache_[current_pc] = std::move(replacement);
cache_.at(current_pc).native_block.execute(state);
```

If lowering or compile fails, return the corresponding stop reason without executing the candidate. An older physically retained stale block must never be selected by exact-match validation.

- [ ] **Step 5: Run GREEN and commit**

```powershell
ctest --test-dir build -C Release -R r5900_block_dispatcher_windows_tests --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

Expected: all tests pass.

```bash
git add src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "feat: invalidate stale R5900 native blocks"
```

---

### Task 5: Differential state semantics and complete failure coverage

**Files:**
- Modify: `tests/r5900_block_dispatcher_windows_tests.cpp`
- Modify: `src/recompiler/windows/r5900_block_dispatcher.cpp` only if these tests expose a spec violation.

**Interfaces:**
- Consumes: completed dispatcher behavior from Tasks 1-4.
- Produces: full-state equivalence evidence and final diagnostics/error-atomicity coverage.

- [ ] **Step 1: Add all-32-GPR differential test**

Initialize every GPR low/high half distinctly:

```cpp
R5900IrExecutionState initial{};
for (std::size_t index = 0; index < initial.gpr.size(); ++index) {
    initial.gpr[index].low64 =
        0x0101010101010101ull * static_cast<std::uint64_t>(index + 1u);
    initial.gpr[index].high64 =
        0xf000000000000000ull | static_cast<std::uint64_t>(index);
}
initial.gpr[0] = {0xffffffffffffffffull, 0xffffffffffffffffull};
initial.gpr[29].low64 = 0x1000u;
```

Use:

```cpp
const std::vector<std::uint32_t> words = {
    0u,
    r_type(9, 10, 8, 0, 0x21),
    i_type(0x09, 29, 29, 0xfff0),
    i_type(0x0d, 4, 5, 0xff00),
};
```

Build independent reference IR:

```cpp
std::vector<R5900IrInstruction> reference_ir;
for (std::size_t index = 0; index < words.size(); ++index) {
    const auto pc = base + static_cast<std::uint32_t>(index * 4u);
    const auto lowered = lower_r5900_instruction(decode_r5900(words[index]), pc);
    expect(lowered.ok(), "differential fixture must lower in reference path");
    reference_ir.insert(reference_ir.end(),
                        lowered.instructions.begin(), lowered.instructions.end());
}

auto expected = initial;
auto actual = initial;
expect(execute_r5900_ir(reference_ir, expected).ok(),
       "reference executor must accept dispatcher differential IR");
```

Set analyzer `max_instructions = words.size()`, run dispatcher with budget 1, and compare every low/high half exactly. Expect `BlockBudgetExhausted`, one block, four instructions. This covers GPR0 normalization, high64 preservation, ADDIU aliasing, and native/reference equivalence.

- [ ] **Step 2: Add unmapped, non-executable, and analyzer-option failures**

Create non-executable mapped memory with `make_memory({0u}, base, 6u)` and expect `AnalysisFailure`, zero execution, `next_pc == base`.

For unmapped failure, use a valid executable map at `base` and call `run(base + 0x1000u, state, 1u)`. Expect `AnalysisFailure`, zero execution, exact failing `next_pc`.

For invalid analyzer instruction limit:

```cpp
R5900BlockDispatcherOptions invalid_options{};
invalid_options.block_options.max_instructions = 0u;
R5900BlockDispatcher invalid_dispatcher(memory, invalid_options);
const auto invalid = invalid_dispatcher.run(base, state, 1u);
expect(invalid.reason == R5900DispatchStopReason::AnalysisFailure,
       "zero analyzer instruction limit must map to analysis failure");
```

For each analysis failure, assert `message.find("analysis") != std::string::npos` and the expected eight-digit hexadecimal PC substring is present.

- [ ] **Step 3: Verify error atomicity after prior progress**

Use `max_instructions = 1` with one supported ADDIU at `base` and an unmapped sequential PC by making the executable segment contain exactly one word. Call `run(base, state, 2u)`. Expect:

```cpp
expect(result.reason == R5900DispatchStopReason::AnalysisFailure,
       "later analysis failure must preserve earlier progress");
expect(result.blocks_executed == 1u && result.instructions_executed == 1u,
       "earlier native block must remain committed");
expect(state.gpr[1].low64 == 1u,
       "earlier state mutation must remain committed");
expect(result.next_pc == base + 4u,
       "later failure must report first unprocessed PC");
```

Do not add production-only failure injection for unreachable `LoweringFailure` or `CompileFailure`; the production code must still map those internal results correctly if they occur. This preserves YAGNI and the spec's “where practical” test language.

- [ ] **Step 4: Run complete Windows regression**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\frame_pacer_windows_tests.exe
.\build\Release\Burnout3PacingProbe.exe --seconds 1
```

Expected:

- configure/build success;
- all previous 27 tests plus dispatcher test pass, for 28 total unless the repository independently gained another test;
- `frame_pacer_windows_tests` remains green;
- pacing probe remains at 120 Hz / approximately 8.333 ms cadence.

Commit final test/code state:

```bash
git add src/recompiler/windows/r5900_block_dispatcher.cpp tests/r5900_block_dispatcher_windows_tests.cpp
git commit -m "test: validate R5900 dispatcher semantics"
```

- [ ] **Step 5: Verify the exact feature commit in GitHub Actions**

The workflow `.github/workflows/windows-ci.yml` runs Windows Server 2022, Visual Studio 17 2022 x64, Release build, full CTest, frame pacing telemetry, and one-second pacing probe.

Expected: conclusion `success` for the exact Task 5 head SHA. Record the workflow run ID and commit SHA for Task 6.

---

### Task 6: Documentation and final branch validation

**Files:**
- Modify: `README.md`
- Modify: `PROGRESS.md`

**Interfaces:**
- Consumes: exact green CI run ID and SHA from Task 5.
- Produces: accurate milestone status without overstating game execution.

- [ ] **Step 1: Update README capability statement**

Add a concise status paragraph stating that Windows now has a minimal R5900 native block dispatcher providing:

```text
CFG-driven supported-prefix selection
NOP/ADDU/ADDIU/ORI current subset
compile-on-demand
native cache reuse
guest-word FNV fingerprint + exact-word invalidation
explicit next_pc, stop reason, and block budget
```

The same paragraph must state that branches/jumps/calls, delay slots, guest loads/stores, and real Burnout 3 ELF/game execution are not implemented by this milestone.

- [ ] **Step 2: Update PROGRESS with exact evidence**

Add/update the row:

```text
R5900 native block dispatcher v0 | CI_VALIDATED
```

Record the exact Task 5 Windows CI run ID and head SHA. Describe the proven pipeline as:

```text
guest memory -> CFG/basic block -> current IR -> x64 compile-on-demand -> native execute -> cache/reuse/invalidate
```

Do not increase graphics, audio, input, menu, race, or gameplay completion based on this infrastructure milestone.

- [ ] **Step 3: Commit documentation**

```bash
git add README.md PROGRESS.md
git commit -m "docs: record R5900 block dispatcher validation"
```

- [ ] **Step 4: Verify final documentation head CI**

Verify the exact documentation-head SHA has Windows CI conclusion `success`, with the full CTest suite green and pacing gates unchanged. Do not claim completion before this concrete result exists.

- [ ] **Step 5: Final review checklist**

```text
[ ] cache keyed by start_pc and validated by instruction count + FNV + exact words
[ ] guest words re-read before cache validation
[ ] automatic stale-code recompilation
[ ] no stale native execution
[ ] max_blocks counts native executions only
[ ] semantic boundary beats budget exhaustion
[ ] next_pc is first unexecuted guest PC
[ ] no branch/jump/call/trap/delay-slot execution
[ ] no guest load/store execution
[ ] GPR semantics match reference executor bit-for-bit
[ ] dispatcher allocates no executable pages itself
[ ] clear_cache destroys entries through existing backend RAII
[ ] candidate failures do not execute the failing candidate
[ ] earlier successful blocks remain committed if a later candidate fails
[ ] all Windows tests green
[ ] README/PROGRESS state limitations explicitly
```

After this checklist passes, the branch is ready for code review/integration. It must still be described as a straight-line native-dispatch infrastructure milestone, not real Burnout 3 guest execution and not a completed recompiler.
