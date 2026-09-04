# R5900 IR Reference Executor v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a deterministic reference executor for the existing R5900 IR v0 so decoded/lowered synthetic guest instructions can produce a verifiable EE GPR state before an x86-64 backend exists.

**Architecture:** Keep the executor isolated inside `b3r_recompiler`. It consumes the existing `R5900IrInstruction` sequence, mutates an explicit 32-register EE state represented as low/high 64-bit halves, validates each IR instruction before applying it, executes sequentially, and enforces the immutable GPR-zero invariant. The executor becomes the semantic oracle for later differential tests against native x86-64 code generation.

**Tech Stack:** C++20, CMake 3.25+, CTest, GCC/Clang host builds, Visual Studio 2022 / MSVC 19.44 on `windows-2022` CI.

**Spec:** `docs/superpowers/specs/2026-09-04-r5900-ir-reference-executor-v0-design.md`

## Global Constraints

- The public register state contains exactly 32 EE GPRs, each represented by `low64` and `high64`.
- GPR zero is architecturally immutable and must be normalized to `{0, 0}` before execution and before every return, including failures.
- Executor v0 implements only `R5900IrOpcode::Nop`, `R5900IrOpcode::AddWordSignExtend`, and `R5900IrOpcode::Or64`.
- Write operations require `R5900IrGprWriteMode::Low64PreserveUpper64` and preserve the destination `high64` exactly.
- `AddWordSignExtend` performs 32-bit wrapping addition followed by explicit sign extension to 64 bits.
- `Or64` performs full 64-bit OR on operand values already encoded by lowering.
- Execution is sequential and fail-fast: earlier valid instructions remain committed; the failing instruction and all later instructions do not mutate architectural state.
- No guest memory, branch execution, PC stepping, HI/LO, COP/MMI/VU behavior, executable memory, JIT allocation, x86-64 emission, ISO parsing, game boot, graphics, audio, input, or simulation-timing changes are part of this plan.
- No proprietary Burnout 3 code, executable, assets, dumps, or other game data may be committed.

---

## File Structure

- Create `src/recompiler/r5900_ir_executor.h`: public EE register-state types, execution error/result contract, and `execute_r5900_ir` declaration.
- Create `src/recompiler/r5900_ir_executor.cpp`: zero normalization, structural validation, operand reads, arithmetic/OR semantics, fail-fast execution loop, diagnostics.
- Create `tests/r5900_ir_executor_tests.cpp`: standalone deterministic test executable matching the repository's existing `expect`/`fail` style.
- Modify `CMakeLists.txt`: compile `r5900_ir_executor.cpp` into `b3r_recompiler`, add/register `r5900_ir_executor_tests`.
- Modify `README.md`: change the IR description from deliberately non-executable to reference-executable while making clear there is still no native backend or Burnout 3 execution.
- Modify `docs/PROGRESS.md`: record the new executor status and exact RED/GREEN/PR/post-merge CI evidence only after those runs exist.

---

### Task 1: Establish the RED executor contract

**Files:**
- Create: `tests/r5900_ir_executor_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: existing `R5900IrInstruction`, `R5900IrOpcode`, `R5900IrOperand`, `R5900IrGprWriteMode`, `decode_r5900`, and `lower_r5900_instruction`.
- Produces: a failing build that requires `recompiler/r5900_ir_executor.h` and the public types/API named in the approved spec.

- [ ] **Step 1: Add the executor test target before the implementation exists**

Add immediately after the existing `r5900_ir_tests` target in `CMakeLists.txt`:

```cmake
  add_executable(r5900_ir_executor_tests tests/r5900_ir_executor_tests.cpp)
  target_link_libraries(r5900_ir_executor_tests PRIVATE b3r_recompiler)
  add_test(NAME r5900_ir_executor_tests COMMAND r5900_ir_executor_tests)
