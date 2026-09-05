from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly one match, found {count}')
    return text.replace(old, new, 1)

# README
path = Path('README.md')
text = path.read_text(encoding='utf-8')
replacements = [
    (
        '`Burnout 3 Recompiled - Test Build 0.1` bootstrap, static-analysis, and native R5900 startup-execution infrastructure through the startup `SQ` guest-memory write, direct `J`/`JAL`, and indirect `JR`/`JALR` execution with architectural delay slots.',
        '`Burnout 3 Recompiled - Test Build 0.1` bootstrap, static-analysis, and native R5900 startup-execution infrastructure through the startup `SQ` guest-memory write, ordinary `BEQ`/`BNE`, direct `J`/`JAL`, and indirect `JR`/`JALR` execution with architectural delay slots.',
        'README milestone'),
    (
        'including `SYNC`, `MTSAH`, `MTHI1`, `MTLO1`, `PADDUW`, `MTC1`, `CTC1`, `ADDA.S`, ordinary `BEQ`, scalar `AND`, and `SQ`;',
        'including `SYNC`, `MTSAH`, `MTHI1`, `MTLO1`, `PADDUW`, `MTC1`, `CTC1`, `ADDA.S`, ordinary `BEQ`/`BNE`, scalar `AND`, and `SQ`;',
        'README decoder'),
    (
        '- native x86-64 control-transfer emission for `BEQ`, direct `J`/`JAL`, and indirect `JR`/`JALR`;',
        '- native x86-64 control-transfer emission for ordinary `BEQ`/`BNE`, direct `J`/`JAL`, and indirect `JR`/`JALR`; `BNE` reuses the equality terminator with swapped runtime destinations;',
        'README backend bullet'),
    (
        'supports ordinary `BEQ`, direct `J`/`JAL`, and indirect `JR`/`JALR`, applies a block budget, caches native blocks by guest PC, rejects stale cache entries after guest-code changes, and bridges mutable guest memory for body `SQ` operations;',
        'supports ordinary `BEQ`/`BNE`, direct `J`/`JAL`, and indirect `JR`/`JALR`, applies a block budget, caches native blocks by guest PC, rejects stale cache entries after guest-code changes, and bridges mutable guest memory for body `SQ` operations;',
        'README dispatcher bullet'),
    (
        'cache fingerprints that cover straight-line body words plus supported `BEQ`/`J`/`JAL`/`JR`/`JALR` terminator and delay-slot words;',
        'cache fingerprints that cover straight-line body words plus supported `BEQ`/`BNE`/`J`/`JAL`/`JR`/`JALR` terminator and delay-slot words;',
        'README cache bullet'),
    (
        'differential Windows tests comparing reference and native x86-64 execution for integer/MMI/COP1 state, `BEQ`, direct `J`/`JAL`, indirect `JR`/`JALR`, target-snapshot/link-before-delay ordering, `rd == rs`, `rd == 0`, link high64 preservation, source-mutating delay slots, and `Store128` success/failure semantics;',
        'differential Windows tests comparing reference and native x86-64 execution for integer/MMI/COP1 state, `BEQ`, direct `J`/`JAL`, indirect `JR`/`JALR`, target-snapshot/link-before-delay ordering, `rd == rs`, `rd == 0`, link high64 preservation, source-mutating delay slots, and `Store128` success/failure semantics; dispatcher tests additionally cover ordinary `BNE` taken/not-taken behavior, cache reuse across runtime predicate changes, delay-word invalidation, and `SQ` delay rejection;',
        'README tests bullet'),
    (
        'a synthetic startup-shaped dispatcher test that completes **7 native guest blocks / 94 guest instructions**, covers one taken and one not-taken `BEQ`, executes `SQ` at `0x00100160`, direct `J`/`JAL`, `JR` with its delay slot, aliasing `JALR r5,r5` with link-visible delay semantics, reaches the indirect target at `0x001001c0`, and stops conservatively at unsupported `BNE` `0x001001c4`;',
        'a synthetic startup-shaped dispatcher test that completes **7 native guest blocks / 96 guest instructions**, covers one taken and one not-taken `BEQ`, executes `SQ` at `0x00100160`, direct `J`/`JAL`, `JR` with its delay slot, aliasing `JALR r5,r5` with link-visible delay semantics, reaches the indirect target at `0x001001c0`, executes not-taken `BNE r0,r0` at `0x001001c4` plus its NOP delay, and then stops at deterministic analysis failure `0x001001cc` because the synthetic executable fixture ends;',
        'README startup bullet'),
    (
        '`R5900BlockDispatcher` supports ordinary `BEQ`, direct `J`/`JAL`, and indirect `JR`/`JALR` as native block terminators plus `SQ` in straight-line block bodies.',
        '`R5900BlockDispatcher` supports ordinary `BEQ`/`BNE`, direct `J`/`JAL`, and indirect `JR`/`JALR` as native block terminators plus `SQ` in straight-line block bodies.',
        'README dispatcher narrative'),
    (
        '`BEQ` snapshots its predicate before the slot; `J` returns its fixed direct target;',
        '`BEQ` and `BNE` evaluate their low64 GPR predicates before the slot; `BNE` reuses `BranchEqual64` with the equality and inequality destinations swapped; `J` returns its fixed direct target;',
        'README branch narrative'),
    (
        '`SQ` in dispatcher-managed `BEQ`, `J`/`JAL`, or `JR`/`JALR` delay slots remains deliberately outside v0. `BNE`, branch-likely forms, guest loads, other guest stores, and other unsupported control flow still stop conservatively.',
        '`SQ` in dispatcher-managed `BEQ`/`BNE`, `J`/`JAL`, or `JR`/`JALR` delay slots remains deliberately outside v0. Branch-likely forms including `BNEL`, guest loads, other guest stores, and other unsupported control flow still stop conservatively.',
        'README limitations'),
    (
        'Broader guest-memory loads/stores, additional control flow such as `BNE`/branch-likely forms, BSS-clearing loops beyond the current synthetic startup boundary, syscall/HLE integration, graphics, audio, input, menus, and gameplay remain unimplemented. The current `JR`/`JALR` milestone is **CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION**;',
        'Broader guest-memory loads/stores, additional control flow such as branch-likely forms, BSS-clearing loops beyond the current synthetic startup boundary, syscall/HLE integration, graphics, audio, input, menus, and gameplay remain unimplemented. The current ordinary `BNE` milestone is **CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION**;',
        'README status paragraph'),
]
for old, new, label in replacements:
    text = replace_once(text, old, new, label)
