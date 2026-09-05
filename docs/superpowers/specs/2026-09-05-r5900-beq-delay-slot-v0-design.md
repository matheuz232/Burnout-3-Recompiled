# R5900 BEQ + Delay Slot v0 Design

Date: 2026-09-05
Branch: `feature/r5900-beq-delay-slot-v0`
Base: `main` at `f9337b977655763076fd2729883b401bd37f3b4a`

## Goal

Extend the native R5900 execution path past the first control-flow boundary in the Burnout 3 startup. The milestone must natively execute the first two real `BEQ` instructions and their architectural delay slots, then stop before the first `SQ` at guest PC `0x00100160`.

Success on the approved startup path is:

- first `BEQ` at `0x00100130`: taken;
- delay slot at `0x00100134`: executed;
- next PC: `0x0010014C`;
- second block executes `LUI`, `ORI`, `AND`;
- second `BEQ` at `0x00100158`: not taken;
- delay slot at `0x0010015C`: executed;
- next PC: `0x00100160`;
- dispatcher stops at unsupported `SQ`;
- total completed guest instructions: 81;
- total completed guest blocks: 2.

This milestone does not implement guest-memory writes, BSS clearing loops, `SQ`, syscalls, jumps/calls, branch-likely behavior, graphics, audio, input, or boot.

## Architectural approach

Use a native block terminator model. Branch evaluation and delay-slot execution are compiled into the Windows x86-64 block. The dispatcher consumes the block's returned next guest PC.

Rejected alternatives:

1. Interpret the branch condition in C++ after native body execution. This would create an interpreter seam in the control-flow path.
2. Direct-chain generated blocks. This would introduce link patching and cache invalidation complexity before it is required.

The selected design keeps the existing dispatcher/cache architecture, leaves guest PC outside `R5900IrExecutionState`, and creates a clean basis for later `J/JAL/JR/JALR` support.

## IR block contract

Add block-level IR while preserving instruction-level IR.

```cpp
enum class R5900IrTerminatorKind {
    Fallthrough = 0,
    BranchEqual64,
};

struct R5900IrTerminator {
    std::uint32_t guest_pc{};
    std::uint32_t guest_raw{};
    R5900IrTerminatorKind kind{R5900IrTerminatorKind::Fallthrough};
    std::vector<R5900IrOperand> inputs{};
    std::uint32_t taken_pc{};
    std::uint32_t fallthrough_pc{};
    std::vector<R5900IrInstruction> delay_slot{};
};

struct R5900IrBlock {
    std::vector<R5900IrInstruction> body{};
    R5900IrTerminator terminator{};
};
```

`BranchEqual64` accepts exactly two GPR operands and compares their low 64-bit scalar values. High 64-bit GPR halves do not participate in `BEQ`.

`BEQ` target and fallthrough are explicit block metadata:

- `taken_pc = direct_target(guest_pc)`;
- `fallthrough_pc = guest_pc + 8`.

Only ordinary `BEQ` is supported here. `BEQL` and every other conditional-branch family remain unsupported.

## Delay-slot semantics

The branch predicate must be evaluated before the delay slot, because the delay-slot instruction may modify a branch source register.

Required semantic order:

1. read `rs.low64` and `rt.low64`;
2. compute the branch outcome;
3. execute the delay-slot IR exactly once;
4. return the previously selected next PC.

The initial x64 implementation may duplicate the delay-slot machine code in the taken and not-taken paths:

```text
CMP rs.low64, rt.low64
JNE not_taken

taken:
    <delay slot>
    normalize GPR0
    MOV EAX, taken_pc
    RET

not_taken:
    <delay slot>
    normalize GPR0
    MOV EAX, fallthrough_pc
    RET
```

This avoids adding temporary branch state to the EE execution-state structure.

## x64 backend ABI

Change compiled-block execution from:

```cpp
void execute(R5900IrExecutionState& state) const noexcept;
```

to:

```cpp
std::uint32_t execute(R5900IrExecutionState& state) const noexcept;
```

The generated Windows x64 function remains a one-argument function receiving `R5900IrExecutionState*` in `RCX`, but now returns guest `next_pc` in `EAX`.

Add:

```cpp
compile_r5900_ir_x64(const R5900IrBlock& block)
```

The existing vector overload remains source-compatible and produces fallthrough behavior. For a non-empty vector it returns `last.guest_pc + 4`; for an empty vector it returns `0`.

## Reference executor

Add a block-level reference executor returning the selected guest PC while mutating architectural state.

For `BranchEqual64` it must snapshot the predicate before executing the delay slot, then execute the delay slot through the existing instruction-level executor and finally return `taken_pc` or `fallthrough_pc`.

The reference path remains the semantic oracle for x64 differential testing.

## `AND` support

The real startup path at `0x00100154` uses scalar `AND rd,rs,rt`.

No new IR opcode is required. Lower it as:

```text
And64
Gpr(rd) <- Gpr(rs) & Gpr(rt)
```

using `Low64PreserveUpper64`. Writing `rd == 0` remains a provenance-preserving no-op. The existing `And64` validator/backend support is generalized from the current GPR+immediate form to also accept GPR+GPR.

## Dispatcher model