```

Do not add `src/recompiler/r5900_ir_executor.cpp` to `b3r_recompiler` yet. The RED gate should fail because the public executor contract is absent.

- [ ] **Step 2: Write the first failing contract test**

Create `tests/r5900_ir_executor_tests.cpp` with the same lightweight style as `tests/r5900_ir_tests.cpp`:

```cpp
#include "recompiler/r5900_ir_executor.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_executor_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    R5900IrExecutionState state{};
    state.gpr[1].low64 = 0x1122334455667788ull;
    state.gpr[1].high64 = 0x8877665544332211ull;

    R5900IrInstruction nop{};
    nop.guest_pc = 0x00100000u;
    nop.opcode = R5900IrOpcode::Nop;

    const auto result = execute_r5900_ir(std::vector<R5900IrInstruction>{nop}, state);
    expect(result.ok(), "valid Nop must execute successfully");
    expect(state.gpr[1].low64 == 0x1122334455667788ull, "Nop must preserve low64");
    expect(state.gpr[1].high64 == 0x8877665544332211ull, "Nop must preserve high64");

    std::cout << "r5900_ir_executor_tests: PASS\n";
    return EXIT_SUCCESS;
}
```

- [ ] **Step 3: Run the RED gate locally**

Run:

```bash
cmake -S . -B build -DB3R_BUILD_TESTS=ON
cmake --build build --parallel
```

Expected: configure succeeds and compilation fails specifically because `recompiler/r5900_ir_executor.h` does not exist. Existing sources must not be the cause of failure.

- [ ] **Step 4: Commit the RED gate**

```bash
git add CMakeLists.txt tests/r5900_ir_executor_tests.cpp
git commit -m "test: define R5900 IR executor contract"
```

Push the branch and retain the failing Windows CI run ID as RED evidence. The Windows build should fail at the same missing-header contract rather than due to an unrelated regression.

---

### Task 2: Implement the state model and valid IR semantics

**Files:**
- Create: `src/recompiler/r5900_ir_executor.h`
- Create: `src/recompiler/r5900_ir_executor.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/r5900_ir_executor_tests.cpp`

**Interfaces:**
- Consumes: `const std::vector<R5900IrInstruction>&` and caller-owned `R5900IrExecutionState&`.
- Produces:
  - `struct R5900IrGprValue { std::uint64_t low64; std::uint64_t high64; };`
  - `struct R5900IrExecutionState { std::array<R5900IrGprValue, 32> gpr; };`
  - `enum class R5900IrExecutionError { None, MalformedInstruction, InvalidRegister, UnsupportedOpcode };`
  - `struct R5900IrExecutionResult` with `ok() const noexcept`.
  - `execute_r5900_ir(const std::vector<R5900IrInstruction>&, R5900IrExecutionState&)`.

- [ ] **Step 1: Expand tests for valid semantics before implementation**

Add helpers to the test file:

```cpp
R5900IrOperand gpr(std::uint8_t index) {
    R5900IrOperand value{};
    value.kind = R5900IrOperandKind::Gpr;
    value.gpr_index = index;
    return value;
}

R5900IrOperand immediate(std::int64_t value) {
    R5900IrOperand operand{};
    operand.kind = R5900IrOperandKind::Immediate;
    operand.immediate = value;
    return operand;
}

R5900IrInstruction write_ir(R5900IrOpcode opcode,
                            std::uint8_t destination,
                            R5900IrOperand lhs,
                            R5900IrOperand rhs,
                            std::uint32_t guest_pc) {
    R5900IrInstruction ir{};
    ir.guest_pc = guest_pc;
    ir.opcode = opcode;
    ir.destination = R5900IrRegister{destination};
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
    ir.inputs = {lhs, rhs};
    return ir;
}
```

Add explicit cases that require all approved valid semantics:

```cpp
{
    R5900IrExecutionState state{};
    state.gpr[9].low64 = 0x000000007fffffffull;
    state.gpr[10].low64 = 1u;
    state.gpr[8].high64 = 0x0123456789abcdefull;

    const auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 8, gpr(9), gpr(10), 0x00100010u);
    const auto result = execute_r5900_ir({ir}, state);
    expect(result.ok(), "word add must execute");
    expect(state.gpr[8].low64 == 0xffffffff80000000ull, "negative word result must sign-extend to 64 bits");
    expect(state.gpr[8].high64 == 0x0123456789abcdefull, "word add must preserve destination high64");
}

{
    R5900IrExecutionState state{};
    state.gpr[1].low64 = 0x00000000ffffffffull;
    const auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 2, gpr(1), immediate(1), 0x00100014u);
    const auto result = execute_r5900_ir({ir}, state);
    expect(result.ok(), "wrapping word add must execute");
    expect(state.gpr[2].low64 == 0u, "word add must wrap modulo 2^32");
}

