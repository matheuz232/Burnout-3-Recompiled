from pathlib import Path

# README
path = Path('README.md')
text = path.read_text(encoding='utf-8')
replacements = [
    (
        '`Burnout 3 Recompiled - Test Build 0.1` bootstrap, static-analysis, and native R5900 startup-execution infrastructure through the startup `SQ` guest-memory write at `0x00100160`.',
        '`Burnout 3 Recompiled - Test Build 0.1` bootstrap, static-analysis, and native R5900 startup-execution infrastructure through the startup `SQ` guest-memory write plus direct `J`/`JAL` execution with architectural delay slots.'
    ),
    (
        '- block-level R5900 IR with a `BranchEqual64` terminator, explicit taken/fallthrough PCs, and one architectural delay slot;',
        '- block-level R5900 IR with typed `BranchEqual64`, `DirectJump`, and `DirectCall` terminators plus one explicit architectural delay slot;'
    ),
    (
        '- native x86-64 `BEQ` emission that snapshots the low-64-bit equality predicate before the delay slot, executes exactly one delay-slot path, and returns the selected guest `next_pc` in EAX;',
        '- native x86-64 control-transfer emission for `BEQ`, direct `J`, and direct `JAL`; `JAL` writes zero-extended `PC+8` to `GPR31.low64` before the delay slot while preserving `GPR31.high64`, and generated blocks return the selected guest `next_pc` in EAX;'
    ),
    (
        '- a Windows R5900 native block dispatcher that analyzes guest blocks, lowers/compiles them on demand, executes them through the x86-64 backend, consumes the JIT-returned `next_pc`, applies a block budget, caches native blocks by guest PC, rejects stale cache entries after guest-code changes, and bridges mutable guest memory for body `SQ` operations;',
        '- a Windows R5900 native block dispatcher that analyzes guest blocks, lowers/compiles them on demand, executes them through the x86-64 backend, consumes the JIT-returned `next_pc`, supports ordinary `BEQ`, direct `J`, and direct `JAL`, applies a block budget, caches native blocks by guest PC, rejects stale cache entries after guest-code changes, and bridges mutable guest memory for body `SQ` operations;'
    ),
    (
        '- cache fingerprints that cover straight-line body words plus supported `BEQ` and delay-slot words, with atomic cache/accounting behavior when lowering or native compilation fails;',
        '- cache fingerprints that cover straight-line body words plus supported `BEQ`/`J`/`JAL` terminator and delay-slot words, with atomic cache/accounting behavior when lowering or native compilation fails;'
    ),
    (
        '- differential Windows tests comparing reference and native x86-64 execution for integer/MMI/COP1 state, `BEQ` taken/not-taken, GPR0, differing high64 values, source-mutating delay slots, and `Store128` success/failure semantics;',
        '- differential Windows tests comparing reference and native x86-64 execution for integer/MMI/COP1 state, `BEQ`, direct `J`/`JAL`, link-before-delay ordering, GPR31 high64 preservation, source-mutating delay slots, and `Store128` success/failure semantics;'
    ),
    (
        '- a synthetic startup-shaped dispatcher test that completes **3 native guest blocks / 82 guest instructions**, covers one taken and one not-taken `BEQ`, executes both architectural delay slots, executes `SQ` at `0x00100160`, zeros 16 bytes at `0x004e2680`, preserves neighboring bytes, and stops at the next synthetic control-flow boundary `0x00100164`;',
        '- a synthetic startup-shaped dispatcher test that completes **5 native guest blocks / 87 guest instructions**, covers one taken and one not-taken `BEQ`, executes `SQ` at `0x00100160`, then executes direct `J` and `JAL` with their architectural delay slots, proves the JAL link in `GPR31`, reaches a synthetic callee prefix, and stops conservatively at unsupported `JR` `0x001001a4`;'
    ),
    (
        '`R5900BlockDispatcher` supports ordinary `BEQ` as a native block terminator and `SQ` in straight-line block bodies. For supported `BEQ`, the block body, branch instruction, and delay slot are one cache candidate. The native block evaluates the equality predicate before the architectural delay slot and returns either the branch target or fallthrough PC. `SQ` uses the low 32 bits of the base GPR plus the signed 16-bit immediate with 32-bit wrap, then silently aligns the address down to 16 bytes before writing the full 128-bit source GPR. `SQ` in a `BEQ` delay slot remains deliberately outside v0. Jumps/calls, branch-likely forms, guest loads, other guest stores, and other unsupported control flow still stop conservatively.',
        '`R5900BlockDispatcher` supports ordinary `BEQ`, direct `J`, and direct `JAL` as native block terminators plus `SQ` in straight-line block bodies. The block body, supported terminator, and architectural delay slot form one cache candidate. `BEQ` snapshots its predicate before the slot; `J` returns its fixed direct target; `JAL` writes `PC+8` to `GPR31.low64` before the slot while preserving the upper 64 bits. `SQ` uses the low 32 bits of the base GPR plus the signed 16-bit immediate with 32-bit wrap, then silently aligns the address down to 16 bytes before writing the full 128-bit source GPR. `SQ` in dispatcher-managed `BEQ` or `J`/`JAL` delay slots remains deliberately outside v0. `JR`, `JALR`, branch-likely forms, guest loads, other guest stores, and other unsupported control flow still stop conservatively.'
    ),
    (
        'The game still does **not** boot. Broader guest-memory loads/stores, jump/call execution, BSS-clearing loops beyond the validated startup `SQ`, syscall/HLE integration, graphics, audio, input, menus, and gameplay remain unimplemented.',
        'The game still does **not** boot. Broader guest-memory loads/stores, indirect `JR`/`JALR` execution, BSS-clearing loops beyond the current synthetic startup boundary, syscall/HLE integration, graphics, audio, input, menus, and gameplay remain unimplemented.'
    ),
]
for old, new in replacements:
    if text.count(old) != 1:
        raise SystemExit(f'README anchor mismatch: {old[:80]}')
    text = text.replace(old, new, 1)
