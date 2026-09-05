from pathlib import Path

path = Path("tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp")
text = path.read_text(encoding="utf-8")

old_definition = "const auto bnel_boundary = i_type(0x15u, 0u, 0u, 0u);"
new_definition = "const auto bgtz_boundary = i_type(0x07u, 0u, 0u, 0u);"
if old_definition not in text:
    raise SystemExit("legacy BNEL boundary definition not found")
text = text.replace(old_definition, new_definition, 1)

uses = text.count("bnel_boundary")
if uses == 0:
    raise SystemExit("legacy BNEL boundary uses not found")
text = text.replace("bnel_boundary", "bgtz_boundary")

for old, new in (
    ("J fixture must reach unsupported target BNEL",
     "J fixture must reach unsupported target BGTZ"),
    ("JAL fixture must reach unsupported target BNEL",
     "JAL fixture must reach unsupported target BGTZ"),
):
    if old not in text:
        raise SystemExit(f"missing expected fixture diagnostic: {old!r}")
    text = text.replace(old, new, 1)

if "bnel_boundary" in text:
    raise SystemExit("legacy BNEL boundary identifier remains after fixture update")

path.write_text(text, encoding="utf-8")