{
    R5900IrExecutionState state{};
    state.gpr[29].low64 = 0x0000000000001000ull;
    state.gpr[29].high64 = 0xfeedfacecafebeefull;
    const auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 29, gpr(29), immediate(-16), 0x00100018u);
    const auto result = execute_r5900_ir({ir}, state);
    expect(result.ok(), "ADDIU-style immediate add must execute");
    expect(state.gpr[29].low64 == 0x0000000000000ff0ull, "signed immediate must contribute its low 32 bits to word addition");
    expect(state.gpr[29].high64 == 0xfeedfacecafebeefull, "ADDIU-style write must preserve high64");
}

{
    R5900IrExecutionState state{};
    state.gpr[4].low64 = 0x1234567800000000ull;
    state.gpr[5].high64 = 0xaabbccddeeff0011ull;
    const auto ir = write_ir(R5900IrOpcode::Or64, 5, gpr(4), immediate(0xff00), 0x0010001cu);
    const auto result = execute_r5900_ir({ir}, state);
    expect(result.ok(), "ORI-style OR must execute");
    expect(state.gpr[5].low64 == 0x123456780000ff00ull, "Or64 must operate across the full low 64-bit register value");
    expect(state.gpr[5].high64 == 0xaabbccddeeff0011ull, "Or64 must preserve high64");
}

{
    R5900IrExecutionState state{};
    state.gpr[0].low64 = 0xffffffffffffffffull;
    state.gpr[0].high64 = 0xffffffffffffffffull;
    const auto ir = write_ir(R5900IrOpcode::Or64, 0, immediate(1), immediate(2), 0x00100020u);
    const auto result = execute_r5900_ir({ir}, state);
    expect(result.ok(), "write to GPR zero must be accepted as a discarded architectural write");
    expect(state.gpr[0].low64 == 0u && state.gpr[0].high64 == 0u, "GPR zero must remain immutable zero");
}
```

Run the build. Expected: still RED because executor header/implementation are absent.

- [ ] **Step 2: Create the public executor header**

Create `src/recompiler/r5900_ir_executor.h`:

```cpp
#pragma once

#include "recompiler/r5900_ir.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace b3r::recompiler {

struct R5900IrGprValue {
    std::uint64_t low64{};
    std::uint64_t high64{};
};

struct R5900IrExecutionState {
    std::array<R5900IrGprValue, 32> gpr{};
};

enum class R5900IrExecutionError {
    None = 0,
    MalformedInstruction,
    InvalidRegister,
    UnsupportedOpcode,
};

struct R5900IrExecutionResult {
    R5900IrExecutionError error{R5900IrExecutionError::None};
    std::string message{};

    [[nodiscard]] bool ok() const noexcept {
        return error == R5900IrExecutionError::None;
    }
};

[[nodiscard]] R5900IrExecutionResult
execute_r5900_ir(const std::vector<R5900IrInstruction>& instructions,
                 R5900IrExecutionState& state);

} // namespace b3r::recompiler
```

- [ ] **Step 3: Add the executor source to `b3r_recompiler`**

Change the library source list to:

```cmake
add_library(b3r_recompiler
  src/recompiler/ps2_elf.cpp
  src/recompiler/r5900_decoder.cpp
  src/recompiler/r5900_ir.cpp
  src/recompiler/r5900_ir_executor.cpp
)
```

- [ ] **Step 4: Implement zero normalization, operand reads, and valid opcode semantics**

Create `src/recompiler/r5900_ir_executor.cpp`. Keep helpers private in an anonymous namespace. Use explicit unsigned arithmetic for deterministic wrapping and explicit bit construction for sign extension:

```cpp
#include "recompiler/r5900_ir_executor.h"

#include <cstddef>
#include <cstdint>
#include <sstream>

