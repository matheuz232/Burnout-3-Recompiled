from pathlib import Path

RUN_ID = "33993550729"
JOB_ID = "101379915351"
CODE_SHA = "6d486b9a1276542ec1f1117c76fc431fb2495a54"


def replace_prefixed_line(lines, prefix, replacement):
    matches = [i for i, line in enumerate(lines) if line.startswith(prefix)]
    if len(matches) != 1:
        raise SystemExit(f"expected exactly one line starting {prefix!r}, found {len(matches)}")
    lines[matches[0]] = replacement


# README ---------------------------------------------------------------------
readme_path = Path("README.md")
readme_lines = readme_path.read_text(encoding="utf-8").splitlines()

replace_prefixed_line(
    readme_lines,
    "`Burnout 3 Recompiled - Test Build 0.1` bootstrap,",
    "`Burnout 3 Recompiled - Test Build 0.1` bootstrap, static-analysis, and native R5900 startup-execution infrastructure through the startup `SQ` guest-memory write, ordinary `BEQ`/`BNE`, branch-likely `BEQL`/`BNEL`, direct `J`/`JAL`, and indirect `JR`/`JALR` execution with architectural delay slots.",
)
replace_prefixed_line(
    readme_lines,
    "- block-level R5900 IR with typed `BranchEqual64`,",
    "- block-level R5900 IR with typed `BranchEqual64`, `BranchEqualLikely64`, `BranchNotEqualLikely64`, `DirectJump`, `DirectCall`, `IndirectJump`, and `IndirectCall` terminators plus one explicit architectural delay slot; indirect calls carry an explicit link GPR;",
)
replace_prefixed_line(
    readme_lines,
    "- native x86-64 control-transfer emission for ordinary `BEQ`/`BNE`,",
    "- native x86-64 control-transfer emission for ordinary `BEQ`/`BNE`, branch-likely `BEQL`/`BNEL`, direct `J`/`JAL`, and indirect `JR`/`JALR`; `BEQL`/`BNEL` evaluate the low64 predicate before the slot, execute the delay exactly once only when taken, and branch around the complete emitted delay path when not taken; `BNE` reuses the equality terminator with swapped runtime destinations; indirect transfers snapshot the low 32-bit target before link/delay execution, `JALR` writes zero-extended `PC+8` to the decoded link GPR while preserving its high64 half, and the `rd == rs` / `rd == 0` cases are explicitly supported;",
)
replace_prefixed_line(
    readme_lines,
    "- a Windows R5900 native block dispatcher that analyzes guest blocks,",
    "- a Windows R5900 native block dispatcher that analyzes guest blocks, lowers/compiles them on demand, executes them through the x86-64 backend, consumes the JIT-returned `next_pc`, supports ordinary `BEQ`/`BNE`, branch-likely `BEQL`/`BNEL`, direct `J`/`JAL`, and indirect `JR`/`JALR`, applies a block budget, caches native blocks by guest PC, rejects stale cache entries after guest-code changes, and bridges mutable guest memory for body `SQ` operations;",
)
replace_prefixed_line(
    readme_lines,
    "- cache fingerprints that cover straight-line body words plus supported `BEQ`/`BNE`/`BEQL`/`BNEL`/`J`/`JAL`/`JR`/`JALR`",
    "- cache fingerprints that cover straight-line body words plus supported `BEQ`/`BNE`/`BEQL`/`BNEL`/`J`/`JAL`/`JR`/`JALR` terminator and delay-slot words; runtime branch predicates and indirect target values are deliberately excluded from the key so cached conditional/indirect blocks can change runtime outcomes without recompilation;",
)
replace_prefixed_line(
    readme_lines,
    "- differential Windows tests comparing reference and native x86-64 execution for integer/MMI/COP1 state,",
    "- differential Windows tests comparing reference and native x86-64 execution for integer/MMI/COP1 state, ordinary `BEQ`, branch-likely `BEQL`/`BNEL`, direct `J`/`JAL`, indirect `JR`/`JALR`, likely-branch annulment, target-snapshot/link-before-delay ordering, `rd == rs`, `rd == 0`, link high64 preservation, source-mutating delay slots, and `Store128` success/failure semantics; dispatcher tests additionally cover likely taken/not-taken behavior, cache reuse across runtime predicate changes, branch/delay-word invalidation, selected-guest-word accounting, and `SQ` delay rejection;",
)
replace_prefixed_line(
    readme_lines,
    "`R5900BlockDispatcher` supports ordinary `BEQ`/`BNE`,",
    "`R5900BlockDispatcher` supports ordinary `BEQ`/`BNE`, branch-likely `BEQL`/`BNEL`, direct `J`/`JAL`, and indirect `JR`/`JALR` as native block terminators plus `SQ` in straight-line block bodies. The block body, supported terminator, and architectural delay slot form one cache candidate. `BEQ` and `BNE` evaluate their low64 GPR predicates before the slot and always execute one architectural delay instruction; `BNE` reuses `BranchEqual64` with the equality and inequality destinations swapped. `BEQL` and `BNEL` also decide the low64 predicate before the slot, but implement architectural branch-likely annulment: the taken path executes the delay exactly once while the not-taken path bypasses all delay code and returns `PC+8`. `J` returns its fixed direct target; `JAL` writes `PC+8` to `GPR31.low64` before the slot while preserving the upper 64 bits. `JR`/`JALR` snapshot the low 32-bit runtime target before the slot, and `JALR` writes its decoded link GPR before the delay slot without disturbing that register's high64 half; `rd == rs` therefore jumps using the old target, while `rd == 0` suppresses the link write. `SQ` uses the low 32 bits of the base GPR plus the signed 16-bit immediate with 32-bit wrap, then silently aligns the address down to 16 bytes before writing the full 128-bit source GPR. `SQ` in dispatcher-managed `BEQ`/`BNE`/`BEQL`/`BNEL`, `J`/`JAL`, or `JR`/`JALR` delay slots remains deliberately outside v0. `BLEZL`, `BGTZL`, REGIMM likely/link-likely variants, guest loads, other guest stores, and other unsupported control flow still stop conservatively.",
)
replace_prefixed_line(
    readme_lines,
    "The game still does **not** boot.",
    "The game still does **not** boot. Broader guest-memory loads/stores, additional control flow including `BLEZL`/`BGTZL` and REGIMM likely/link-likely variants, BSS-clearing loops beyond the current synthetic startup boundary, syscall/HLE integration, graphics, audio, input, menus, and gameplay remain unimplemented. The `BEQL + BNEL branch-likely v0` milestone is **CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION** with 51/51 Windows tests; the legally supplied external ELF has not been run through this expanded Windows native path in this environment.",
)

