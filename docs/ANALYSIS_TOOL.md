# Burnout3Analyze

`Burnout3Analyze` is the current developer-facing static-analysis tool for externally supplied PS2 ELF executables.

It exists to turn real executable evidence into deterministic coverage data before the project commits to an R5900-to-x86-64 recompilation strategy.

## Usage

```text
Burnout3Analyze --elf <path> [--output <path>] [--max-blocks <count>] [--follow-direct-calls]
Burnout3Analyze --help
```

Examples on Windows:

```powershell
Burnout3Analyze.exe --elf "D:\Games\Burnout3\SLUS_210.50"
Burnout3Analyze.exe --elf "D:\Games\Burnout3\SLUS_210.50" --output "analysis.txt"
Burnout3Analyze.exe --elf "D:\Games\Burnout3\SLUS_210.50" --max-blocks 8192 --output "analysis.txt"
Burnout3Analyze.exe --elf "D:\Games\Burnout3\SLUS_210.50" --max-blocks 8192 --follow-direct-calls --output "analysis-with-callees.txt"
```

`--max-blocks` defaults to `4096` and must be a positive integer. Without `--output`, the report is written to stdout.

Direct call targets are evidence-only by default, preserving the original entry-root reachable-CFG behavior. `--follow-direct-calls` opts into adding explicit `DirectCall` targets to the same deterministic worklist. Callees and ordinary control-flow successors share the same `--max-blocks` budget. Direct calls remain recorded as call evidence even when their targets are traversed.

The option never resolves register-indirect calls or jumps. `JALR`, `JR`, and other indirect exits remain unresolved evidence, and an explicit direct callee that cannot be analyzed is reported through the existing `TargetAnalysisFailed` issue path. Traversing a call target does not assert or infer a function boundary.

## Pipeline

```text
external file
  -> read ELF bytes
  -> validate ELF32 little-endian MIPS executable
  -> map PT_LOAD regions into Ps2MemoryMap
  -> start at ELF entry point
  -> decode supported R5900 instructions
  -> construct conservative basic blocks
  -> traverse bounded direct reachable CFG
  -> optionally enqueue explicit direct-call targets
  -> render deterministic text report
```

## Report

The report begins with summary counters and coverage diagnostics such as:

```text
ENTRY 0x00100000
BLOCKS 2
INSTRUCTIONS 3
DECODED 2
UNKNOWN 1
CALLS 4
INDIRECT_EXITS 1
CFG_ISSUES 2
INSTRUCTION_HISTOGRAM 3
  BEQ 1
  NOP 1
  UNKNOWN 1
UNKNOWN_PRIMARY_OPCODES 1
  0x1C 1
UNKNOWN_SITES 1
  PC 0x00100020 RAW 0x712A4CC1 PRIMARY 0x1C RS 0x09 RT 0x0A RD 0x09 SA 0x13 FUNCT 0x01
DIRECT_CALL_TARGETS 2
  TARGET 0x00123450 CALL_SITES 3
  TARGET 0x00208000 CALL_SITES 1
```

`INSTRUCTION_HISTOGRAM` counts every reachable instruction site represented by the graph, including architectural delay slots, and sorts instruction names deterministically. `UNKNOWN_PRIMARY_OPCODES` counts only unresolved instructions and groups them by the six-bit MIPS primary opcode extracted from the raw word.

`UNKNOWN_SITES` records every reachable `UNKNOWN` site, including unknown delay-slot instructions, and sorts sites by guest PC with the raw word as a deterministic tie-breaker. Each record exposes the 32-bit word plus the raw MIPS bit positions `PRIMARY`, `RS`, `RT`, `RD`, `SA`, and `FUNCT`.

Those fields are **diagnostic bitfields, not decoded operands**. Their semantic meaning depends on the instruction format and opcode family; for an unsupported instruction some listed fields may not represent registers, shift amounts, or function selectors at all. The section exists to make unresolved SPECIAL/MMI/COP families easier to group from real evidence without claiming instruction semantics that the decoder does not yet implement.

`DIRECT_CALL_TARGETS` groups resolved direct-call evidence by target guest address and reports the number of static call sites that reference each target. Targets are sorted by address. Indirect calls and direct calls without a resolved target are excluded from this aggregation but remain visible in the ordinary `CALL` records.

`CALL_SITES` is a static-reference count from the analyzed graph. It is **not** an execution-frequency profile, does not prove that a target was executed, and does not label the target as a function or infer function boundaries.

The unknown-primary histogram, unknown-site records and direct-call target aggregation are diagnostic evidence, not a decoder or profiler. They expose where unresolved instruction work and repeated direct references exist so real executable evidence can drive implementation priority.

The report then records blocks, instruction raw words, delay slots, edges, calls and analysis issues in deterministic order. Equivalent graph evidence must produce byte-identical output regardless of internal container ordering. Enabling `--follow-direct-calls` can increase block/instruction coverage and therefore the available evidence, but does not change the report schema.

## Conservative rules

The tool deliberately does **not**:

- execute R5900 instructions;
- emulate the PS2 CPU or hardware;
- follow direct call targets unless `--follow-direct-calls` is explicitly requested;
- invent destinations for `JR`/`JALR` or other register-indirect control flow;
- infer function boundaries from call traversal or call-target aggregation;
- treat static `CALL_SITES` counts as runtime frequency;
- translate guest code to x86-64;
- modify the supplied ELF.

Unsupported decoder families, including currently unimplemented MMI/COP instructions, remain visible as `UNKNOWN`. This is intentional: real-game coverage should determine implementation priority.

## Legal-data boundary

No Burnout 3 executable or asset belongs in this repository. Point `--elf` at a file from a legally obtained copy stored outside the source tree. Tests use synthetic ELF fixtures only.

## Next evidence gate

Run the tool against a legally supplied Burnout 3 executable and retain only the generated non-proprietary analysis report/coverage evidence needed to guide further work. For the broadest evidence, run once with `--follow-direct-calls` and a sufficiently large `--max-blocks` value, then inspect `BlockLimitReached`, `TargetAnalysisFailed`, unresolved indirect exits, instruction histograms, unknown-primary coverage, `UNKNOWN_SITES`, and `DIRECT_CALL_TARGETS` before expanding decoder or recompiler scope.
