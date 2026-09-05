from pathlib import Path
import subprocess

readme_path = Path('README.md')
readme = readme_path.read_text(encoding='utf-8')

replacements = [
    (
        '`Burnout 3 Recompiled - Test Build 0.1` bootstrap, static-analysis, and native R5900 startup-execution infrastructure through the startup `SQ` guest-memory write, ordinary `BEQ`/`BNE`, branch-likely `BEQL`/`BNEL`, direct `J`/`JAL`, and indirect `JR`/`JALR` execution with architectural delay slots.',
        '`Burnout 3 Recompiled - Test Build 0.1` bootstrap, static-analysis, and native R5900 startup-execution infrastructure through the real startup BSS-clear loop boundary, with `SQ` guest-memory writes, ordinary `BEQ`/`BNE`, branch-likely `BEQL`/`BNEL`, direct `J`/`JAL`, indirect `JR`/`JALR`, and transfer-block fast cache replay.'
    ),
    (
        '- a Windows R5900 native block dispatcher that analyzes guest blocks, lowers/compiles them on demand, executes them through the x86-64 backend, consumes the JIT-returned `next_pc`, supports ordinary `BEQ`/`BNE`, branch-likely `BEQL`/`BNEL`, direct `J`/`JAL`, and indirect `JR`/`JALR`, applies a block budget, caches native blocks by guest PC, rejects stale cache entries after guest-code changes, and bridges mutable guest memory for body `SQ` operations;',
        '- a Windows R5900 native block dispatcher that analyzes guest blocks, lowers/compiles them on demand, executes them through the x86-64 backend, consumes the JIT-returned `next_pc`, supports ordinary `BEQ`/`BNE`, branch-likely `BEQL`/`BNEL`, direct `J`/`JAL`, and indirect `JR`/`JALR`, applies a block budget, caches native blocks by guest PC, rejects stale cache entries after guest-code changes, bridges mutable guest memory for body `SQ` operations, and fast-replays unchanged cached transfer blocks after direct guest-word verification without repeating analysis/lowering;'
    ),
    (
        '- an optional external-ELF validation mode for the startup dispatcher test, allowing a legally supplied `SLUS_210.50` to be tested locally without committing or uploading game data;',
        '- an optional external-ELF validation mode for the startup dispatcher test, allowing a legally supplied `SLUS_210.50` to execute the full real BSS-clear loop locally and prove the exact `SetupThread` syscall boundary without committing or uploading game data;'
    ),
    (
        'The game still does **not** boot. Broader guest-memory loads/stores, additional control flow including `BLEZL`/`BGTZL` and REGIMM likely/link-likely variants, BSS-clearing loops beyond the current synthetic startup boundary, syscall/HLE integration, graphics, audio, input, menus, and gameplay remain unimplemented. The `BEQL + BNEL branch-likely v0` milestone is **CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION** with 51/51 Windows tests; the legally supplied external ELF has not been run through this expanded Windows native path in this environment.',
        'The game still does **not** boot. The real BSS-clear loop is now represented by a dedicated native-dispatcher fixture and transfer-block fast cache replay, while the external harness is prepared to validate the same loop through `SetupThread` at `0x001001c8`. Broader guest-memory loads/stores, additional control flow including `BLEZL`/`BGTZL` and REGIMM likely/link-likely variants, syscall/HLE integration, graphics, audio, input, menus, and gameplay remain unimplemented. The `R5900 BSS clear loop v0` milestone is **CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION** with 52/52 Windows tests; the legally supplied external ELF has not been run through this expanded Windows native path in this environment.'
    ),
    (
        '## Validate the startup prefix against an external ELF',
        '## Validate startup and the BSS clear loop against an external ELF'
    ),
    (
        'REAL_ELF_SQ_VALIDATED sq=0x00100160 target=0x004e2680 stop=0x........ blocks=N instructions=N',
        'REAL_ELF_BSS_CLEAR_VALIDATED begin=0x004e2680 end=0x01ecea00 stop=0x001001c8 iterations=1698872 blocks=3397748 instructions=13591071 fast_cache_hits=3397742'
    ),
    (
        'The harness requires at least 82 executed guest instructions, proves that execution advances beyond `0x00100160`, and verifies that the full 16-byte startup target at `0x004e2680` is zero afterward. It deliberately does not hard-code a later real-file stop boundary until native/static evidence identifies it. Until that command has been executed successfully against the supplied ELF on Windows x64, the milestone remains **CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION**, not externally native-validated.',
        'The harness now requires the exact real startup path through the BSS clear: 1,698,872 aligned 16-byte `SQ` iterations over `0x004e2680..0x01ecea00`, 3,397,748 native blocks, 13,591,071 selected guest words, and 3,397,742 fast-cache replays before the dispatcher stops at the `SetupThread` syscall (`v1=0x3c`) at `0x001001c8`. It also verifies the startup syscall argument registers and zero recompilations. Until that command has been executed successfully against the supplied ELF on Windows x64, the milestone remains **CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION**, not externally native-validated.'
    ),
]
for old, new in replacements:
    if old not in readme:
        raise SystemExit(f'README marker not found: {old[:80]}')
    readme = readme.replace(old, new, 1)

