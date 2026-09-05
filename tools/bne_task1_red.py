from pathlib import Path

# One-shot RED patch for BNE dispatcher support.
path = Path('tests/r5900_block_dispatcher_windows_tests.cpp')
text = path.read_text(encoding='utf-8')
old = '''        const auto result = dispatcher.run(base, state, 1u);\n        expect(result.reason == R5900DispatchStopReason::ControlFlow,\n               \"supported prefix must stop before unsupported BNE terminator\");\n        expect(result.next_pc == base + 4u,\n               \"control-flow boundary PC must be exact\");\n        expect(result.blocks_executed == 1u && result.instructions_executed == 1u,\n               \"only straight-line prefix must execute\");\n        expect(state.gpr[1].low64 == 7u,\n               \"straight-line prefix must execute before control-flow stop\");\n        expect(state.gpr[2].low64 == 0u,\n               \"unsupported BNE delay slot must not execute\");\n'''
new = '''        const auto result = dispatcher.run(base, state, 1u);\n        expect(result.reason == R5900DispatchStopReason::BlockBudgetExhausted,\n               \"supported BNE block must consume one block budget\");\n        expect(result.next_pc == base + 12u,\n               \"not-taken BNE must continue at PC+8 after its delay slot\");\n        expect(result.blocks_executed == 1u && result.instructions_executed == 3u,\n               \"BNE block must execute prefix, terminator, and one delay instruction\");\n        expect(state.gpr[1].low64 == 7u,\n               \"straight-line prefix must execute before BNE\");\n        expect(state.gpr[2].low64 == 9u,\n               \"BNE delay slot must execute exactly once\");\n'''
if old not in text:
    raise SystemExit('expected BNE unsupported test block not found')
path.write_text(text.replace(old, new, 1), encoding='utf-8')