namespace b3r::recompiler {
namespace {

void normalize_zero(R5900IrExecutionState& state) {
    state.gpr[0] = {};
}

R5900IrExecutionResult failure(R5900IrExecutionError error,
                               std::size_t index,
                               std::uint32_t guest_pc,
                               const char* reason) {
    std::ostringstream message;
    message << "IR instruction " << index << " at guest PC 0x" << std::hex << guest_pc << ": " << reason;
    return {error, message.str()};
}

std::uint64_t read_operand_value(const R5900IrOperand& operand,
                                 const R5900IrExecutionState& state) {
    if (operand.kind == R5900IrOperandKind::Immediate) {
        return static_cast<std::uint64_t>(operand.immediate);
    }
    return state.gpr[operand.gpr_index].low64;
}

std::uint64_t sign_extend_word(std::uint32_t value) {
    if ((value & 0x80000000u) != 0u) {
        return 0xffffffff00000000ull | static_cast<std::uint64_t>(value);
    }
    return static_cast<std::uint64_t>(value);
}

} // namespace

R5900IrExecutionResult
execute_r5900_ir(const std::vector<R5900IrInstruction>& instructions,
                 R5900IrExecutionState& state) {
    normalize_zero(state);

    for (std::size_t index = 0; index < instructions.size(); ++index) {
        const auto& ir = instructions[index];

        switch (ir.opcode) {
        case R5900IrOpcode::Nop:
            break;

        case R5900IrOpcode::AddWordSignExtend: {
            const auto lhs = static_cast<std::uint32_t>(read_operand_value(ir.inputs[0], state));
            const auto rhs = static_cast<std::uint32_t>(read_operand_value(ir.inputs[1], state));
            const auto word = static_cast<std::uint32_t>(lhs + rhs);
            if (ir.destination->index != 0u) {
                state.gpr[ir.destination->index].low64 = sign_extend_word(word);
            }
            break;
        }

        case R5900IrOpcode::Or64: {
            const auto value = read_operand_value(ir.inputs[0], state) |
                               read_operand_value(ir.inputs[1], state);
            if (ir.destination->index != 0u) {
                state.gpr[ir.destination->index].low64 = value;
            }
            break;
        }

        default:
            normalize_zero(state);
            return failure(R5900IrExecutionError::UnsupportedOpcode,
                           index,
                           ir.guest_pc,
                           "unsupported opcode");
        }
    }

    normalize_zero(state);
    return {};
}

} // namespace b3r::recompiler
```

This step deliberately establishes the valid-operation mechanics first. Structural checks are added in Task 3 before the executor is considered safe to consume arbitrary IR.

- [ ] **Step 5: Run valid-semantics tests**

Run:

```bash
cmake -S . -B build -DB3R_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R "r5900_ir(_executor)?_tests"
```

Expected: both `r5900_ir_tests` and `r5900_ir_executor_tests` pass for well-formed IR.

- [ ] **Step 6: Commit the semantic core**

```bash
git add CMakeLists.txt src/recompiler/r5900_ir_executor.h src/recompiler/r5900_ir_executor.cpp tests/r5900_ir_executor_tests.cpp
git commit -m "feat: execute core R5900 IR semantics"
```

---

### Task 3: Harden structural validation and fail-fast behavior

**Files:**
- Modify: `src/recompiler/r5900_ir_executor.cpp`
- Modify: `tests/r5900_ir_executor_tests.cpp`

**Interfaces:**
- Consumes: the public executor contract from Task 2.
- Produces: explicit `MalformedInstruction`, `InvalidRegister`, and `UnsupportedOpcode` failures with instruction index and guest-PC diagnostics, without mutation by the failing instruction.

- [ ] **Step 1: Add failing validation tests**

Add test cases for each structural requirement. Construct malformed IR directly so the tests validate the executor independently of lowering:

```cpp
{
    R5900IrExecutionState state{};
    auto ir = write_ir(R5900IrOpcode::Or64, 1, gpr(32), immediate(1), 0x00100100u);
    const auto result = execute_r5900_ir({ir}, state);
    expect(result.error == R5900IrExecutionError::InvalidRegister, "source GPR >31 must fail explicitly");
}

{
    R5900IrExecutionState state{};
    auto ir = write_ir(R5900IrOpcode::Or64, 32, immediate(1), immediate(2), 0x00100104u);
    const auto result = execute_r5900_ir({ir}, state);
    expect(result.error == R5900IrExecutionError::InvalidRegister, "destination GPR >31 must fail explicitly");
}

