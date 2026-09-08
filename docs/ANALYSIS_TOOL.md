# Burnout3Analyze

`Burnout3Analyze` is the developer-facing static-analysis tool for externally supplied PS2 ELF executables. It turns executable evidence into deterministic coverage data without executing PS2 game code.

## Usage

```text
Burnout3Analyze --elf <path> [--output <path>] [--max-blocks <count>] [--follow-direct-calls] [--pad-bindings]
Burnout3Analyze --help
```

Examples on Windows:

```powershell
Burnout3Analyze.exe --elf "D:\Games\Burnout3\SLUS_210.50"
Burnout3Analyze.exe --elf "D:\Games\Burnout3\SLUS_210.50" --max-blocks 8192 --follow-direct-calls --output "analysis.txt"
Burnout3Analyze.exe --elf "D:\Games\Burnout3\SLUS_210.50" --max-blocks 8192 --follow-direct-calls --pad-bindings --output "analysis-with-pad-bindings.txt"
```

`--max-blocks` defaults to `4096` and must be positive. Without `--output`, the report is written to stdout.

`--follow-direct-calls` adds explicit direct-call targets to the bounded reachability worklist. Register-indirect calls/jumps are never guessed and call traversal does not infer function boundaries.

## PAD binding discovery

`--pad-bindings` is **opt-in and analysis-only**. When absent, the pre-existing analysis report is unchanged byte-for-byte. When present, the tool appends one blank line and a deterministic `PAD_BINDINGS_V0` section.

Discovery combines two evidence classes:

- **ELF symbol evidence**: only the exact accepted names `padInit`, `padPortOpen`, `padGetState`, `padRead`, `padPortClose`, `padEnd` and their single-leading-underscore aliases are accepted. The symbol must have a nonzero guest PC, compatible ELF symbol type, and lie in file-backed executable `PF_X PT_LOAD` memory. One unique exact symbol PC resolves as `trusted` and uses fixed evidence score `1000`.
- **Static fingerprint evidence**: generated only from decoded R5900/control-flow structure and public PS2SDK/libpad semantic constants. Static evidence is always `candidate` at most, regardless of score. It never activates HLE.

`pc=none` means no unique PC was selected. For `unresolved`, evidence is absent or conflicting exact symbols exist. For `candidate`, `pc=none` means multiple static candidates remain ambiguous. Candidate evidence lines retain the individual PCs so ambiguity is explicit rather than guessed.

The scanner does not use Burnout-specific byte signatures, proprietary function hashes, extracted instruction sequences, or hardcoded game addresses.

Example PAD section:

```text
PAD_BINDINGS_V0 1
PAD_BINDING function=padInit confidence=trusted pc=0x00102000 evidence_count=1 max_score=1000
PAD_BINDING_EVIDENCE function=padInit kind=elf_symbol pc=0x00102000 score=1000 detail=padInit
PAD_BINDING function=padPortOpen confidence=unresolved pc=none evidence_count=0 max_score=0
PAD_BINDING function=padGetState confidence=unresolved pc=none evidence_count=0 max_score=0
PAD_BINDING function=padRead confidence=unresolved pc=none evidence_count=0 max_score=0
PAD_BINDING function=padPortClose confidence=unresolved pc=none evidence_count=0 max_score=0
PAD_BINDING function=padEnd confidence=unresolved pc=none evidence_count=0 max_score=0
PAD_BINDINGS_END
```

Diagnostics are deterministically sorted. Repeated runs on identical bytes/options must render byte-identical PAD sections.

## Pipeline

```text
external ELF bytes
  -> authoritative parse_ps2_elf() validation
  -> PT_LOAD mapping into Ps2MemoryMap
  -> bounded R5900 reachability analysis
  -> normal deterministic analysis report
  -> optional analysis-only ELF metadata parsing
  -> optional exact PAD symbol evidence
  -> optional public-semantics static PAD fingerprints
  -> deterministic evidence merge/report
```

Optional metadata never changes loader validity. A missing section/symbol table is normal for stripped ELFs. Malformed optional metadata becomes a discovery diagnostic after the base ELF has already passed authoritative validation; it never weakens PT_LOAD checks.

## Normal report

The normal report includes deterministic summary counters, instruction histograms, `UNKNOWN_PRIMARY_OPCODES`, `UNKNOWN_SITES`, `DIRECT_CALL_TARGETS`, block records, delay slots, edges, calls and CFG issues. `CALL_SITES` is a static-reference count, not runtime frequency.

Enabling `--follow-direct-calls` may expand evidence but does not infer functions. Enabling `--pad-bindings` appends the PAD section only; it does not execute or bind guest functions.

## Conservative rules

The analyzer deliberately does **not**:

- execute R5900 instructions or emulate PS2 hardware;
- invent indirect control-flow destinations;
- treat static call counts as runtime frequency;
- infer function boundaries beyond the fixed analysis heuristic used by the PAD candidate scanner;
- modify the supplied ELF;
- populate or activate `Ps2PadHleBindings`;
- turn static candidates into trusted runtime bindings.

`PS2 PAD Runtime Confirmation v0` is the next milestone required before discovered candidate PCs can be confirmed against live guest calls. Automatic HLE activation remains outside this milestone.

## Legal-data boundary

No Burnout 3 executable or asset belongs in this repository. Point `--elf` at an executable extracted from a legally obtained copy and stored outside the source tree. Tests use synthetic ELF/R5900 fixtures and public semantic constants only.

Generated non-proprietary analysis reports are the intended evidence artifact for further recompiler work.