path.write_text(text, encoding='utf-8')

# PROGRESS
path = Path('docs/PROGRESS.md')
text = path.read_text(encoding='utf-8')
replacements = [
    (
        '| R5900 BEQ + delay slot v0 | CI_VALIDATED | Ordinary BEQ is a native block terminator. Predicate uses GPR low64 values before the slot; taken/not-taken targets are returned by generated code. Direct J/JAL and indirect JR/JALR are separately supported; branch-likely and BNE execution remain out of scope |',
        '| R5900 BEQ + delay slot v0 | CI_VALIDATED | Ordinary BEQ is a native block terminator. Predicate uses GPR low64 values before the slot; taken/not-taken targets are returned by generated code. Direct J/JAL and indirect JR/JALR are separately supported; branch-likely execution remains out of scope |\n| R5900 BNE + delay slot v0 | CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION | Ordinary BNE is dispatched through the existing `BranchEqual64` terminator with equality/inequality destinations swapped. Taken/not-taken paths execute one architectural delay slot; cache reuse across runtime predicate changes, delay-word invalidation, and BNE+SQ delay rejection are covered. Clean code SHA `e0af1a1b4d2147c84a469b003b884cb2e9e023dc` passed Windows CI run `33988016891`, job `101365058018`, with 47/47 tests plus pacing/package validation; external legal-ELF execution remains pending |',
        'PROGRESS BEQ/BNE rows'),
    (
        '| R5900 SQ + guest-memory writes v0 | CI_VALIDATED | Straight-line `SQ` bodies execute through `Store128`: effective address is low32(base) + signed imm16 with 32-bit wrap, silently aligned down to 16 bytes, then all 128 source bits are written. Runtime memory failure is deterministic/non-partial; `SQ` in dispatcher-managed BEQ/J/JAL/JR/JALR delay slots is explicitly outside v0 |',
        '| R5900 SQ + guest-memory writes v0 | CI_VALIDATED | Straight-line `SQ` bodies execute through `Store128`: effective address is low32(base) + signed imm16 with 32-bit wrap, silently aligned down to 16 bytes, then all 128 source bits are written. Runtime memory failure is deterministic/non-partial; `SQ` in dispatcher-managed BEQ/BNE/J/JAL/JR/JALR delay slots is explicitly outside v0 |',
        'PROGRESS SQ row'),
    (
        '| R5900 native block dispatcher v0 | CI_VALIDATED | Dispatcher consumes native `next_pc`, supports ordinary BEQ plus direct J/JAL and indirect JR/JALR with one delay slot and body `SQ`, fingerprints exact body/terminator/delay guest words, excludes runtime indirect target values from the cache key, and recompiles stale code. Synthetic startup executes 7 blocks / 94 instructions and stops at unsupported BNE `0x001001c4` |',
        '| R5900 native block dispatcher v0 | CI_VALIDATED | Dispatcher consumes native `next_pc`, supports ordinary BEQ/BNE plus direct J/JAL and indirect JR/JALR with one delay slot and body `SQ`, fingerprints exact body/terminator/delay guest words, excludes runtime predicate/indirect target values from the cache key, and recompiles stale code. Synthetic startup executes 7 blocks / 96 instructions, executes BNE `0x001001c4` plus its delay, and reaches analysis failure at `0x001001cc` |',
        'PROGRESS dispatcher row'),
    (
        '| R5900 startup execution v0 | CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION | Synthetic native path completes 7 blocks / 94 guest instructions: two BEQs + delay slots, `SQ 0x00100160`, direct J/JAL, JR return with executed delay, aliasing `JALR r5,r5` with link-visible delay, indirect callee entry, then conservative stop at unsupported BNE `0x001001c4`. Clean SHA `2f7a68fcd493f9bb8dcca84a9c5633c1d5f3cab5` passed Windows CI run `33983976120`, job `101354052009`, with 47/47 tests plus pacing/package validation. The actual legal ELF has not been run through this expanded Windows native path, so `EXTERNALLY_VALIDATED` is intentionally not claimed |',
        '| R5900 startup execution v0 | CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION | Synthetic native path completes 7 blocks / 96 guest instructions: two BEQs + delay slots, `SQ 0x00100160`, direct J/JAL, JR return with executed delay, aliasing `JALR r5,r5` with link-visible delay, indirect callee entry, then not-taken `BNE r0,r0` at `0x001001c4` with its NOP delay before deterministic `AnalysisFailure` at `0x001001cc`, the end of the synthetic executable fixture. Clean code SHA `e0af1a1b4d2147c84a469b003b884cb2e9e023dc` passed Windows CI run `33988016891`, job `101365058018`, with 47/47 tests plus pacing/package validation. The actual legal ELF has not been run through this expanded Windows native path, so `EXTERNALLY_VALIDATED` is intentionally not claimed |',
        'PROGRESS startup row'),
    (
        '| Static/binary recompiler | IN_PROGRESS | Decoder -> IR -> reference -> Windows x64 -> dispatcher now executes the modeled startup path through two BEQs, `SQ`, direct J/JAL, and indirect JR/JALR. Broader guest loads/stores, additional branch/control-flow forms beginning with the current BNE boundary, syscall/HLE and real external ELF native execution are the next gates |',
        '| Static/binary recompiler | IN_PROGRESS | Decoder -> IR -> reference -> Windows x64 -> dispatcher now executes the modeled startup path through ordinary BEQ/BNE, `SQ`, direct J/JAL, and indirect JR/JALR. Broader guest loads/stores, branch-likely/additional control-flow forms, syscall/HLE and real external ELF native execution are the next gates |',
        'PROGRESS recompiler row'),
    (
        '## Windows CI evidence\n\n`JR + JALR v0` used explicit TDD gates through IR validation, reference execution, native x64 differentials, dispatcher/cache behavior and startup E2E.',
        '## Windows CI evidence\n\n`BNE + delay slot v0` reused the existing `BranchEqual64` native path with swapped equality/inequality destinations. RED run `33987192233` built successfully and failed only the new BNE execution expectation; subsequent fixture debugging established the correct synthetic endpoint as 7 blocks / 96 instructions at `AnalysisFailure 0x001001cc`. Coverage RED run `33987884461` passed 46/47 and failed only the intentional `BEQ/BNE` SQ-delay diagnostic assertion. Clean GREEN code SHA `e0af1a1b4d2147c84a469b003b884cb2e9e023dc` passed run `33988016891`, job `101365058018`, with **47/47 tests**. Pacing reported 240 samples at 8.333 ms mean (8.308 ms min, 8.359 ms max, 0.002 ms stddev) with zero samples above 9/10/12 ms; the one-second probe completed 120/120 frames at 8.333 ms mean. Analyzer and pacing-probe staging/validation passed. This is CI evidence only; no legal external ELF was executed.\n\n`JR + JALR v0` used explicit TDD gates through IR validation, reference execution, native x64 differentials, dispatcher/cache behavior and startup E2E.',
        'PROGRESS CI evidence intro'),
]
for old, new, label in replacements:
    text = replace_once(text, old, new, label)
path.write_text(text, encoding='utf-8')
