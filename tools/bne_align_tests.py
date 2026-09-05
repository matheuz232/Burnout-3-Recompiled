from pathlib import Path

# Direct-transfer fixtures: use BNEL (branch-likely, intentionally outside v0)
# as the post-target boundary now that ordinary BNE is supported.
path = Path('tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp')
text = path.read_text(encoding='utf-8')
text = text.replace(
    '    const auto bne_boundary = i_type(0x05u, 0u, 0u, 0u);\n',
    '    const auto bnel_boundary = i_type(0x15u, 0u, 0u, 0u);\n',
    1)
if 'bne_boundary' not in text:
    raise SystemExit('expected BNE boundary uses not found')
text = text.replace('bne_boundary', 'bnel_boundary')
text = text.replace('unsupported target JR', 'unsupported target BNEL')
path.write_text(text, encoding='utf-8')

# Startup: supported BNE r0,r0 is not taken, executes its NOP delay, then the
# next analysis begins just beyond the synthetic executable region at 0x1cc.
path = Path('tests/r5900_block_dispatcher_startup_windows_tests.cpp')
text = path.read_text(encoding='utf-8')
repls = [
    ('    expect(result.reason == R5900DispatchStopReason::ControlFlow,\n           "synthetic startup must stop at unsupported BNE boundary");\n',
     '    expect(result.reason == R5900DispatchStopReason::AnalysisFailure,\n           "synthetic startup must stop when analysis reaches the end of the fixture");\n'),
    ('    expect(result.next_pc == 0x001001c4u,\n           "synthetic startup must stop at BNE PC");\n',
     '    expect(result.next_pc == 0x001001ccu,\n           "synthetic startup must advance beyond BNE and its delay slot");\n'),
    ('    expect(result.blocks_executed == 7u,\n           "synthetic startup must execute seven native blocks");\n',
     '    expect(result.blocks_executed == 8u,\n           "synthetic startup must execute eight native blocks");\n'),
    ('    expect(result.instructions_executed == 94u,\n           "synthetic startup must execute ninety-four guest instructions");\n',
     '    expect(result.instructions_executed == 96u,\n           "synthetic startup must execute ninety-six guest instructions");\n'),
    ('    std::cout << "SYNTHETIC_STARTUP_JR_JALR_VALIDATED sq=0x00100160 target=0x004e2680 "\n                 "stop=0x001001c4 blocks=7 instructions=94\\n";\n',
     '    std::cout << "SYNTHETIC_STARTUP_BNE_VALIDATED sq=0x00100160 target=0x004e2680 "\n                 "stop=0x001001cc blocks=8 instructions=96\\n";\n'),
]
for old, new in repls:
    if text.count(old) != 1:
        raise SystemExit(f'expected startup expectation site not found: {old[:60]!r}')
    text = text.replace(old, new, 1)
path.write_text(text, encoding='utf-8')