path.write_text(text, encoding='utf-8')

# PROGRESS
path = Path('docs/PROGRESS.md')
text = path.read_text(encoding='utf-8')
rows = [
    (
        '| R5900 IR v0 | CI_VALIDATED | Provenance-carrying lowering covers the startup integer/MMI/COP1 subset plus scalar register `AND` and `Store128` for `SQ`; block IR adds a typed `BranchEqual64` terminator with taken/fallthrough PCs and one explicit delay slot; malformed inputs fail before their own effects |',
        '| R5900 IR v0 | CI_VALIDATED | Provenance-carrying lowering covers the startup integer/MMI/COP1 subset plus scalar `AND` and `Store128`; block IR now has typed `BranchEqual64`, `DirectJump`, and `DirectCall` terminators with explicit delay slots and validated direct targets/link state |'
    ),
    (
        '| R5900 IR reference executor v0 | CI_VALIDATED | Executes instruction/block IR against full modeled EE state and an opaque guest-memory callback bridge. `Store128` tests cover low32 effective-address wrap, 16-byte alignment-down behavior, full low/high64 forwarding, GPR0, callback failure, fault provenance and stop-before-later-instruction semantics |',
        '| R5900 IR reference executor v0 | CI_VALIDATED | Executes instruction/block IR against full modeled EE state and an opaque guest-memory callback bridge. Direct `J`/`JAL` coverage proves one delay slot, fixed targets, link-before-delay ordering, GPR31 high64 preservation and body-failure-before-link behavior; `Store128` failure provenance remains deterministic |'
    ),
    (
        '| R5900 Windows x86-64 backend v0 | CI_VALIDATED | Emits callable Windows x86-64 for the modeled startup subset, `BranchEqual64`, and `Store128`. Memory-bearing blocks use a Win64 ABI-safe helper frame/shadow space and structured execution context; native/reference Store128 differential tests cover success/failure while RW -> RX/W^X remains intact |',
        '| R5900 Windows x86-64 backend v0 | CI_VALIDATED | Emits callable Windows x86-64 for the modeled startup subset, `BranchEqual64`, `DirectJump`, `DirectCall`, and `Store128`. Native/reference direct-transfer differentials cover link ordering plus successful/failing Store128 delay IR while the Win64 helper frame and RW -> RX/W^X path remain intact |'
    ),
    (
        '| R5900 BEQ + delay slot v0 | CI_VALIDATED | Ordinary BEQ is a native block terminator. Predicate uses GPR low64 values before the slot; taken/not-taken targets are returned by generated code. Reference/x64 differential coverage includes taken, not-taken, GPR0, differing high64 and delay slots mutating rs/rt. Branch-likely and other control transfers remain out of scope |',
        '| R5900 BEQ + delay slot v0 | CI_VALIDATED | Ordinary BEQ is a native block terminator. Predicate uses GPR low64 values before the slot; taken/not-taken targets are returned by generated code. Direct J/JAL are now separately supported; branch-likely and indirect control transfers remain out of scope |'
    ),
    (
        '| R5900 SQ + guest-memory writes v0 | CI_VALIDATED | Straight-line `SQ` bodies execute through `Store128`: effective address is low32(base) + signed imm16 with 32-bit wrap, silently aligned down to 16 bytes, then all 128 source bits are written. Runtime memory failure is deterministic/non-partial; `SQ` in a BEQ delay slot is explicitly outside v0 |',
        '| R5900 SQ + guest-memory writes v0 | CI_VALIDATED | Straight-line `SQ` bodies execute through `Store128`: effective address is low32(base) + signed imm16 with 32-bit wrap, silently aligned down to 16 bytes, then all 128 source bits are written. Runtime memory failure is deterministic/non-partial; `SQ` in dispatcher-managed BEQ/J/JAL delay slots is explicitly outside v0 |'
    ),
    (
        '| R5900 native block dispatcher v0 | CI_VALIDATED | Dispatcher consumes native `next_pc`, supports ordinary BEQ + one delay slot and body `SQ`, bridges mutable `Ps2MemoryMap` writes, and fingerprints exact guest words. `MemoryAccessFailure` preserves completed prefix state/count, does not count the faulting store/block, and keeps the compiled cache reusable for a later corrected run. Synthetic startup executes 3 blocks / 82 instructions through `SQ` |',
        '| R5900 native block dispatcher v0 | CI_VALIDATED | Dispatcher consumes native `next_pc`, supports ordinary BEQ plus direct J/JAL with one delay slot and body `SQ`, fingerprints exact body/terminator/delay guest words, recompiles stale entries, and keeps JR/JALR as conservative boundaries. Synthetic startup executes 5 blocks / 87 instructions and stops at JR `0x001001a4` |'
    ),
    (
        '| R5900 startup execution v0 | CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION | Synthetic native path completes 3 blocks / 82 guest instructions: first BEQ taken, second BEQ not taken, both delay slots execute, then `SQ 0x00100160` zeros 16 bytes at `0x004e2680` and execution stops at synthetic sentinel J `0x00100164`. Final SQ E2E gate `33948139927` passed 38/38 plus pacing/package validations. External-ELF mode accepts a local legal ELF; the actual ELF has not yet been run through the Windows native path, so `EXTERNALLY_VALIDATED` is intentionally not claimed |',
        '| R5900 startup execution v0 | CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION | Synthetic native path completes 5 blocks / 87 guest instructions: two BEQs + delay slots, `SQ 0x00100160`, direct J + delay, direct JAL + link-visible delay, callee prefix, then conservative stop at unsupported JR `0x001001a4`. Code-complete SHA `e81b1e891821a2e36a7cb7b20815c2ac1fdf7221` passed Windows CI run `33954774597`, job `101275974117`, with 43/43 tests. The actual legal ELF has not been run through this expanded Windows native path, so `EXTERNALLY_VALIDATED` is intentionally not claimed |'
    ),
    (
        '| Static/binary recompiler | IN_PROGRESS | Decoder -> IR -> reference -> Windows x64 -> dispatcher now executes the modeled startup path through two ordinary BEQs, both delay slots, and the 128-bit startup `SQ`. Broader guest loads/stores, jump/call/control flow, syscall/HLE and real external ELF native execution are the next gates |',
        '| Static/binary recompiler | IN_PROGRESS | Decoder -> IR -> reference -> Windows x64 -> dispatcher now executes the modeled startup path through two BEQs, `SQ`, direct J and direct JAL. Broader guest loads/stores, indirect JR/JALR, syscall/HLE and real external ELF native execution are the next gates |'
    ),
]
for old, new in rows:
    if text.count(old) != 1:
        raise SystemExit(f'PROGRESS row anchor mismatch: {old[:80]}')
    text = text.replace(old, new, 1)