{
    R5900IrExecutionState state{};
    R5900IrInstruction ir{};
    ir.guest_pc = 0x00100108u;
    ir.opcode = R5900IrOpcode::Or64;
    ir.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
    ir.inputs = {immediate(1), immediate(2)};
    const auto result = execute_r5900_ir({ir}, state);
    expect(result.error == R5900IrExecutionError::MalformedInstruction, "write opcode without destination must fail");
}

{
    R5900IrExecutionState state{};
    auto ir = write_ir(R5900IrOpcode::AddWordSignExtend, 1, immediate(1), immediate(2), 0x0010010cu);
    ir.inputs.pop_back();
    const auto result = execute_r5900_ir({ir}, state);
    expect(result.error == R5900IrExecutionError::MalformedInstruction, "write opcode with wrong operand count must fail");
}

{
    R5900IrExecutionState state{};
    auto ir = write_ir(R5900IrOpcode::Or64, 1, immediate(1), immediate(2), 0x00100110u);
    ir.write_mode = R5900IrGprWriteMode::None;
    const auto result = execute_r5900_ir({ir}, state);
    expect(result.error == R5900IrExecutionError::MalformedInstruction, "write opcode with wrong write mode must fail");
}

{
    R5900IrExecutionState state{};
    R5900IrInstruction nop{};
    nop.guest_pc = 0x00100114u;
    nop.opcode = R5900IrOpcode::Nop;
    nop.destination = R5900IrRegister{1};
    const auto result = execute_r5900_ir({nop}, state);
    expect(result.error == R5900IrExecutionError::MalformedInstruction, "Nop with destination must fail");
}

{
    R5900IrExecutionState state{};
    R5900IrInstruction nop{};
    nop.guest_pc = 0x00100118u;
    nop.opcode = R5900IrOpcode::Nop;
    nop.inputs = {immediate(1)};
    const auto result = execute_r5900_ir({nop}, state);
    expect(result.error == R5900IrExecutionError::MalformedInstruction, "Nop with inputs must fail");
}

{
    R5900IrExecutionState state{};
    R5900IrInstruction nop{};
    nop.guest_pc = 0x0010011cu;
    nop.opcode = R5900IrOpcode::Nop;
    nop.write_mode = R5900IrGprWriteMode::Low64PreserveUpper64;
    const auto result = execute_r5900_ir({nop}, state);
    expect(result.error == R5900IrExecutionError::MalformedInstruction, "Nop with a write mode must fail");
}

{
    R5900IrExecutionState state{};
    auto ir = write_ir(R5900IrOpcode::Or64, 1, immediate(1), immediate(2), 0x00100120u);
    ir.inputs[0].kind = static_cast<R5900IrOperandKind>(0xff);
    const auto result = execute_r5900_ir({ir}, state);
    expect(result.error == R5900IrExecutionError::MalformedInstruction, "unknown operand kind must fail explicitly");
}

{
    R5900IrExecutionState state{};
    R5900IrInstruction ir{};
    ir.guest_pc = 0x00100124u;
    ir.opcode = static_cast<R5900IrOpcode>(0xff);
    const auto result = execute_r5900_ir({ir}, state);
    expect(result.error == R5900IrExecutionError::UnsupportedOpcode, "unknown opcode must fail explicitly");
    expect(result.message.find("IR instruction 0") != std::string::npos, "error must include instruction index");
    expect(result.message.find("100124") != std::string::npos, "error must include guest PC");
}
```

Add `#include <string>` to the test file for diagnostic assertions.

Add the fail-fast transaction-boundary test:

```cpp
{
    R5900IrExecutionState state{};
    state.gpr[1].low64 = 1u;
    state.gpr[3].low64 = 0xaaaaaaaaaaaaaaaaull;

    const auto first = write_ir(R5900IrOpcode::Or64, 2, gpr(1), immediate(2), 0x00100130u);
    auto bad = write_ir(R5900IrOpcode::Or64, 3, gpr(32), immediate(4), 0x00100134u);
    const auto later = write_ir(R5900IrOpcode::Or64, 3, immediate(8), immediate(16), 0x00100138u);

    const auto result = execute_r5900_ir({first, bad, later}, state);
    expect(result.error == R5900IrExecutionError::InvalidRegister, "malformed middle instruction must stop execution");
    expect(state.gpr[2].low64 == 3u, "earlier valid instruction must remain committed");
    expect(state.gpr[3].low64 == 0xaaaaaaaaaaaaaaaaull, "failing and later instructions must not mutate state");
}
```