scope_line = "Validated native control transfers: `BEQ`, `BNE`, `BEQL`, `BNEL`, `J`, `JAL`, `JR`, `JALR`. `BEQL`/`BNEL` implement architectural branch-likely annulment: their delay slot executes only on the taken path. `BLEZL`/`BGTZL` and REGIMM likely/link-likely variants remain unsupported. External legal-ELF validation of this expanded path is pending. The game does not boot yet."
anchor = "The startup execution state models all 32 EE GPRs"
anchor_matches = [i for i, line in enumerate(readme_lines) if line.startswith(anchor)]
if len(anchor_matches) != 1:
    raise SystemExit("README scope insertion anchor missing or ambiguous")
idx = anchor_matches[0]
if scope_line not in readme_lines:
    readme_lines[idx:idx] = [scope_line, ""]

readme_path.write_text("\n".join(readme_lines) + "\n", encoding="utf-8")


# PROGRESS -------------------------------------------------------------------
progress_path = Path("docs/PROGRESS.md")
progress_lines = progress_path.read_text(encoding="utf-8").splitlines()

replace_prefixed_line(
    progress_lines,
    "| R5900 IR v0 |",
    "| R5900 IR v0 | CI_VALIDATED | Provenance-carrying lowering covers the startup integer/MMI/COP1 subset plus scalar `AND` and `Store128`; block IR now has typed `BranchEqual64`, `BranchEqualLikely64`, `BranchNotEqualLikely64`, `DirectJump`, `DirectCall`, `IndirectJump`, and `IndirectCall` terminators with explicit delay slots, runtime-target GPR inputs, and validated link state |",
)
replace_prefixed_line(
    progress_lines,
    "| R5900 IR reference executor v0 |",
    "| R5900 IR reference executor v0 | CI_VALIDATED | Executes instruction/block IR against full modeled EE state and an opaque guest-memory callback bridge. BEQL/BNEL reference execution proves low64 predicate capture before delay, taken-only delay execution, zero not-taken delay/helper/fault effects, and Store128 failure propagation; direct/indirect transfer ordering coverage remains green |",
)
replace_prefixed_line(
    progress_lines,
    "| R5900 Windows x86-64 backend v0 |",
    "| R5900 Windows x86-64 backend v0 | CI_VALIDATED | Emits callable Windows x86-64 for the modeled startup subset, ordinary and likely conditional branches, direct/indirect control transfers, and `Store128`. BEQL uses JNE and BNEL uses JE to branch around the complete delay path on predicate failure; reference/native differentials cover taken/not-taken annulment and Store128 helper suppression/failure while the RW -> RX/W^X path remains intact |",
)
replace_prefixed_line(
    progress_lines,
    "| R5900 BEQ + delay slot v0 |",
    "| R5900 BEQ + delay slot v0 | CI_VALIDATED | Ordinary BEQ is a native block terminator. Predicate uses GPR low64 values before the slot; taken/not-taken targets are returned by generated code and the delay always executes once. BEQL/BNEL are separately supported by explicit likely terminators; ordinary BEQ semantics remain unchanged |",
)
replace_prefixed_line(
    progress_lines,
    "| R5900 SQ + guest-memory writes v0 |",
    "| R5900 SQ + guest-memory writes v0 | CI_VALIDATED | Straight-line `SQ` bodies execute through `Store128`: effective address is low32(base) + signed imm16 with 32-bit wrap, silently aligned down to 16 bytes, then all 128 source bits are written. Runtime memory failure is deterministic/non-partial; `SQ` in dispatcher-managed BEQ/BNE/BEQL/BNEL/J/JAL/JR/JALR delay slots is explicitly outside v0 |",
)
replace_prefixed_line(
    progress_lines,
    "| R5900 native block dispatcher v0 |",
    "| R5900 native block dispatcher v0 | CI_VALIDATED | Dispatcher consumes native `next_pc`, supports ordinary BEQ/BNE, likely BEQL/BNEL, direct J/JAL and indirect JR/JALR with one selected delay word and body `SQ`, fingerprints exact body/terminator/delay guest words, excludes runtime predicate/indirect target values from the cache key, and recompiles stale code. Not-taken likely paths annul delay effects while `instructions_executed` remains selected-word accounting. Synthetic startup remains 7 blocks / 96 instructions at analysis failure `0x001001cc` |",
)
replace_prefixed_line(
    progress_lines,
    "| R5900 startup execution v0 |",
    f"| R5900 startup execution v0 | CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION | Synthetic native path remains 7 blocks / 96 selected guest instructions through two BEQs, `SQ 0x00100160`, direct J/JAL, JR/JALR and BNE before deterministic `AnalysisFailure` at `0x001001cc`. Branch-likely expansion passed Windows CI run `{RUN_ID}`, job `{JOB_ID}`, SHA `{CODE_SHA}`, with 51/51 tests and all package gates. The actual legal ELF has not been run through this expanded Windows native path, so `EXTERNALLY_VALIDATED` is intentionally not claimed |",
)
replace_prefixed_line(
    progress_lines,
    "| Static/binary recompiler |",
    "| Static/binary recompiler | IN_PROGRESS | Decoder -> IR -> reference -> Windows x64 -> dispatcher now executes the modeled startup path through ordinary BEQ/BNE, branch-likely BEQL/BNEL, `SQ`, direct J/JAL, and indirect JR/JALR. Broader guest loads/stores, BLEZL/BGTZL and REGIMM likely/link-likely control flow, syscall/HLE and real external ELF native execution are the next gates |",
)

