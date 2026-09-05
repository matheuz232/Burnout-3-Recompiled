from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    if text.count(old) != 1:
        raise SystemExit(f"expected exactly one match in {path}: {old[:120]!r}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "README.md",
    "`Burnout 3 Recompiled - Test Build 0.1` bootstrap, static-analysis, and native R5900 startup-execution infrastructure through the startup `SQ` guest-memory write plus direct `J`/`JAL` execution with architectural delay slots.",
    "`Burnout 3 Recompiled - Test Build 0.1` bootstrap, static-analysis, and native R5900 startup-execution infrastructure through the startup `SQ` guest-memory write, direct `J`/`JAL`, and indirect `JR`/`JALR` execution with architectural delay slots.",
)
replace_once(
    "README.md",
    "- block-level R5900 IR with typed `BranchEqual64`, `DirectJump`, and `DirectCall` terminators plus one explicit architectural delay slot;",
    "- block-level R5900 IR with typed `BranchEqual64`, `DirectJump`, `DirectCall`, `IndirectJump`, and `IndirectCall` terminators plus one explicit architectural delay slot; indirect calls carry an explicit link GPR;",
)
replace_once(
    "README.md",
    "- native x86-64 control-transfer emission for `BEQ`, direct `J`, and direct `JAL`; `JAL` writes zero-extended `PC+8` to `GPR31.low64` before the delay slot while preserving `GPR31.high64`, and generated blocks return the selected guest `next_pc` in EAX;",
    "- native x86-64 control-transfer emission for `BEQ`, direct `J`/`JAL`, and indirect `JR`/`JALR`; indirect transfers snapshot the low 32-bit target before link/delay execution, `JALR` writes zero-extended `PC+8` to the decoded link GPR while preserving its high64 half, and the `rd == rs` / `rd == 0` cases are explicitly supported;",
)
replace_once(
    "README.md",
    "- a Windows R5900 native block dispatcher that analyzes guest blocks, lowers/compiles them on demand, executes them through the x86-64 backend, consumes the JIT-returned `next_pc`, supports ordinary `BEQ`, direct `J`, and direct `JAL`, applies a block budget, caches native blocks by guest PC, rejects stale cache entries after guest-code changes, and bridges mutable guest memory for body `SQ` operations;",
    "- a Windows R5900 native block dispatcher that analyzes guest blocks, lowers/compiles them on demand, executes them through the x86-64 backend, consumes the JIT-returned `next_pc`, supports ordinary `BEQ`, direct `J`/`JAL`, and indirect `JR`/`JALR`, applies a block budget, caches native blocks by guest PC, rejects stale cache entries after guest-code changes, and bridges mutable guest memory for body `SQ` operations;",
)
replace_once(
    "README.md",
    "- cache fingerprints that cover straight-line body words plus supported `BEQ`/`J`/`JAL` terminator and delay-slot words, with atomic cache/accounting behavior when lowering or native compilation fails;",
    "- cache fingerprints that cover straight-line body words plus supported `BEQ`/`J`/`JAL`/`JR`/`JALR` terminator and delay-slot words; runtime indirect target values are deliberately excluded from the key so cached `JR`/`JALR` blocks can jump to new register targets without recompilation;",
)
replace_once(
    "README.md",
    "- differential Windows tests comparing reference and native x86-64 execution for integer/MMI/COP1 state, `BEQ`, direct `J`/`JAL`, link-before-delay ordering, GPR31 high64 preservation, source-mutating delay slots, and `Store128` success/failure semantics;",
    "- differential Windows tests comparing reference and native x86-64 execution for integer/MMI/COP1 state, `BEQ`, direct `J`/`JAL`, indirect `JR`/`JALR`, target-snapshot/link-before-delay ordering, `rd == rs`, `rd == 0`, link high64 preservation, source-mutating delay slots, and `Store128` success/failure semantics;",
)
replace_once(
    "README.md",
    "- a synthetic startup-shaped dispatcher test that completes **5 native guest blocks / 87 guest instructions**, covers one taken and one not-taken `BEQ`, executes `SQ` at `0x00100160`, then executes direct `J` and `JAL` with their architectural delay slots, proves the JAL link in `GPR31`, reaches a synthetic callee prefix, and stops conservatively at unsupported `JR` `0x001001a4`;",
    "- a synthetic startup-shaped dispatcher test that completes **7 native guest blocks / 94 guest instructions**, covers one taken and one not-taken `BEQ`, executes `SQ` at `0x00100160`, direct `J`/`JAL`, `JR` with its delay slot, aliasing `JALR r5,r5` with link-visible delay semantics, reaches the indirect target at `0x001001c0`, and stops conservatively at unsupported `BNE` `0x001001c4`;",
)
replace_once(
    "README.md",
    "`R5900BlockDispatcher` supports ordinary `BEQ`, direct `J`, and direct `JAL` as native block terminators plus `SQ` in straight-line block bodies. The block body, supported terminator, and architectural delay slot form one cache candidate. `BEQ` snapshots its predicate before the slot; `J` returns its fixed direct target; `JAL` writes `PC+8` to `GPR31.low64` before the slot while preserving the upper 64 bits. `SQ` uses the low 32 bits of the base GPR plus the signed 16-bit immediate with 32-bit wrap, then silently aligns the address down to 16 bytes before writing the full 128-bit source GPR. `SQ` in dispatcher-managed `BEQ` or `J`/`JAL` delay slots remains deliberately outside v0. `JR`, `JALR`, branch-likely forms, guest loads, other guest stores, and other unsupported control flow still stop conservatively.",
    "`R5900BlockDispatcher` supports ordinary `BEQ`, direct `J`/`JAL`, and indirect `JR`/`JALR` as native block terminators plus `SQ` in straight-line block bodies. The block body, supported terminator, and architectural delay slot form one cache candidate. `BEQ` snapshots its predicate before the slot; `J` returns its fixed direct target; `JAL` writes `PC+8` to `GPR31.low64` before the slot while preserving the upper 64 bits. `JR`/`JALR` snapshot the low 32-bit runtime target before the slot, and `JALR` writes its decoded link GPR before the delay slot without disturbing that register's high64 half; `rd == rs` therefore jumps using the old target, while `rd == 0` suppresses the link write. `SQ` uses the low 32 bits of the base GPR plus the signed 16-bit immediate with 32-bit wrap, then silently aligns the address down to 16 bytes before writing the full 128-bit source GPR. `SQ` in dispatcher-managed `BEQ`, `J`/`JAL`, or `JR`/`JALR` delay slots remains deliberately outside v0. `BNE`, branch-likely forms, guest loads, other guest stores, and other unsupported control flow still stop conservatively.",
)
replace_once(
    "README.md",
    "The game still does **not** boot. Broader guest-memory loads/stores, indirect `JR`/`JALR` execution, BSS-clearing loops beyond the current synthetic startup boundary, syscall/HLE integration, graphics, audio, input, menus, and gameplay remain unimplemented.",
    "The game still does **not** boot. Broader guest-memory loads/stores, additional control flow such as `BNE`/branch-likely forms, BSS-clearing loops beyond the current synthetic startup boundary, syscall/HLE integration, graphics, audio, input, menus, and gameplay remain unimplemented. The current `JR`/`JALR` milestone is **CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION**; the legally supplied external ELF has not been run through this expanded Windows native path in this environment.",
)

