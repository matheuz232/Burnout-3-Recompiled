from pathlib import Path
import subprocess

path = Path('CMakeLists.txt')
text = path.read_text(encoding='utf-8')
needle = '''    add_executable(r5900_block_dispatcher_startup_windows_tests\n      tests/r5900_block_dispatcher_startup_windows_tests.cpp\n    )\n    target_link_libraries(r5900_block_dispatcher_startup_windows_tests PRIVATE\n      b3r_recompiler_dispatcher_x64\n    )\n    add_test(NAME r5900_block_dispatcher_startup_windows_tests COMMAND r5900_block_dispatcher_startup_windows_tests)\n'''
insert = needle + '''\n    add_executable(r5900_block_dispatcher_bss_clear_windows_tests\n      tests/r5900_block_dispatcher_bss_clear_windows_tests.cpp\n    )\n    target_link_libraries(r5900_block_dispatcher_bss_clear_windows_tests PRIVATE\n      b3r_recompiler_dispatcher_x64\n    )\n    add_test(NAME r5900_block_dispatcher_bss_clear_windows_tests\n      COMMAND r5900_block_dispatcher_bss_clear_windows_tests)\n'''
if 'r5900_block_dispatcher_bss_clear_windows_tests' not in text:
    if needle not in text:
        raise SystemExit('CMake startup-test marker not found')
    path.write_text(text.replace(needle, insert, 1), encoding='utf-8')

subprocess.run(['git', 'config', 'user.name', 'github-actions[bot]'], check=True)
subprocess.run(['git', 'config', 'user.email', '41898282+github-actions[bot]@users.noreply.github.com'], check=True)
subprocess.run(['git', 'add', 'CMakeLists.txt'], check=True)
if subprocess.run(['git', 'diff', '--cached', '--quiet']).returncode != 0:
    subprocess.run(['git', 'commit', '-m', 'test: register BSS clear fast-cache RED'], check=True)
    subprocess.run(['git', 'push'], check=True)