old_inspection = 'A legally supplied Burnout 3 ELF was inspected out-of-repository. Its entry point is `0x00100008`. Static inspection identifies the original 74-instruction startup body before the first `BEQ` at `0x00100130`; the first branch is taken to `0x0010014C` with a NOP delay slot. The continuation contains `LUI`, `ORI`, scalar `AND`, a second `BEQ` at `0x00100158`, and its NOP delay slot; with the startup state produced by the preceding body, that second branch is not taken and fallthrough reaches `SQ` at `0x00100160`, whose startup target is `0x004e2680`. This real-file inspection is **not** claimed as native external execution: the Windows external-ELF dispatcher path is compiled and CI-tested without game data, but the supplied ELF has not yet been run through that Windows executable in this environment.'
new_inspection = 'A legally supplied Burnout 3 ELF was inspected out-of-repository. Its entry point is `0x00100008`. Static inspection identifies the original 74-instruction startup body before the first `BEQ` at `0x00100130`; the first branch is taken to `0x0010014C` with a NOP delay slot. The continuation reaches `SQ` at `0x00100160` and enters the real BSS-clear loop, advancing `r2` by 16 bytes from `0x004e2680` to `0x01ecea00`. That is 1,698,872 `SQ` iterations. After the loop, startup prepares the EE kernel call arguments and reaches `SYSCALL` at `0x001001c8` with `v1=0x3c` (`SetupThread`); the next startup syscall observed statically is `v1=0x3d` (`SetupHeap`) at `0x001001e4`. This real-file inspection is **not** claimed as native external execution: the Windows external-ELF dispatcher path is compiled and CI-tested without game data, but the supplied ELF has not yet been run through that Windows executable in this environment.'
if old_inspection not in readme:
    raise SystemExit('README real-ELF inspection paragraph marker not found')
readme = readme.replace(old_inspection, new_inspection, 1)
readme_path.write_text(readme, encoding='utf-8')

progress_path = Path('docs/PROGRESS.md')
progress = progress_path.read_text(encoding='utf-8')
sq_row = '| R5900 SQ + guest-memory writes v0 | CI_VALIDATED | Straight-line `SQ` bodies execute through `Store128`: effective address is low32(base) + signed imm16 with 32-bit wrap, silently aligned down to 16 bytes, then all 128 source bits are written. Runtime memory failure is deterministic/non-partial; `SQ` in dispatcher-managed BEQ/BNE/BEQL/BNEL/J/JAL/JR/JALR delay slots is explicitly outside v0 |'
bss_row = '| R5900 BSS clear loop v0 | CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION | Transfer-block cache entries can now fast-replay after direct byte-exact guest-word verification, bypassing repeated analysis/lowering while preserving stale-code fallback and recompilation. A dedicated 4-quadword synthetic BSS loop proves 2 compiled blocks, 7/7 fast replays, exact zero range/preserved sentinels, 9 blocks / 26 selected words, and Trap at the syscall boundary. Clean code SHA `da82c5afd42a4d66b43e1eed6bfbec37ddb0fde7` passed Windows CI run `33995680666`, job `101385700565`, with 52/52 tests plus pacing/package validation. The external harness is prepared for the real 1,698,872-iteration BSS loop through `SetupThread @ 0x001001c8`, but that legal-ELF Windows run is NOT RUN. |'
if bss_row not in progress:
    if sq_row not in progress:
        raise SystemExit('PROGRESS SQ row marker not found')
    progress = progress.replace(sq_row, sq_row + '\n' + bss_row, 1)

old_dispatcher = '| R5900 native block dispatcher v0 | CI_VALIDATED | Dispatcher consumes native `next_pc`, supports ordinary BEQ/BNE, likely BEQL/BNEL, direct J/JAL and indirect JR/JALR with one selected delay word and body `SQ`, fingerprints exact body/terminator/delay guest words, excludes runtime predicate/indirect target values from the cache key, and recompiles stale code. Not-taken likely paths annul delay effects while `instructions_executed` remains selected-word accounting. Synthetic startup remains 7 blocks / 96 instructions at analysis failure `0x001001cc` |'
new_dispatcher = '| R5900 native block dispatcher v0 | CI_VALIDATED | Dispatcher consumes native `next_pc`, supports ordinary BEQ/BNE, likely BEQL/BNEL, direct J/JAL and indirect JR/JALR with one selected delay word and body `SQ`, fingerprints exact body/terminator/delay guest words, excludes runtime predicate/indirect target values from the cache key, and recompiles stale code. Stable cached transfer blocks now use direct guest-word verification plus fast native replay before analysis; any mismatch falls back to the existing analysis/fingerprint/recompile path. Not-taken likely paths annul delay effects while `instructions_executed` remains selected-word accounting. Synthetic startup remains 7 blocks / 96 instructions at analysis failure `0x001001cc` |'
if old_dispatcher not in progress:
    raise SystemExit('PROGRESS dispatcher row marker not found')
