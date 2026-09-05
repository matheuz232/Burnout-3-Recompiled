from pathlib import Path

# 1) Add likely-branch semantics to the reference block executor.
executor = Path("src/recompiler/r5900_ir_executor.cpp")
text = executor.read_text(encoding="utf-8")
anchor = """    case R5900IrTerminatorKind::DirectJump: {\n"""
addition = """    case R5900IrTerminatorKind::BranchEqualLikely64:\n    case R5900IrTerminatorKind::BranchNotEqualLikely64: {\n        const bool equal =\n            state.gpr[block.terminator.inputs[0].gpr_index].low64 ==\n            state.gpr[block.terminator.inputs[1].gpr_index].low64;\n        const bool taken =\n            block.terminator.kind == R5900IrTerminatorKind::BranchEqualLikely64\n                ? equal\n                : !equal;\n\n        if (!taken) {\n            return {R5900IrExecutionError::None,\n                    {},\n                    block.terminator.fallthrough_pc};\n        }\n\n        const auto delay_result =\n            execute_ir_sequence(block.terminator.delay_slot, context);\n        if (!delay_result.ok()) {\n            return map_block_execution_failure(delay_result);\n        }\n\n        return {R5900IrExecutionError::None,\n                {},\n                block.terminator.taken_pc};\n    }\n\n"""
if anchor not in text:
    raise SystemExit("executor insertion anchor not found")
executor.write_text(text.replace(anchor, addition + anchor, 1), encoding="utf-8")

# 2) Dedicated reference-executor coverage.
test = Path("tests/r5900_ir_branch_likely_executor_tests.cpp")
test.write_text(r'''#include "r5900_branch_likely_test_support.h"
#include "recompiler/r5900_ir_executor.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {
using namespace b3r::recompiler;
using namespace b3r::test_support;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_ir_branch_likely_executor_tests: FAIL: "
              << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) fail(message);
}

struct MemoryProbe {
    bool succeed{true};
    std::size_t calls{};
};

bool write128(void* user,
              std::uint32_t,
              std::uint64_t,
              std::uint64_t) noexcept {
    auto& probe = *static_cast<MemoryProbe*>(user);
    ++probe.calls;
    return probe.succeed;
}

R5900IrExecutionContext context_for(R5900IrExecutionState& state,
                                    MemoryProbe& probe) {
    R5900IrExecutionContext context{};
    context.state = &state;
    context.memory.user = &probe;
    context.memory.write128 = &write128;
    return context;
}
} // namespace

int main() {
    {
        const auto block = branch_equal_likely(
            0x0010c000u, 4u, 5u, 0x0010c100u,
            addiu(8u, 8u, 1, 0x0010c004u));
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 7u;
        state.gpr[5].low64 = 7u;
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok() && result.next_pc == 0x0010c100u &&
                   state.gpr[8].low64 == 1u,
               "BEQL taken must execute delay exactly once");
    }

    {
        const auto block = branch_equal_likely(
            0x0010c000u, 4u, 5u, 0x0010c100u,
            addiu(8u, 8u, 1, 0x0010c004u));
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 7u;
        state.gpr[5].low64 = 8u;
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok() && result.next_pc == 0x0010c008u &&
                   state.gpr[8].low64 == 0u,
               "BEQL not-taken must annul delay");
    }

    {
        const auto block = branch_not_equal_likely(
            0x0010c200u, 4u, 5u, 0x0010c300u,
            addiu(8u, 8u, 1, 0x0010c204u));
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 7u;
        state.gpr[5].low64 = 8u;
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok() && result.next_pc == 0x0010c300u &&
                   state.gpr[8].low64 == 1u,
               "BNEL taken must execute delay exactly once");
    }

    {
        const auto block = branch_not_equal_likely(
            0x0010c200u, 4u, 5u, 0x0010c300u,
            addiu(8u, 8u, 1, 0x0010c204u));
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 7u;
        state.gpr[5].low64 = 7u;
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok() && result.next_pc == 0x0010c208u &&
                   state.gpr[8].low64 == 0u,
               "BNEL not-taken must annul delay");
    }

    {
        const auto block = branch_equal_likely(
            0x0010c400u, 4u, 5u, 0x0010c500u,
            addiu(4u, 0u, 9, 0x0010c404u));
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 3u;
        state.gpr[5].low64 = 3u;
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok() && result.next_pc == 0x0010c500u &&
                   state.gpr[4].low64 == 9u,
               "BEQL predicate must be captured before delay mutates source");
    }

    {
        const auto block = branch_not_equal_likely(
            0x0010c600u, 4u, 5u, 0x0010c700u,
            addiu(9u, 9u, 1, 0x0010c604u));
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 11u;
        state.gpr[5].low64 = 11u;
        state.gpr[9] = {0x4455u, 0x9988776655443322ull};
        const auto before = state.gpr[9];
        const auto result = execute_r5900_ir_block(block, state);
        expect(result.ok() && result.next_pc == 0x0010c608u &&
                   state.gpr[9].low64 == before.low64 &&
                   state.gpr[9].high64 == before.high64,
               "annulled delay must not mutate GPR state");
    }

    {
        const auto block = branch_equal_likely(
            0x0010c800u, 4u, 5u, 0x0010c900u,
            store128(2u, 3u, 0, 0x0010c804u));
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 1u;
        state.gpr[5].low64 = 2u;
        state.gpr[2].low64 = 0x00004000u;
        state.gpr[3] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
        MemoryProbe probe{};
        probe.succeed = false;
        auto context = context_for(state, probe);
        const auto result = execute_r5900_ir_block(block, context);
        expect(result.ok() && result.next_pc == 0x0010c808u,
               "not-taken BEQL must ignore failing Store128 delay");
        expect(probe.calls == 0u && !context.memory_fault.active,
               "annulled Store128 delay must not call helper or create fault");
    }

    {
        const auto block = branch_not_equal_likely(
            0x0010ca00u, 4u, 5u, 0x0010cb00u,
            store128(2u, 3u, 0, 0x0010ca04u));
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 1u;
        state.gpr[5].low64 = 2u;
        state.gpr[2].low64 = 0x00004000u;
        state.gpr[3] = {0x1111222233334444ull, 0xaaaabbbbccccddddull};
        MemoryProbe probe{};
        probe.succeed = false;
        auto context = context_for(state, probe);
        const auto result = execute_r5900_ir_block(block, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "taken BNEL must propagate failing Store128 delay");
        expect(probe.calls == 1u && context.memory_fault.active &&
                   context.memory_fault.guest_pc == 0x0010ca04u &&
                   context.memory_fault.width_bytes == 16u,
               "taken likely delay fault details mismatch");
    }

    {
        auto block = branch_equal_likely(
            0x0010cc04u, 4u, 5u, 0x0010cd00u,
            addiu(10u, 10u, 1, 0x0010cc08u));
        block.body = {store128(2u, 3u, 0, 0x0010cc00u)};
        R5900IrExecutionState state{};
        state.gpr[4].low64 = 5u;
        state.gpr[5].low64 = 5u;
        state.gpr[10].low64 = 77u;
        const auto before_delay = state.gpr[10].low64;
        R5900IrExecutionContext context{};
        context.state = &state;
        const auto result = execute_r5900_ir_block(block, context);
        expect(result.error == R5900IrExecutionError::MemoryAccessFailure,
               "body failure must precede likely predicate semantics");
        expect(state.gpr[10].low64 == before_delay,
               "body failure must prevent likely delay execution");
        expect(context.memory_fault.active &&
                   context.memory_fault.guest_pc == 0x0010cc00u,
               "body failure must report body fault PC");
    }

    std::cout << "r5900_ir_branch_likely_executor_tests: PASS\n";
    return EXIT_SUCCESS;
}
''', encoding="utf-8")

