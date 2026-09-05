from pathlib import Path

path = Path("CMakeLists.txt")
text = path.read_text(encoding="utf-8")
old = """    add_test(NAME r5900_block_dispatcher_direct_transfer_windows_tests\n      COMMAND r5900_block_dispatcher_direct_transfer_windows_tests)\n\n    add_executable(r5900_block_dispatcher_store128_windows_tests"""
new = """    add_test(NAME r5900_block_dispatcher_direct_transfer_windows_tests
      COMMAND r5900_block_dispatcher_direct_transfer_windows_tests)

    add_executable(r5900_block_dispatcher_indirect_transfer_windows_tests
      tests/r5900_block_dispatcher_indirect_transfer_windows_tests.cpp
    )
    target_link_libraries(r5900_block_dispatcher_indirect_transfer_windows_tests PRIVATE
      b3r_recompiler_dispatcher_x64
    )
    add_test(NAME r5900_block_dispatcher_indirect_transfer_windows_tests
      COMMAND r5900_block_dispatcher_indirect_transfer_windows_tests)

    add_executable(r5900_block_dispatcher_store128_windows_tests"""
if text.count(old) != 1:
    raise SystemExit("dispatcher direct-transfer CMake anchor missing or duplicated")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("Task 4 RED target registered")