Run the executor test. Expected: RED because Task 2's executor indexes operands/registers before validating all malformed structures.

- [ ] **Step 2: Add validation helpers that run before each mutation**

In `r5900_ir_executor.cpp`, add these private helpers:

```cpp
R5900IrExecutionResult validate_operand(const R5900IrOperand& operand,
                                        std::size_t index,
                                        std::uint32_t guest_pc) {
    switch (operand.kind) {
    case R5900IrOperandKind::Immediate:
        return {};
    case R5900IrOperandKind::Gpr:
        if (operand.gpr_index >= 32u) {
            return failure(R5900IrExecutionError::InvalidRegister, index, guest_pc, "invalid source GPR");
        }
        return {};
    default:
        return failure(R5900IrExecutionError::MalformedInstruction, index, guest_pc, "unsupported operand kind");
    }
}

R5900IrExecutionResult validate_write(const R5900IrInstruction& ir,
                                      std::size_t index) {
    if (!ir.destination.has_value()) {
        return failure(R5900IrExecutionError::MalformedInstruction, index, ir.guest_pc, "missing destination");
    }
    if (ir.destination->index >= 32u) {
        return failure(R5900IrExecutionError::InvalidRegister, index, ir.guest_pc, "invalid destination GPR");
    }
    if (ir.write_mode != R5900IrGprWriteMode::Low64PreserveUpper64) {
        return failure(R5900IrExecutionError::MalformedInstruction, index, ir.guest_pc, "invalid GPR write mode");
    }
    if (ir.inputs.size() != 2u) {
        return failure(R5900IrExecutionError::MalformedInstruction, index, ir.guest_pc, "expected exactly two inputs");
    }
    for (const auto& operand : ir.inputs) {
        const auto validation = validate_operand(operand, index, ir.guest_pc);
        if (!validation.ok()) {
            return validation;
        }
    }
    return {};
}

R5900IrExecutionResult validate_nop(const R5900IrInstruction& ir,
                                    std::size_t index) {
    if (ir.destination.has_value() || !ir.inputs.empty() || ir.write_mode != R5900IrGprWriteMode::None) {
        return failure(R5900IrExecutionError::MalformedInstruction, index, ir.guest_pc, "malformed Nop");
    }
    return {};
}
```

- [ ] **Step 3: Validate the entire current instruction before executing it**

Restructure each switch arm so validation happens before any state read that could index a GPR and before every state mutation:

```cpp
case R5900IrOpcode::Nop: {
    const auto validation = validate_nop(ir, index);
    if (!validation.ok()) {
        normalize_zero(state);
        return validation;
    }
    break;
}

case R5900IrOpcode::AddWordSignExtend: {
    const auto validation = validate_write(ir, index);
    if (!validation.ok()) {
        normalize_zero(state);
        return validation;
    }
    const auto lhs = static_cast<std::uint32_t>(read_operand_value(ir.inputs[0], state));
    const auto rhs = static_cast<std::uint32_t>(read_operand_value(ir.inputs[1], state));
    const auto word = static_cast<std::uint32_t>(lhs + rhs);
    if (ir.destination->index != 0u) {
        state.gpr[ir.destination->index].low64 = sign_extend_word(word);
    }
    break;
}

case R5900IrOpcode::Or64: {
    const auto validation = validate_write(ir, index);
    if (!validation.ok()) {
        normalize_zero(state);
        return validation;
    }
    const auto value = read_operand_value(ir.inputs[0], state) |
                       read_operand_value(ir.inputs[1], state);
    if (ir.destination->index != 0u) {
        state.gpr[ir.destination->index].low64 = value;
    }
    break;
}
```

The destination `high64` is preserved because no write opcode assigns it.