# 3) Register dedicated test.
cmake = Path("CMakeLists.txt")
text = cmake.read_text(encoding="utf-8")
anchor = "  add_executable(ps2_memory_map_tests tests/ps2_memory_map_tests.cpp)\n"
addition = """  add_executable(r5900_ir_branch_likely_executor_tests\n    tests/r5900_ir_branch_likely_executor_tests.cpp\n  )\n  target_link_libraries(r5900_ir_branch_likely_executor_tests PRIVATE b3r_recompiler)\n  add_test(NAME r5900_ir_branch_likely_executor_tests\n    COMMAND r5900_ir_branch_likely_executor_tests)\n\n"""
if anchor not in text:
    raise SystemExit("CMake executor insertion anchor not found")
cmake.write_text(text.replace(anchor, addition + anchor, 1), encoding="utf-8")

# 4) Remove temporary RED from the existing indirect-transfer executor test,
#    and restore its original support include.
old = Path("tests/r5900_ir_indirect_transfer_executor_tests.cpp")
text = old.read_text(encoding="utf-8")
text = text.replace('#include "r5900_branch_likely_test_support.h"',
                    '#include "r5900_direct_transfer_test_support.h"', 1)
start_marker = "    // Task 2 RED: likely terminators validate, but executor semantics do not exist yet.\n"
end_marker = '    std::cout << "r5900_ir_indirect_transfer_executor_tests: PASS\\n";'
if start_marker not in text or end_marker not in text:
    raise SystemExit("temporary Task 2 RED anchors not found")
start = text.index(start_marker)
end = text.index(end_marker, start)
old.write_text(text[:start] + text[end:], encoding="utf-8")