progress = progress.replace(old_dispatcher, new_dispatcher, 1)

old_startup = '| R5900 startup execution v0 | CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION | Synthetic native path remains 7 blocks / 96 selected guest instructions through two BEQs, `SQ 0x00100160`, direct J/JAL, JR/JALR and BNE before deterministic `AnalysisFailure` at `0x001001cc`. Branch-likely expansion passed Windows CI run `33993550729`, job `101379915351`, SHA `6d486b9a1276542ec1f1117c76fc431fb2495a54`, with 51/51 tests and all package gates. The actual legal ELF has not been run through this expanded Windows native path, so `EXTERNALLY_VALIDATED` is intentionally not claimed |'
new_startup = '| R5900 startup execution v0 | CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION | Synthetic control-transfer path remains 7 blocks / 96 selected guest instructions at deterministic `AnalysisFailure 0x001001cc`; a separate BSS-loop fixture validates repeated `BEQ`/`SQ`/`ADDIU`/`J` execution and fast-cache replay through a syscall boundary. The external harness now requires the real path to `SetupThread @ 0x001001c8` after exactly 1,698,872 BSS iterations, 3,397,748 blocks and 13,591,071 selected guest words. The actual legal ELF has not been run through this expanded Windows native path, so `EXTERNALLY_VALIDATED` is intentionally not claimed |'
if old_startup not in progress:
    raise SystemExit('PROGRESS startup row marker not found')
progress = progress.replace(old_startup, new_startup, 1)

old_static = '| Static/binary recompiler | IN_PROGRESS | Decoder -> IR -> reference -> Windows x64 -> dispatcher now executes the modeled startup path through ordinary BEQ/BNE, branch-likely BEQL/BNEL, `SQ`, direct J/JAL, and indirect JR/JALR. Broader guest loads/stores, BLEZL/BGTZL and REGIMM likely/link-likely control flow, syscall/HLE and real external ELF native execution are the next gates |'
new_static = '| Static/binary recompiler | IN_PROGRESS | Decoder -> IR -> reference -> Windows x64 -> dispatcher executes the modeled startup control flow and now has a fast replay path suitable for the real BSS-clear loop. The next real boundary is EE kernel `SetupThread` syscall `0x3c` at `0x001001c8`, followed by `SetupHeap` `0x3d`; syscall/HLE, broader guest loads/stores, remaining control flow, and real external ELF native execution are the next gates |'
if old_static not in progress:
    raise SystemExit('PROGRESS static recompiler row marker not found')
progress = progress.replace(old_static, new_static, 1)

evidence_heading = '## Windows CI evidence\n\n'
evidence = '`R5900 BSS clear loop v0` added transfer-block fast cache replay with byte-exact guest-word verification before native reuse. RED run `33995448140`, job `101385087338`, failed exactly because the new `fast_cache_hits` telemetry did not yet exist. Clean GREEN SHA `da82c5afd42a4d66b43e1eed6bfbec37ddb0fde7` passed Windows CI run `33995680666`, job `101385700565`, with **52/52 tests**; the dedicated BSS fixture completed 4 aligned `SQ` iterations through 9 blocks / 26 selected words, compiled two native blocks and recorded seven fast replays. Pacing telemetry reported 240 samples at 8.333 ms mean with zero samples above 9/10/12 ms; the one-second probe completed 120/120 frames. Analyzer and pacing-probe package gates passed. The external harness is compiled to require the real `0x004e2680..0x01ecea00` clear and `SetupThread @ 0x001001c8`, but the legal ELF has not been executed on the Windows native path, so this is not `EXTERNALLY_VALIDATED`.\n\n'
if evidence not in progress:
    if evidence_heading not in progress:
        raise SystemExit('PROGRESS evidence heading not found')
    progress = progress.replace(evidence_heading, evidence_heading + evidence, 1)

progress_path.write_text(progress, encoding='utf-8')

subprocess.run(['git', 'config', 'user.name', 'github-actions[bot]'], check=True)
subprocess.run(['git', 'config', 'user.email', '41898282+github-actions[bot]@users.noreply.github.com'], check=True)
subprocess.run(['git', 'add', 'README.md', 'docs/PROGRESS.md'], check=True)
if subprocess.run(['git', 'diff', '--cached', '--quiet']).returncode != 0:
    subprocess.run(['git', 'commit', '-m', 'docs: record R5900 BSS clear loop validation'], check=True)
    subprocess.run(['git', 'push'], check=True)
