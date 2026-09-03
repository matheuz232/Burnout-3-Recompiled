# R5900 Decoder

## Status

Initial static decoder implemented on `feature/r5900-decoder`.

This module is an **analysis primitive**, not a CPU interpreter or emulator. It accepts one 32-bit Emotion Engine instruction word and returns structured metadata suitable for later control-flow discovery and static recompilation work.

## Current output

`decode_r5900()` exposes:

- raw 32-bit instruction word;
- primary opcode;
- `rs`, `rt`, `rd`, shift amount and function fields;
- 16-bit immediate and signed-immediate view;
- 26-bit jump target field;
- decoded instruction identity for the currently supported subset;
- instruction class: ALU, branch, jump, load, store, system or unknown;
- memory width for decoded load/store instructions;
- branch-likely flag;
- link flag;
- architectural delay-slot flag;
- direct branch/jump target when it can be resolved from `PC` and the instruction alone;
- stable instruction names for diagnostics and future analysis reports.

## Current coverage

The first milestone covers common integer/control-flow operations and primary-opcode load/store instructions needed to begin scanning executable EE code. It includes R5900-specific `LQ`/`SQ` and 128-bit COP2 load/store widths.

The opcode organization was cross-checked against the public R5900 opcode tables used by PCSX2, but this project contains an independent decoder implementation and does not import emulator execution/recompiler code.

## Intentionally unsupported in this milestone

The following decode families currently remain `Unknown` unless represented by a covered top-level instruction:

- MMI packed/SIMD sub-opcodes;
- COP0 sub-opcodes and TLB/exception-control instructions;
- COP1 FPU sub-opcodes;
- COP2/VU macro-mode instruction decoding;
- instruction execution semantics;
- register state;
- memory side effects;
- dynamic branch-target resolution for `JR`/`JALR`;
- basic-block construction and function discovery.

Keeping unsupported instructions explicit prevents the analysis pipeline from silently assigning incorrect semantics.

## Control-flow rules

Direct conditional branches resolve target addresses as:

```text
PC + 4 + (sign_extend(immediate) << 2)
```

Direct `J`/`JAL` targets resolve as:

```text
((PC + 4) & 0xF0000000) | (target26 << 2)
```

`JR` and `JALR` are classified as delayed indirect jumps and deliberately return no static direct target.

Branch-likely instructions retain an explicit `likely` flag because their delay-slot execution rules differ from normal branches and will matter when the basic-block analyzer is introduced.

## Next gate

After compiler/CI validation, the next analysis component should walk executable ELF-backed words from known entry points, use this decoder to identify control-flow edges, and emit evidence without executing game code. MMI/COP families should be added incrementally when real executable coverage demonstrates that they are needed.