new_row_prefix = "| R5900 BEQL + BNEL branch-likely v0 |"
if not any(line.startswith(new_row_prefix) for line in progress_lines):
    bne_indexes = [i for i, line in enumerate(progress_lines) if line.startswith("| R5900 BNE + delay slot v0 |")]
    if len(bne_indexes) != 1:
        raise SystemExit("BNE milestone row missing or ambiguous")
    new_row = (
        f"| R5900 BEQL + BNEL branch-likely v0 | CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION | "
        f"IR uses `BranchEqualLikely64` + `BranchNotEqualLikely64`. Reference/native annulment differential coverage: PASS. "
        f"Dispatcher/cache likely-branch coverage: PASS, including runtime predicate cache hits, branch/delay mutation recompilation, selected-word accounting, and SQ-delay rejection. "
        f"CTest: 51/51. Synthetic startup: 7 blocks / 96 selected guest words / `AnalysisFailure @ 0x001001cc`. "
        f"Windows CI run `{RUN_ID}`, job `{JOB_ID}`, code SHA `{CODE_SHA}`: 240 pacing samples at 8.333 ms mean (8.329 min / 8.338 max / 0.000 stddev), zero >9/10/12 ms, high-resolution timer YES; probe 120/120 at 8.333 ms mean. "
        f"External legal ELF: NOT RUN for this expanded path. Game boot: NOT ACHIEVED |"
    )
    progress_lines.insert(bne_indexes[0] + 1, new_row)

heading = "## Windows CI evidence"
heading_indexes = [i for i, line in enumerate(progress_lines) if line == heading]
if len(heading_indexes) != 1:
    raise SystemExit("Windows CI evidence heading missing or ambiguous")
evidence = (
    f"`BEQL + BNEL branch-likely v0` completed explicit IR-validation, reference-executor, native-x64, and dispatcher/cache TDD gates. "
    f"The clean code SHA `{CODE_SHA}` passed Windows CI run `{RUN_ID}`, job `{JOB_ID}`, with **51/51 tests**. "
    f"Pacing telemetry reported 240 samples at 8.333 ms mean (8.329 ms min, 8.338 ms max, 0.000 ms stddev), P50/P95/P99 8.333 ms, and zero samples above 9/10/12 ms; high-resolution timing was active. "
    f"The one-second probe completed 120/120 frames at 8.333 ms mean (8.330 ms min, 8.333 ms max), with zero samples above 9/10/12 ms. "
    f"Analyzer and pacing-probe package staging/validation passed. A legacy direct-transfer test boundary was moved from BNEL to still-unsupported BGTZ because BNEL is now intentionally executable; production semantics were unchanged by that fixture repair. This remains CI evidence only: the legal external ELF was not executed and the game does not boot."
)
if evidence not in progress_lines:
    insert_at = heading_indexes[0] + 1
    progress_lines[insert_at:insert_at] = ["", evidence]

progress_path.write_text("\n".join(progress_lines) + "\n", encoding="utf-8")
