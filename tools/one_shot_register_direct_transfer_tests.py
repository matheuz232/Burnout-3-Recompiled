from pathlib import Path

path = Path('CMakeLists.txt')
text = path.read_text(encoding='utf-8')

portable_anchor = '''  add_executable(r5900_ir_block_executor_tests
    tests/r5900_ir_block_executor_tests.cpp
  )
  target_link_libraries(r5900_ir_block_executor_tests PRIVATE b3r_recompiler)
  add_test(NAME r5900_ir_block_executor_tests COMMAND r5900_ir_block_executor_tests)
'''
portable_insert = portable_anchor + '''
  add_executable(r5900_ir_direct_transfer_validation_tests
    tests/r5900_ir_direct_transfer_validation_tests.cpp
  )
  target_link_libraries(r5900_ir_direct_transfer_validation_tests PRIVATE b3r_recompiler)
  add_test(NAME r5900_ir_direct_transfer_validation_tests
    COMMAND r5900_ir_direct_transfer_validation_tests)

  add_executable(r5900_ir_direct_transfer_executor_tests
    tests/r5900_ir_direct_transfer_executor_tests.cpp
  )
  target_link_libraries(r5900_ir_direct_transfer_executor_tests PRIVATE b3r_recompiler)
  add_test(NAME r5900_ir_direct_transfer_executor_tests
    COMMAND r5900_ir_direct_transfer_executor_tests)
'''
if text.count(portable_anchor) != 1:
    raise SystemExit('portable direct-transfer registration anchor mismatch')
text = text.replace(portable_anchor, portable_insert, 1)

x64_anchor = '''    add_executable(r5900_x64_store128_windows_tests
      tests/r5900_x64_store128_windows_tests.cpp
    )
    target_link_libraries(r5900_x64_store128_windows_tests PRIVATE b3r_recompiler_x64)
    add_test(NAME r5900_x64_store128_windows_tests COMMAND r5900_x64_store128_windows_tests)
'''
x64_insert = x64_anchor + '''
    add_executable(r5900_x64_direct_transfer_windows_tests
      tests/r5900_x64_direct_transfer_windows_tests.cpp
    )
    target_link_libraries(r5900_x64_direct_transfer_windows_tests PRIVATE
      b3r_recompiler_x64
    )
    add_test(NAME r5900_x64_direct_transfer_windows_tests
      COMMAND r5900_x64_direct_transfer_windows_tests)
'''
if text.count(x64_anchor) != 1:
    raise SystemExit('x64 direct-transfer registration anchor mismatch')
text = text.replace(x64_anchor, x64_insert, 1)

dispatcher_anchor = '''    add_executable(r5900_block_dispatcher_windows_tests
      tests/r5900_block_dispatcher_windows_tests.cpp
    )
    target_link_libraries(r5900_block_dispatcher_windows_tests PRIVATE
      b3r_recompiler_dispatcher_x64
    )
    add_test(NAME r5900_block_dispatcher_windows_tests COMMAND r5900_block_dispatcher_windows_tests)
'''
dispatcher_insert = dispatcher_anchor + '''
    add_executable(r5900_block_dispatcher_direct_transfer_windows_tests
      tests/r5900_block_dispatcher_direct_transfer_windows_tests.cpp
    )
    target_link_libraries(r5900_block_dispatcher_direct_transfer_windows_tests PRIVATE
      b3r_recompiler_dispatcher_x64
    )
    add_test(NAME r5900_block_dispatcher_direct_transfer_windows_tests
      COMMAND r5900_block_dispatcher_direct_transfer_windows_tests)
'''
if text.count(dispatcher_anchor) != 1:
    raise SystemExit('dispatcher direct-transfer registration anchor mismatch')
text = text.replace(dispatcher_anchor, dispatcher_insert, 1)

path.write_text(text, encoding='utf-8')
