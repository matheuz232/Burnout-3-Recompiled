# Burnout3Analyze

`Burnout3Analyze` is the current developer-facing static-analysis tool for externally supplied PS2 ELF executables.

It exists to turn real executable evidence into deterministic coverage data before the project commits to an R5900-to-x86-64 recompilation strategy.

## Usage

```text
Burnout3Analyze --elf <path> [--output <path>] [--max-blocks <count>]
Burnout3Analyze --help
```

Examples on Windows:

```powershell
Burnout3Analyze.exe --elf "D:\Games\Burnout3\SLUS_210.50"
Burnout3Analyze.exe --elf "D:\Games\Burnout3\SLUS_210.50" --output "analysis.txt"
Burnout3Analyze.exe --elf "D:\Games\Burnout3\SLUS_210.50" --max-blocks 8192 --output "analysis.txt"
```

`--max-blocks` defaults to `4096` and must be a positive integer. Without `--output`, the report is written to stdout.

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
  -> render deterministic text report
```

## Report

The report begins with summary counters such as:

```text
ENTRY 0x00100000
BLOCKS 2
INSTRUCTIONS 3
DECODED 2
UNKNOWN 1
CALLS 2
INDIRECT_EXITS 1
CFG_ISSUES 2
```

It then records blocks, instruction raw words, delay slots, edges, calls and analysis issues in deterministic order. Equivalent graph evidence must produce byte-identical output regardless of internal container ordering.

## Conservative rules

The tool deliberately does **not**:

- execute R5900 instructions;
- emulate the PS2 CPU or hardware;
- follow direct call targets as though they automatically belonged to the same function;
- invent destinations for `JR`/`JALR` or other register-indirect control flow;
- infer function boundaries that are not supported by evidence;
- translate guest code to x86-64;
- modify the supplied ELF.

Unsupported decoder families, including currently unimplemented MMI/COP instructions, remain visible as `UNKNOWN`. This is intentional: real-game coverage should determine implementation priority.

## Legal-data boundary

No Burnout 3 executable or asset belongs in this repository. Point `--elf` at a file from a legally obtained copy stored outside the source tree. Tests use synthetic ELF fixtures only.

## Next evidence gate

Run the tool against a legally supplied Burnout 3 executable and retain only the generated non-proprietary analysis report/coverage evidence needed to guide further work. That evidence will determine the next decoder families, function-discovery work and eventual IR/backend design.
