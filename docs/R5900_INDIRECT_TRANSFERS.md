# R5900 JR/JALR execution v0

Implemented on `feature/r5900-indirect-jump-v0`, based on direct-transfer
milestone `27d553fcb0959407af3e6b13503c652458f7d8f1`.

## Execution contract

1. Validate the complete IR block before CPU-state mutation.
2. Execute the body. A body memory fault stops before the jump, link write,
   or delay slot, retaining the existing memory-fault reporting contract.
3. Capture `uint32_t(GPR[rs].low64)` as the guest destination.
4. For JALR, write the zero-extended, wrapping 32-bit `PC+8` link to
   `GPR[rd].low64` unless `rd == 0`. Preserve `high64`.
5. Execute the one supported delay instruction exactly once.
6. Return the captured destination to the dispatcher as a guest PC.

The source is read after body execution and before link/delay writes. Thus a
delay that changes `rs` cannot redirect the jump. `JALR rd == rs` uses the
original source value for the destination and makes the new link visible to
the delay. A later delay write to `rd` wins. Both halves of GPR0 remain zero.

This deterministic aliasing policy agrees with the ordering in the
[PCSX2 interpreter's JR/JALR implementation](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Interpreter.cpp).
Its low-word destination and link-write conventions were inspected as an
implementation reference; this increment does not claim hardware validation
of architecturally restricted instruction sequences.

## IR and native implementation

`IndirectJump` carries one source GPR. `IndirectCall` adds an explicit
`link_gpr` and a validated `link_pc == guest_pc + 8`. Other terminators reject
`link_gpr`; existing direct calls retain their fixed GPR31 contract.

The native backend uses the existing 0x38-byte Win64 frame. It stores the
captured destination in the local at `[rsp+0x30]`, outside the 32-byte shadow
area and saved state/context pointers, then restores it into EAX after the
delay. Body SQ helper calls are supported and complete before target capture.
Generated code does not jump to or dereference a guest PC as a host pointer.

## Dispatcher and faults

The cache stores guest body, terminator and delay words, not a resolved
indirect destination. Reusing a cached block therefore reads the current
register value on every execution. Code changes to the source encoding or
delay invalidate the cached block; self-loops remain bounded by `max_blocks`.

The returned destination retains all 32 bits, including alignment bits.
On the next dispatch iteration, existing analysis rejects unaligned,
unmapped, or non-executable instruction fetches with `AnalysisFailure` at
that destination. The already completed jump and delay remain committed and
counted. If the block budget ends first, the returned PC is pending fetch.
This is the current dispatcher stop contract, not a modeled COP0 exception.

An unsupported body instruction terminates selection before a later supported
transfer. The eligible prefix may execute, but the unexecuted body tail,
transfer, link write and delay cannot be skipped over or counted.

## Limits and next work

- SQ in JR/JALR delay slots is rejected by IR validation and the dispatcher.
  Control-flow, traps and other unsupported delay instructions are rejected
  before body/link effects. Broader delay-slot exception semantics remain open.
- Static reachability still leaves indirect destinations unresolved. Only
  runtime dispatch uses actual register values.
- The synthetic startup executes 6 blocks / 90 instructions, returns through
  JR, and stops before SYSCALL at `0x0010018c`. The syscall address and post-SQ
  continuation are synthetic fixtures, not additional real-game evidence.
- No real game ELF was executed in this increment. External Windows startup
  validation, further memory/loop instructions and syscall/HLE remain pending.

The next implementation should use an external legal ELF run/report to locate
the first remaining unsupported instruction or syscall and implement that
bounded operation with reference/native tests. Keep the broader syscall/HLE
and game-boot milestones separate from this validated function-return path.

## Validation

Code `9366a61e7b896d6dc503f33bd2f53164ecba620f` passed 29/29 GCC 13.3 portable
tests and [46/46 Windows/MSVC tests](https://github.com/matheuz232/Burnout-3-Recompiled/actions/runs/33974114747).
The Windows suite executes generated machine code, compares complete modeled
CPU state against the reference executor, and tests decoder-to-dispatcher
integration and the synthetic startup return.
