from pathlib import Path

path = Path("tests/r5900_block_dispatcher_startup_windows_tests.cpp")
text = path.read_text(encoding="utf-8")

replacements = [
    ("words.reserve(105u);", "words.reserve(113u);"),
    ('''    words.push_back(i_type(0x09u, 0u, 25u, 2u));            // 0x188 poison
    words.push_back(i_type(0x09u, 0u, 26u, 2u));            // 0x18c poison
    words.push_back(i_type(0x09u, 0u, 27u, 2u));            // 0x190 poison
    words.push_back(i_type(0x09u, 0u, 28u, 2u));            // 0x194 poison
    words.push_back(i_type(0x09u, 0u, 30u, 2u));            // 0x198 poison
    words.push_back(0u);                                    // 0x19c guard
    words.push_back(i_type(0x09u, 0u, 24u, 0x0055u));       // 0x1a0 callee
    words.push_back(r_type(31u, 0u, 0u, 0u, 0x08u));        // 0x1a4 JR r31
    words.push_back(0u);                                    // 0x1a8 mapped JR delay
    expect(words.size() == 105u, "synthetic J/JAL fixture count mismatch");
''', '''    words.push_back(i_type(0x0fu, 0u, 5u, 0x0010u));       // 0x188 LUI r5,0x0010
    words.push_back(i_type(0x0du, 5u, 5u, 0x01c0u));       // 0x18c ORI r5,r5,0x01c0
    words.push_back(r_type(5u, 0u, 5u, 0u, 0x09u));        // 0x190 JALR r5,r5
    words.push_back(i_type(0x09u, 5u, 6u, 0u));            // 0x194 JALR delay sees link
    words.push_back(i_type(0x09u, 0u, 25u, 1u));           // 0x198 poison
    words.push_back(i_type(0x09u, 0u, 26u, 1u));           // 0x19c poison
    words.push_back(i_type(0x09u, 0u, 24u, 0x0055u));      // 0x1a0 direct callee
    words.push_back(r_type(31u, 0u, 0u, 0u, 0x08u));       // 0x1a4 JR r31
    words.push_back(i_type(0x09u, 0u, 29u, 0x0077u));      // 0x1a8 JR delay
    words.push_back(i_type(0x09u, 0u, 25u, 2u));           // 0x1ac poison
    words.push_back(i_type(0x09u, 0u, 26u, 2u));           // 0x1b0 poison
    words.push_back(i_type(0x09u, 0u, 27u, 2u));           // 0x1b4 poison
    words.push_back(i_type(0x09u, 0u, 28u, 2u));           // 0x1b8 poison
    words.push_back(i_type(0x09u, 0u, 30u, 2u));           // 0x1bc poison/guard
    words.push_back(i_type(0x09u, 0u, 7u, 0x0066u));       // 0x1c0 indirect target
    words.push_back(i_type(0x05u, 0u, 0u, 0u));            // 0x1c4 unsupported BNE
    words.push_back(0u);                                    // 0x1c8 mapped BNE delay
    expect(words.size() == 113u, "synthetic JR/JALR fixture count mismatch");
'''),
    ('''    expect(state.gpr[24].low64 == 0x55u,
           "callee prefix mismatch");
''', '''    expect(state.gpr[24].low64 == 0x55u,
           "direct callee entry mismatch");
    expect(state.gpr[29].low64 == 0x77u,
           "JR delay must execute");
    expect(state.gpr[5].low64 == 0x00100198u,
           "JALR rd==rs link mismatch");
    expect(state.gpr[6].low64 == 0x00100198u,
           "JALR delay must observe new link");
    expect(state.gpr[7].low64 == 0x66u,
           "indirect target entry mismatch");
'''),
    ('''    std::cout << "SYNTHETIC_STARTUP_J_JAL_VALIDATED sq=0x00100160 target=0x004e2680 "
                 "stop=0x001001a4 blocks=5 instructions=87\\n";
''', '''    std::cout << "SYNTHETIC_STARTUP_JR_JALR_VALIDATED sq=0x00100160 target=0x004e2680 "
                 "stop=0x001001c4 blocks=7 instructions=94\\n";
'''),
]

for old, new in replacements:
    if text.count(old) != 1:
        raise SystemExit(f"expected exactly one startup fixture match: {old[:100]!r}")
    text = text.replace(old, new, 1)

path.write_text(text, encoding="utf-8")
print("Task 5 GREEN fixture applied")