- [ ] **Step 4: Run executor and full portable tests**

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R r5900_ir_executor_tests
ctest --test-dir build --output-on-failure
```

Expected: executor validation tests pass; all portable tests remain green.

- [ ] **Step 5: Commit validation hardening**

```bash
git add src/recompiler/r5900_ir_executor.cpp tests/r5900_ir_executor_tests.cpp
git commit -m "test: harden R5900 IR executor validation"
```

---

### Task 4: Prove the decoder → IR → executor pipeline

**Files:**
- Modify: `tests/r5900_ir_executor_tests.cpp`

**Interfaces:**
- Consumes: `decode_r5900(std::uint32_t)`, `lower_r5900_instruction(const R5900DecodedInstruction&, std::uint32_t)`, and `execute_r5900_ir(...)`.
- Produces: end-to-end synthetic evidence that real decoder output lowers into executable reference IR with deterministic final EE GPR state.

- [ ] **Step 1: Add synthetic R5900 instruction encoders to the executor test**

```cpp
constexpr std::uint32_t r_type(std::uint8_t rs,
                               std::uint8_t rt,
                               std::uint8_t rd,
                               std::uint8_t sa,
                               std::uint8_t funct) {
    return (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) |
           funct;
}

constexpr std::uint32_t i_type(std::uint8_t op,
                               std::uint8_t rs,
                               std::uint8_t rt,
                               std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           imm;
}
```

- [ ] **Step 2: Add a multi-instruction decode/lower/execute test**

Build this synthetic sequence without proprietary bytes:

```cpp
{
    R5900IrExecutionState state{};
    state.gpr[9].low64 = 5u;
    state.gpr[10].low64 = 7u;
    state.gpr[8].high64 = 0x1111222233334444ull;
    state.gpr[29].low64 = 0x1000u;
    state.gpr[29].high64 = 0xaaaabbbbccccddddull;
    state.gpr[4].low64 = 0x1234567800000000ull;
    state.gpr[5].high64 = 0x5555666677778888ull;

    const std::uint32_t words[] = {
        r_type(9, 10, 8, 0, 0x21),      // addu t0,t1,t2
        i_type(0x09, 29, 29, 0xfff0),   // addiu sp,sp,-16
        i_type(0x0d, 4, 5, 0xff00),     // ori a1,a0,0xff00
    };

    std::vector<R5900IrInstruction> program;
    std::uint32_t pc = 0x00101000u;
    for (const auto word : words) {
        const auto lowered = lower_r5900_instruction(decode_r5900(word), pc);
        expect(lowered.ok(), "synthetic guest instruction must lower for executor integration test");
        expect(lowered.instructions.size() == 1u, "each current synthetic instruction must lower to one IR op");
        program.push_back(lowered.instructions.front());
        pc += 4u;
    }

    const auto executed = execute_r5900_ir(program, state);
    expect(executed.ok(), "decoded/lowered synthetic sequence must execute");
    expect(state.gpr[8].low64 == 12u, "ADDU result must flow through decoder, IR and executor");
    expect(state.gpr[8].high64 == 0x1111222233334444ull, "ADDU pipeline must preserve high64");
    expect(state.gpr[29].low64 == 0x0ff0u, "ADDIU result must flow through decoder, IR and executor");
    expect(state.gpr[29].high64 == 0xaaaabbbbccccddddull, "ADDIU pipeline must preserve high64");
    expect(state.gpr[5].low64 == 0x123456780000ff00ull, "ORI result must flow through decoder, IR and executor");
    expect(state.gpr[5].high64 == 0x5555666677778888ull, "ORI pipeline must preserve high64");
}
```

- [ ] **Step 3: Run the focused and complete host suites**

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R "r5900_(decoder|ir|ir_executor)_tests"
ctest --test-dir build --output-on-failure
```

Expected: decoder, lowering, executor, and all existing portable tests pass.

- [ ] **Step 4: Commit the integration proof**

```bash
git add tests/r5900_ir_executor_tests.cpp
git commit -m "test: prove R5900 decode IR execution path"
```

---

### Task 5: Validate Windows CI, review, document, merge, and verify main

**Files:**
- Modify after feature CI is green: `README.md`
- Modify after feature CI is green: `docs/PROGRESS.md`

**Interfaces:**
- Consumes: completed executor and all existing Windows CI jobs.
- Produces: auditable CI evidence, reviewed PR, merged `main`, and post-merge green verification.

- [ ] **Step 1: Push the completed feature implementation and inspect Windows CI**

The repository workflow is already defined as:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Acceptance on the GREEN feature run:

