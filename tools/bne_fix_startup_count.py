from pathlib import Path

path = Path('tests/r5900_block_dispatcher_startup_windows_tests.cpp')
text = path.read_text(encoding='utf-8')
old = '''    expect(result.blocks_executed == 8u,\n           "synthetic startup must execute eight native blocks");\n'''
new = '''    expect(result.blocks_executed == 7u,\n           "synthetic startup must execute seven native blocks with BNE in the final block");\n'''
if text.count(old) != 1:
    raise SystemExit('expected startup block-count assertion not found')
text = text.replace(old, new, 1)
text = text.replace('stop=0x001001cc blocks=8 instructions=96',
                    'stop=0x001001cc blocks=7 instructions=96', 1)
path.write_text(text, encoding='utf-8')