The block analyzer already exposes the control-transfer terminator, architectural delay slot and taken/not-taken edges. The dispatcher will consume these structures instead of treating every branch as an immediate boundary.

For an ordinary supported `BEQ`:

1. build body IR from instructions preceding the terminator;
2. lower the delay-slot instruction;
3. build `BranchEqual64` terminator metadata;
4. compile/cache the full IR block;
5. execute the native block;
6. receive `next_pc` from native code;
7. continue dispatch from that PC while budget remains.

Other branches/jumps still stop as control-flow boundaries.

## Cache contract

A cached branch-capable block fingerprint must include every guest word that affects native behavior:

- straight-line body;
- `BEQ` terminator word;
- delay-slot word.

Exact guest-word comparison remains authoritative in addition to FNV-1a fingerprinting.

Changing any of body, terminator or delay slot invalidates/recompiles the block. Changing only runtime GPR values does not invalidate the block; the same compiled code must decide taken/not-taken dynamically.

No direct block links are cached or patched in this milestone.

## Accounting

A completed BEQ block contributes:

```text
body instruction count
+ 1 branch instruction
+ 1 executed delay-slot instruction
```

The delay slot counts once even if its generated machine code exists in both branch paths.

`blocks_executed` increments after a guest block completes. `next_pc` is the value returned by the compiled block.

The approved startup milestone expects:

```text
blocks_executed       = 2
instructions_executed = 81
next_pc               = 0x00100160
reason                = UnsupportedInstruction
```

## Failure and atomicity contract

A branch-capable block is executable only if all required stages succeed before execution:

1. analysis;
2. body lowering;
3. delay-slot lowering;
4. terminator validation;
5. complete IR validation;
6. x64 compilation.

If any step fails, no part of that block executes and no replacement cache entry is published.

Specific rules:

- malformed `BEQ` IR is rejected before native emission;
- missing or non-executable delay slot is an analysis failure;
- unsupported delay-slot instruction is a lowering/unsupported failure before execution;
- taken and fallthrough PCs must be 4-byte aligned;
- GPR0 normalization remains before/after applicable generated work;
- the branch predicate is always captured before the delay slot;
- no PC field is added to `R5900IrExecutionState`;
- no branch-likely annul behavior is implemented;
- no partial cache replacement occurs on compile failure.

## TDD plan requirements

Implementation must follow explicit RED -> GREEN gates.

### Block IR / validation

Positive cases:

- `BEQ r1,r2`;
- `BEQ r0,r0`;
- distinct taken/fallthrough PCs;
- NOP delay slot;
- delay slot that modifies one compared GPR.

Negative cases:

- FPR/immediate branch operands;
- wrong operand count;
- malformed terminator;
- unaligned target/fallthrough;
- unsupported delay-slot IR.

### Reference semantics

Critical ordering test:

```text
r1 = 5
r2 = 5
BEQ r1,r2,target
delay: ADDIU r1,r1,1
```

Expected:

```text
branch outcome = taken
r1 = 6
next_pc = target
```

A matching not-taken case is required.

### x64 differential

Compare reference block execution and native x64 execution across the complete modeled state and returned `next_pc` for:

- taken;
- not taken;
- `r0 == r0`;
- equal low64 with different high64 values;
- delay slot modifying `rs`;
- delay slot modifying `rt`.

### Cache

Prove recompilation for mutations in:

- body;
- BEQ word;
- delay-slot word.

Prove a cache hit remains valid when only GPR runtime values change and the same cached block selects a different branch outcome.

### Synthetic dispatcher end-to-end

Use generated synthetic encodings only. Build a path equivalent in control-flow structure to:

```text
block 1 -> BEQ taken
block 2 -> BEQ not taken
block 3 -> SQ unsupported
```

Required result:

```text
blocks_executed       = 2
instructions_executed = 81
next_pc               = 0x00100160
reason                = UnsupportedInstruction
```

Both delay-slot side effects must be observed.

## Real ELF validation

The user-supplied `SLUS_210.50` remains external and must never be committed.

Known static boundary evidence to validate against:

```text
entry                  0x00100008
first BEQ              0x00100130
first taken target     0x0010014C
second BEQ             0x00100158
second fallthrough     0x00100160
next blocker           SQ at 0x00100160
```

The existing external Windows startup harness will be extended to expect the new boundary. Static inspection alone is not enough to claim `EXTERNALLY_VALIDATED`; the legal external ELF must actually execute through the Windows native path first.

## Explicit exclusions

Not included:

- `SQ` or any guest-memory load/store semantics;
- BSS clear loops;
- `BEQL` or other likely branches;
- BNE/BLEZ/BGTZ/REGIMM families;
- J/JAL/JR/JALR;
- direct block chaining;
- guest PC in architectural state;
- syscalls/HLE;
- graphics/audio/input;
- game initialization/menu/gameplay;
- a claim that Burnout 3 boots.

## Completion criterion

The milestone is complete when Windows CI proves the synthetic native branch path and its differential semantics, documentation accurately records the result, and the dispatcher deterministically reaches unsupported `SQ` at `0x00100160` after two completed BEQ blocks and 81 executed guest instructions.

External validation remains a separate status until the user-supplied ELF is actually executed through the Windows external harness.