- MSVC compilation succeeds with `/W4 /permissive- /Zc:__cplusplus` inherited from `b3r_recompiler`.
- `r5900_ir_executor_tests` passes.
- Every pre-existing CTest test passes.
- frame pacing telemetry still executes.
- `Burnout3PacingProbe --seconds 1` still succeeds.
- analyzer and pacing-probe package staging/validation still succeed.

If any unrelated pre-existing test regresses, stop the milestone and diagnose before documentation.

- [ ] **Step 2: Update README only after GREEN evidence exists**

Replace wording that says the IR is entirely non-executable with a precise statement equivalent to:

```markdown
The initial IR now has a deterministic reference executor for the current `Nop`, `AddWordSignExtend`, and `Or64` subset. It models all 32 EE GPRs as 128-bit values split into low/high 64-bit halves, preserves upper halves for current integer writes, enforces GPR zero, and rejects malformed IR explicitly. This executor is a semantic oracle only: there is still no x86-64 backend, native guest-code execution path, Burnout 3 boot, graphics, audio, or input implementation.
```

Do not claim a native recompiler exists yet.

- [ ] **Step 3: Update `docs/PROGRESS.md` with exact evidence**

Add a component row immediately after `R5900 IR v0`:

```markdown
| R5900 IR reference executor v0 | CI_VALIDATED | Executes `Nop`, `AddWordSignExtend`, and `Or64` against an explicit 32x128-bit EE GPR state, preserves upper 64-bit halves, enforces GPR zero, rejects malformed IR, and passes the synthetic decoder -> IR -> execution path on Windows/MSVC CI |
```

Append a validation paragraph containing the actual run IDs and actual total CTest counts observed for:

- RED missing-contract run;
- GREEN feature run;
- PR merge-ref run;
- post-merge `main` run.

Record only observed values. Do not predict run IDs or pass counts.

- [ ] **Step 4: Commit documentation**

```bash
git add README.md docs/PROGRESS.md
git commit -m "docs: record R5900 IR executor validation"
```

- [ ] **Step 5: Run pre-PR verification again**

```bash
cmake -S . -B build -DB3R_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: complete host suite passes from the final branch contents.

- [ ] **Step 6: Open the pull request**

Use title:

```text
recompiler: add R5900 IR reference executor v0
```

PR body must state:

- executor subset: `Nop`, `AddWordSignExtend`, `Or64`;
- 32 x 128-bit EE GPR state model;
- preserved high64 semantics;
- immutable GPR zero;
- structural validation/fail-fast policy;
- synthetic decoder -> lowering -> execution coverage;
- RED and GREEN Windows CI run IDs;
- explicit non-goals: no x86-64 codegen, no real ELF execution, no gameplay/runtime subsystem changes.

- [ ] **Step 7: Review the PR before merge**

Check the diff for:

- accidental mutation of `high64`;
- signed-overflow undefined behavior;
- source/destination array access before validation;
- malformed `Nop` acceptance;
- GPR-zero leakage on failure paths;
- unrelated runtime/graphics/audio/input changes;
- proprietary game data.

Address any concrete review finding and rerun CI.

- [ ] **Step 8: Require green PR CI and merge**

The PR merge-ref Windows run must pass the full workflow. Merge only after that evidence is green.

- [ ] **Step 9: Verify post-merge `main`**

After merge, inspect the new `main` Windows workflow run. The milestone is `CI_VALIDATED` only when the post-merge run passes compilation, the complete CTest suite including `r5900_ir_executor_tests`, pacing telemetry/smoke, and package validation.

- [ ] **Step 10: Record the final post-merge run in `docs/PROGRESS.md` if the merge commit changed the evidence available at documentation time**

If the documentation commit necessarily preceded the post-merge run, make a small follow-up documentation branch/PR containing the exact post-merge run ID. Do not rewrite historical evidence or claim validation before it exists.

---

## Completion Gate

The executor milestone is complete only when all of the following are true:

```text
R5900 synthetic word
        |
        v
     decoder
        |
        v
    IR lowering
        |
        v
reference executor
        |
        v
explicit EE GPR state
        |
        v
Windows/MSVC CI + post-merge main verification
```

At that point the next separate milestone is a minimal Windows x86-64 backend for the same IR subset, validated by differential state comparison against this executor. That backend is not part of this plan.