replace_once(
    "docs/PROGRESS.md",
    "| R5900 IR v0 | CI_VALIDATED | Provenance-carrying lowering covers the startup integer/MMI/COP1 subset plus scalar `AND` and `Store128`; block IR now has typed `BranchEqual64`, `DirectJump`, and `DirectCall` terminators with explicit delay slots and validated direct targets/link state |",
    "| R5900 IR v0 | CI_VALIDATED | Provenance-carrying lowering covers the startup integer/MMI/COP1 subset plus scalar `AND` and `Store128`; block IR now has typed `BranchEqual64`, `DirectJump`, `DirectCall`, `IndirectJump`, and `IndirectCall` terminators with explicit delay slots, runtime-target GPR inputs, and validated link state |",
)
replace_once(
    "docs/PROGRESS.md",
    "| R5900 IR reference executor v0 | CI_VALIDATED | Executes instruction/block IR against full modeled EE state and an opaque guest-memory callback bridge. Direct `J`/`JAL` coverage proves one delay slot, fixed targets, link-before-delay ordering, GPR31 high64 preservation and body-failure-before-link behavior; `Store128` failure provenance remains deterministic |",
    "| R5900 IR reference executor v0 | CI_VALIDATED | Executes instruction/block IR against full modeled EE state and an opaque guest-memory callback bridge. Direct and indirect transfer coverage proves one delay slot, low32 target snapshot before `JALR` link mutation, `rd == rs`, `rd == 0`, link-before-delay ordering, high64 preservation and deterministic memory-fault ordering |",
)
replace_once(
    "docs/PROGRESS.md",
    "| R5900 Windows x86-64 backend v0 | CI_VALIDATED | Emits callable Windows x86-64 for the modeled startup subset, `BranchEqual64`, `DirectJump`, `DirectCall`, and `Store128`. Native/reference direct-transfer differentials cover link ordering plus successful/failing Store128 delay IR while the Win64 helper frame and RW -> RX/W^X path remain intact |",
    "| R5900 Windows x86-64 backend v0 | CI_VALIDATED | Emits callable Windows x86-64 for the modeled startup subset, direct/indirect control transfers, and `Store128`. `JR`/`JALR` use the Win64 helper frame with the low32 target snapshotted at `[rsp+0x30]`; native/reference differentials cover aliasing, link ordering and successful/failing Store128 delay IR while the RW -> RX/W^X path remains intact |",
)
replace_once(
    "docs/PROGRESS.md",
    "| R5900 BEQ + delay slot v0 | CI_VALIDATED | Ordinary BEQ is a native block terminator. Predicate uses GPR low64 values before the slot; taken/not-taken targets are returned by generated code. Direct J/JAL are now separately supported; branch-likely and indirect control transfers remain out of scope |",
    "| R5900 BEQ + delay slot v0 | CI_VALIDATED | Ordinary BEQ is a native block terminator. Predicate uses GPR low64 values before the slot; taken/not-taken targets are returned by generated code. Direct J/JAL and indirect JR/JALR are separately supported; branch-likely and BNE execution remain out of scope |\n| R5900 JR + JALR v0 | CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION | `IndirectJump`/`IndirectCall` snapshot the low32 target before link/delay execution; `JALR` supports arbitrary `rd`, `rd == rs`, and `rd == 0` while preserving high64. Dispatcher/cache differentials and the startup E2E are green; external legal-ELF execution remains pending |",
)
replace_once(
    "docs/PROGRESS.md",
    "| R5900 SQ + guest-memory writes v0 | CI_VALIDATED | Straight-line `SQ` bodies execute through `Store128`: effective address is low32(base) + signed imm16 with 32-bit wrap, silently aligned down to 16 bytes, then all 128 source bits are written. Runtime memory failure is deterministic/non-partial; `SQ` in dispatcher-managed BEQ/J/JAL delay slots is explicitly outside v0 |",
    "| R5900 SQ + guest-memory writes v0 | CI_VALIDATED | Straight-line `SQ` bodies execute through `Store128`: effective address is low32(base) + signed imm16 with 32-bit wrap, silently aligned down to 16 bytes, then all 128 source bits are written. Runtime memory failure is deterministic/non-partial; `SQ` in dispatcher-managed BEQ/J/JAL/JR/JALR delay slots is explicitly outside v0 |",
)
replace_once(
    "docs/PROGRESS.md",
    "| R5900 native block dispatcher v0 | CI_VALIDATED | Dispatcher consumes native `next_pc`, supports ordinary BEQ plus direct J/JAL with one delay slot and body `SQ`, fingerprints exact body/terminator/delay guest words, recompiles stale entries, and keeps JR/JALR as conservative boundaries. Synthetic startup executes 5 blocks / 87 instructions and stops at JR `0x001001a4` |",
    "| R5900 native block dispatcher v0 | CI_VALIDATED | Dispatcher consumes native `next_pc`, supports ordinary BEQ plus direct J/JAL and indirect JR/JALR with one delay slot and body `SQ`, fingerprints exact body/terminator/delay guest words, excludes runtime indirect target values from the cache key, and recompiles stale code. Synthetic startup executes 7 blocks / 94 instructions and stops at unsupported BNE `0x001001c4` |",
)
replace_once(
    "docs/PROGRESS.md",
    "| R5900 startup execution v0 | CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION | Synthetic native path completes 5 blocks / 87 guest instructions: two BEQs + delay slots, `SQ 0x00100160`, direct J + delay, direct JAL + link-visible delay, callee prefix, then conservative stop at unsupported JR `0x001001a4`. Code-complete SHA `e81b1e891821a2e36a7cb7b20815c2ac1fdf7221` passed Windows CI run `33954774597`, job `101275974117`, with 43/43 tests. The actual legal ELF has not been run through this expanded Windows native path, so `EXTERNALLY_VALIDATED` is intentionally not claimed |",
    "| R5900 startup execution v0 | CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION | Synthetic native path completes 7 blocks / 94 guest instructions: two BEQs + delay slots, `SQ 0x00100160`, direct J/JAL, JR return with executed delay, aliasing `JALR r5,r5` with link-visible delay, indirect callee entry, then conservative stop at unsupported BNE `0x001001c4`. Clean SHA `2f7a68fcd493f9bb8dcca84a9c5633c1d5f3cab5` passed Windows CI run `33983976120`, job `101354052009`, with 47/47 tests plus pacing/package validation. The actual legal ELF has not been run through this expanded Windows native path, so `EXTERNALLY_VALIDATED` is intentionally not claimed |",
)
replace_once(
    "docs/PROGRESS.md",
    "| Static/binary recompiler | IN_PROGRESS | Decoder -> IR -> reference -> Windows x64 -> dispatcher now executes the modeled startup path through two BEQs, `SQ`, direct J and direct JAL. Broader guest loads/stores, indirect JR/JALR, syscall/HLE and real external ELF native execution are the next gates |",
    "| Static/binary recompiler | IN_PROGRESS | Decoder -> IR -> reference -> Windows x64 -> dispatcher now executes the modeled startup path through two BEQs, `SQ`, direct J/JAL, and indirect JR/JALR. Broader guest loads/stores, additional branch/control-flow forms beginning with the current BNE boundary, syscall/HLE and real external ELF native execution are the next gates |",
)
replace_once(
    "docs/PROGRESS.md",
    "## Windows CI evidence\n",
    "## Windows CI evidence\n\n`JR + JALR v0` used explicit TDD gates through IR validation, reference execution, native x64 differentials, dispatcher/cache behavior and startup E2E. The clean feature SHA `2f7a68fcd493f9bb8dcca84a9c5633c1d5f3cab5` passed Windows CI run `33983976120`, job `101354052009`, with **47/47 tests**. The synthetic startup reaches unsupported `BNE 0x001001c4` after **7 native blocks / 94 guest instructions**. Pacing telemetry reported 240 samples at 8.333 ms mean with zero samples over 9/10/12 ms; the one-second probe completed 120/120 frames. Analyzer and pacing-probe package staging/validation also passed. This is CI evidence only; no legal external ELF was executed in that run.\n",
)

print("Task 6 documentation updated")
