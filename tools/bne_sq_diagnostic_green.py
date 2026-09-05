from pathlib import Path

path = Path('src/recompiler/windows/r5900_block_dispatcher.cpp')
text = path.read_text(encoding='utf-8')
old = '''                        has_supported_beq\n                            ? "SQ in a BEQ delay slot is outside dispatcher v0 scope"\n'''
new = '''                        (has_supported_beq || has_supported_bne)\n                            ? "SQ in a BEQ/BNE delay slot is outside dispatcher v0 scope"\n'''
if text.count(old) != 1:
    raise SystemExit(f'expected one BEQ SQ diagnostic site, found {text.count(old)}')
path.write_text(text.replace(old, new, 1), encoding='utf-8')