anchor = '\nThe bootstrap is materially further along but **Test Build 0.1 is not yet complete**.'
evidence = '''

Direct `J`/`JAL` v0 used explicit RED/GREEN gates across IR validation, reference execution, native x64 emission and dispatcher integration. After aligning legacy fixtures whose old contract treated J/JAL as unsupported boundaries, code-complete SHA `e81b1e891821a2e36a7cb7b20815c2ac1fdf7221` passed Windows CI run `33954774597` / job `101275974117` on `windows-2022` with MSVC 19.44.35228: **43/43 CTest passed**. Focused tests `r5900_ir_direct_transfer_validation_tests`, `r5900_ir_direct_transfer_executor_tests`, `r5900_x64_direct_transfer_windows_tests`, `r5900_block_dispatcher_direct_transfer_windows_tests`, and the startup E2E all passed. The synthetic startup now reaches unsupported JR `0x001001a4` after 5 blocks / 87 guest instructions. The same run recorded 240 pacing samples at 8.333 ms mean with zero samples above 9/10/12 ms and a 120-frame probe at 120 Hz. This is hosted-CI evidence only; no legal external game ELF was executed by that run.
'''
if text.count(anchor) != 1:
    raise SystemExit('PROGRESS evidence insertion anchor mismatch')
text = text.replace(anchor, evidence + anchor, 1)
text = text.replace(
    '4. continue the execution scope with broader guest loads/stores and the next statically evidenced jump/call/control-flow and syscall/HLE boundaries, without committing proprietary data.',
    '4. continue the execution scope with broader guest loads/stores, indirect `JR`/`JALR`, and the next syscall/HLE boundaries, without committing proprietary data.',
    1)
path.write_text(text, encoding='utf-8')
