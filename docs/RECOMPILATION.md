# Recompilation Strategy

## Status

**STRATEGY NOT SELECTED YET. STRUCTURAL ELF ANALYSIS INFRASTRUCTURE IS NOW IN PROGRESS.**

No static/binary recompilation strategy is considered justified before examining the legally supplied PS2 executable. The first analysis component is therefore a read-only, platform-independent ELF32/MIPS loader rather than an execution engine.

## Current ELF milestone

`src/recompiler/ps2_elf.*` now provides strict parsing for the structural subset needed to begin executable analysis:

- ELF magic and identification fields;
- ELF32 class;
- little-endian encoding;
- ET_EXEC type;
- EM_MIPS machine;
- ELF version;
- ELF/program-header sizes;
- entry point;
- program-header count;
- PT_LOAD metadata;
- file-range and memory-size validation;
- bounded views into segment file bytes.

The parser intentionally does **not** execute instructions, map PS2 memory, infer game semantics, parse symbols/relocations yet, or emulate hardware. Tests construct synthetic ELF images and contain no Burnout 3 proprietary data.

## Evidence required before strategy selection

Analysis of a legally supplied original executable must identify at minimum:

- ELF entry point and program/section layout;
- symbol availability;
- relocations;
- executable/data regions;
- global memory usage;
- imported/runtime library calls;
- PS2-specific modules/syscalls;
- Emotion Engine, DMA, VU0/VU1, GS, SPU2 and IOP interactions.

The ELF loader covers only the first structural portion of this list. Section headers, symbols and relocations are still future work.

## Candidate approaches

The project may ultimately combine:

- static translation of MIPS functions to native C/C++;
- MIPS-to-IR translation followed by x86-64 code generation;
- manually reconstructed high-confidence functions;
- HLE/native replacements for well-understood PS2 services;
- temporary interpretation/compatibility components used only as development aids.

The decision will be documented after executable evidence exists. A full PS2 emulator is explicitly out of scope.

## Function-analysis rule

For each discovered MIPS function:

1. preserve original address and size;
2. identify inputs/outputs;
3. identify side effects and memory references;
4. build pseudocode from evidence;
5. compare against the original binary/behavior;
6. only then implement or translate;
7. mark VERIFIED only after behavioral validation.
