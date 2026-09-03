# Recompilation Strategy

## Status

**NOT SELECTED YET.**

No static/binary recompilation strategy is considered justified before examining the legally supplied PS2 executable.

## Evidence required before selection

The next analysis milestone must identify at minimum:

- ELF entry point and program/section layout;
- symbol availability;
- relocations;
- executable/data regions;
- global memory usage;
- imported/runtime library calls;
- PS2-specific modules/syscalls;
- Emotion Engine, DMA, VU0/VU1, GS, SPU2 and IOP interactions.

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
