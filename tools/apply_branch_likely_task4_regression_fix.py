from pathlib import Path

path = Path("tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp")
text = path.read_text(encoding="utf-8")

replacements = {
    "const auto bnel_boundary = i_type(0x15u, 0u, 0u, 0u);":
        "const auto bgtz_boundary = i_type(0x07u, 0u, 0u, 0u);",
    "bnel_boundary, 0u,": "bgtz_boundary, 0u,",
    "bnel_boundary, 0u,\n        }, base);": "bgtz_boundary, 0u,\n        }, base);",
    "bnel_boundary, 0u,\n        }, base, data_base);": "bgtz_boundary, 0u,\n        }, base, data_base);",
    "J fixture must reach unsupported target BNEL":
        "J fixture must reach unsupported target BGTZ",
    "JAL fixture must reach unsupported target BNEL":
        "JAL fixture must reach unsupported target BGTZ",
}

for old, new in replacements.items():
    if old not in text:
        raise SystemExit(f"missing expected fixture text: {old!r}")
    text = text.replace(old, new)

if "bnel_boundary" in text:
    raise SystemExit("legacy BNEL boundary identifier remains after fixture update")

path.write_text(text, encoding="utf-8")
