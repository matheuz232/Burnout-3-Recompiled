# R5900 Static Control-Flow Analysis — Design

## Scope

Implement a conservative, read-only basic-block analyzer over the already validated PS2 guest memory map and R5900 decoder. It must never execute guest instructions or infer register values.

## Architecture

Add a new `b3r_analysis` library under `src/analysis/`. It depends on `b3r_runtime` (guest-memory reads and ELF segment flags) and `b3r_recompiler` (R5900 decoding). This avoids making the recompiler layer depend back on runtime and prevents a dependency cycle.

## Input

- `const runtime::Ps2MemoryMap&`;
- aligned guest start PC;
- analysis options, including a finite instruction limit.

By default, every fetched instruction and architectural delay slot must reside in an ELF-backed region whose `PF_X` flag is set.

## Output

A basic block contains:

- start PC;
- linear decoded instructions through and including the terminating control instruction;
- an optional architectural delay-slot instruction stored separately;
- explicit end kind;
- explicit control-flow edges with direct targets only when statically resolvable;
- branch-likely metadata describing whether the delay slot executes on the not-taken path.

## Conservative termination

Stop analysis on:

- conditional branch;
- direct jump;
- direct call;
- indirect jump;
- indirect call;
- `SYSCALL` / `BREAK` trap;
- unsupported/unknown instruction;
- configured instruction limit.

Fail rather than guess when the start PC is unaligned/unmapped/non-executable or when a required instruction/delay slot cannot be fetched from executable memory.

## Edges

- conditional branch: taken target + not-taken `PC+8`;
- `J`: direct jump target;
- `JAL`: call target + continuation `PC+8`;
- `JR`: indirect jump edge with no invented target;
- `JALR`: indirect call edge + continuation `PC+8`;
- instruction-limit block: explicit fallthrough to the next PC so chunking does not lose reachability.

## Delay slots

The delay slot is not appended as an ordinary linear instruction because branch-likely instructions annul it on the not-taken path. The block records whether the delay slot executes on fallthrough (`!terminator.likely`).

## Non-goals

- no instruction execution;
- no register propagation;
- no indirect-target recovery;
- no function discovery yet;
- no MMI/COP/VU semantics beyond what the decoder already recognizes;
- no whole-program graph traversal yet.
