from pathlib import Path

path = Path("tests/r5900_block_dispatcher_startup_windows_tests.cpp")
text = path.read_text(encoding="utf-8")
old = '''    const auto result = dispatcher.run(base, state, 5u);
    expect(result.reason == R5900DispatchStopReason::ControlFlow,
           "startup must stop at unsupported JR");
    expect(result.next_pc == kIndirectReturnPc,
           "startup JR boundary mismatch");
    expect(result.blocks_executed == 5u,
           "startup block count mismatch");
    expect(result.instructions_executed == 87u,
           "startup instruction count mismatch");
'''
new = '''    const auto result = dispatcher.run(base, state, 8u);
    expect(result.reason == R5900DispatchStopReason::ControlFlow,
           "synthetic startup must stop at unsupported BNE boundary");
    expect(result.next_pc == 0x001001c4u,
           "synthetic startup must stop at BNE PC");
    expect(result.blocks_executed == 7u,
           "synthetic startup must execute seven native blocks");
    expect(result.instructions_executed == 94u,
           "synthetic startup must execute ninety-four guest instructions");
'''
if text.count(old) != 1:
    raise SystemExit("old startup terminal assertions missing or duplicated")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("Task 5 RED assertions applied")
