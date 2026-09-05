# R5900 indirect jump/call v0 design

Date: 2026-09-05

Branch: `feature/r5900-indirect-jump-call-v0`

Base branch: `feature/r5900-beq-delay-slot-v0`

Base SHA: `27d553fcb0959407af3e6b13503c652458f7d8f1`

## Goal

Extend the native Windows R5900 startup execution path with architectural `JR` and `JALR` execution, including one delay slot, correct target snapshot ordering, `JALR rd == rs` aliasing, arbitrary link destination register handling, native/reference differential coverage, dispatcher/cache support, and an expanded synthetic startup end-to-end path.

The milestone remains Windows-only and native-recompiler-oriented. It does not add a generic PS2 emulator.

## Scope

### In scope

- `JR` as an `IndirectJump` IR terminator.
- `JALR` as an `IndirectCall` IR terminator.
- Exactly one architectural delay slot for both instructions.
- Runtime target snapshot from the low 32 bits of the source GPR before the delay slot.
- `JALR` target snapshot before any link write.
- Arbitrary `JALR rd` link destination, including `rd == rs` and `rd == 0`.
- Link value `guest_pc + 8`, written before the delay slot.
- Link write updates only destination `low64`; destination `high64` is preserved.
- Reference executor support.
- Native x64 backend support under the existing Win64 ABI.
- Dispatcher lowering and cache execution for `JR` and `JALR`.
- Dynamic target changes on cache hits without recompilation.
- Deterministic follow-up analysis failure for invalid runtime targets.
- Explicit dispatcher rejection of `SQ` in `JR`/`JALR` delay slots.
- Synthetic startup continuation through the existing `JR r31`, then a real aliasing `JALR r5,r5`, then a new unsupported `BNE` boundary.
- Dedicated focused tests plus full Windows CI, pacing, and package validation.

### Out of scope

- `BNE` execution.
- Other branch families or branch-likely execution.
- Branch-and-link families.
- Control transfer instructions in a delay slot and their exception semantics.
- Dispatcher-managed guest-memory operations in control-transfer delay slots.
- Call-stack modeling, return prediction, function discovery, or devirtualization.
- Syscall/HLE.
- Guest loads or additional store widths.
- Graphics, audio, input, menus, gameplay, or game boot.
- Claiming external legal-ELF validation without actually running the legal ELF.

## Existing architecture

The decoder and control-flow analyzer already distinguish `JR` and `JALR`:

- `JR` is a jump with a delay slot and no link flag.
- `JALR` is a jump with a delay slot and a link flag.
- analysis ends them as `IndirectJump` and `IndirectCall` respectively.

The current recompiler IR supports `Fallthrough`, `BranchEqual64`, `DirectJump`, and `DirectCall` terminators. The reference executor and x64 backend support those four kinds. The dispatcher currently recognizes only supported `BEQ`, `J`, and `JAL` final control transfers. `JR`/`JALR` are therefore the next explicit control-flow boundary.

The existing cache fingerprints guest code words and includes a supported terminator plus its delay-slot word. That cache model is already suitable for indirect transfers because the runtime target register value is execution state, not code identity.

## Chosen architecture

Use two typed terminators:

```cpp
enum class R5900IrTerminatorKind {
    Fallthrough = 0,
    BranchEqual64,
    DirectJump,
    DirectCall,
    IndirectJump,
    IndirectCall,
};
```

Retain the existing terminator fields and add one field used only by `IndirectCall`:

```cpp
std::uint8_t link_gpr{};
```

The source target register is represented through `inputs[0]`, keeping the existing operand validation machinery and avoiding an additional source-register field.

A single generic `IndirectTransfer` kind was rejected because it would encode optional link behavior and permit invalid combinations. Lowering `JALR` into synthetic body instructions plus `IndirectJump` was also rejected because it would hide the critical architectural ordering between target snapshot, link write, and delay-slot execution.

## IR contracts

### IndirectJump

`JR rs` lowers to:

```text
kind            = IndirectJump
inputs          = [GPR(rs)]
taken_pc        = 0
fallthrough_pc  = 0
target_pc       = 0
link_pc         = 0
link_gpr        = 0
delay_slot      = exactly one lowered IR instruction
```

The target is dynamic and therefore is not encoded in `target_pc`.

### IndirectCall

`JALR rd, rs` lowers to:

```text
kind            = IndirectCall
inputs          = [GPR(rs)]
taken_pc        = 0
fallthrough_pc  = 0
target_pc       = 0
link_pc         = uint32(guest_pc + 8)
link_gpr        = rd
delay_slot      = exactly one lowered IR instruction
```

`link_gpr` may be any architectural GPR index from 0 through 31.

### Existing terminators

`link_gpr` must remain zero for `Fallthrough`, `BranchEqual64`, `DirectJump`, and `DirectCall`. The direct-call destination remains fixed to GPR31 in this v0 representation.

## Validation

All terminator guest PCs remain 4-byte aligned.

### IndirectJump validation

Require:

- exactly one input;
- input kind is GPR;
- input GPR index is less than 32;
- `taken_pc == 0`;
- `fallthrough_pc == 0`;
- `target_pc == 0`;
- `link_pc == 0`;
- `link_gpr == 0`;
- exactly one delay-slot IR instruction;
- delay-slot IR instruction validates normally.

No target alignment check is possible at block-validation time because the target is runtime state.

### IndirectCall validation

Require:

- exactly one input;
- input kind is GPR;
- input GPR index is less than 32;
- `taken_pc == 0`;
- `fallthrough_pc == 0`;
- `target_pc == 0`;
- `link_gpr < 32`;
- `link_pc` is 4-byte aligned;
- `link_pc == uint32(guest_pc + 8)`;
- exactly one delay-slot IR instruction;
- delay-slot IR instruction validates normally.

### Existing terminator tightening

Validation for existing terminators additionally rejects non-zero `link_gpr` so the new field cannot silently leak into unrelated IR kinds.

## Reference executor semantics

The block execution order remains:

1. reset memory-fault status;
2. validate block;
3. execute body;
4. execute terminator semantics;
5. return the next guest PC.

A body failure prevents target snapshot, link write, and delay-slot execution.

### JR

For `JR rs`:

```text
target32 = uint32(state.gpr[rs].low64)
execute delay slot exactly once
next_pc = target32
```

The target is captured before the delay slot. Therefore a delay-slot write to `rs` does not change this transfer's destination.

`JR` does not modify GPR31 or any other link register.

### JALR

For `JALR rd, rs`:

```text
target32 = uint32(state.gpr[rs].low64)

if rd != 0:
    state.gpr[rd].low64 = uint64(link_pc)
    state.gpr[rd].high64 = preserved

normalize GPR0
execute delay slot exactly once
next_pc = target32
```

The target snapshot occurs before the link write. This is mandatory for aliasing such as `JALR r5,r5`: the jump uses the old value of `r5`, while the architectural state after the link contains `PC+8` in `r5.low64`.

When `rd == 0`, no persistent link write occurs and GPR0 remains normalized to zero.

The delay slot sees the updated link. If the delay slot later writes the same destination GPR, the delay-slot write wins.

If a delay-slot guest-memory operation fails at the lower-level IR path, the link write remains committed because it happened before the delay slot.

## Native x64 backend

Add a dedicated indirect-transfer compile path rather than overloading the direct-transfer emitter.

### Snapshot storage

Indirect transfer compilation always uses the existing Win64 helper frame, even when body and delay slot contain no helper call.

The existing frame subtracts `0x38` bytes from RSP:

- `[rsp+0x00 .. rsp+0x1f]`: 32-byte Win64 shadow space;
- `[rsp+0x20]`: saved state pointer;
- `[rsp+0x28]`: saved execution-context pointer;
- `[rsp+0x30 .. rsp+0x37]`: remaining 8-byte local area.

Use `[rsp+0x30]` as the indirect-target snapshot local. Store the target as a 32-bit value and reload it into EAX after successful delay-slot execution.

This avoids depending on volatile registers across a delay slot that may call `r5900_native_store128`, and avoids introducing a new non-volatile register preservation contract.

### Native JR sequence

Conceptually:

```text
helper-frame prologue
normalize GPR0
emit body

load EAX = low32(GPR[rs].low64)
store EAX -> [rsp+0x30]

normalize GPR0
emit delay once
normalize GPR0

load EAX <- [rsp+0x30]
helper-frame epilogue
RET
```

### Native JALR sequence

Conceptually:

```text
helper-frame prologue
normalize GPR0
emit body

load EAX = low32(GPR[rs].low64)
store EAX -> [rsp+0x30]      // snapshot before link

if link_gpr != 0:
    EAX = link_pc
    store RAX -> GPR[link_gpr].low64

normalize GPR0
emit delay once
normalize GPR0

load EAX <- [rsp+0x30]
helper-frame epilogue
RET
```

The link store writes only the low 64-bit half and preserves high64.

### Helper failure in delay slot

`Store128` already emits an immediate native-block return after a failed helper call, with the execution context carrying the memory fault. For an indirect call, the link has already been written before that delay slot and remains committed. The saved target is irrelevant on the failure path because the x64 wrapper returns `MemoryAccessFailure` instead of a next PC.

No ABI change is required. The generated function signature remains:

```cpp
std::uint32_t (*)(R5900IrExecutionState*, R5900IrExecutionContext*)
```

W^X behavior, Win64 shadow-space requirements, stack alignment, and helper-call conventions remain unchanged.

## Dispatcher support

Recognize only the analyzer/decoder pairs:

```text
IndirectJump analyzer end + final decoded JR     -> supported JR
IndirectCall analyzer end + final decoded JALR   -> supported JALR
```

No other indirect transfer becomes executable implicitly.

For supported `JR`/`JALR`:

- eligible instructions before the terminator become the IR body;
- the terminator itself is not body-lowered;
- the analyzer-provided delay slot must exist and remain readable;
- cache guest words include body + terminator + delay slot;
- `JR` terminator input is `GPR(rs)`;
- `JALR` terminator input is `GPR(rs)`;
- `JALR link_gpr = rd`;
- `JALR link_pc = guest_pc + 8`;
- delay slot lowers exactly once;
- `SQ` in an indirect-transfer delay slot is explicitly rejected as `LoweringFailure`;
- compiled/native `next_pc` becomes the next dispatcher PC;
- no post-body `boundary_reason` is assigned to a supported `JR` or `JALR`.

The current supported-transfer abstraction should be extended from `BEQ/J/JAL` to `BEQ/J/JAL/JR/JALR` without duplicating the surrounding cache and lowering pipeline.

## Cache contract

The cache identity remains code-only:

```text
body words
terminator word
delay-slot word
```

The runtime contents of the indirect target GPR are not part of the fingerprint.

This is a required behavior: a cached `JR r5` or `JALR ...,r5` block executed later with a different `r5.low64` must produce the new dynamic target without recompilation.

Changing the terminator word or delay-slot word must invalidate/recompile the block as before.

For supported indirect transfers:

```text
end_pc_exclusive = terminator_pc + 8
```

This describes the guest code span participating in the cached block, not the runtime target.

## Invalid runtime targets

The indirect transfer itself does not repair or pre-validate the target.

The native/reference result is exactly:

```text
next_pc = uint32(snapshot_gpr_low64)
```

If that runtime target is misaligned, unmapped, or mapped but non-executable, the indirect-transfer block has already executed and is counted normally. The next dispatcher iteration asks the analyzer to start at that PC and stops with deterministic `AnalysisFailure` according to existing analyzer rules.

The previous block's state, link write, and delay-slot effects remain committed.

## Delay-slot scope restriction

At the IR/reference/backend level, `Store128` remains valid in a delay slot so differential tests can verify helper-frame behavior, memory fault ordering, and link commitment.

At dispatcher v0 level, `SQ` remains excluded from dispatcher-managed delay slots for all currently supported control transfers:

```text
BEQ
J
JAL
JR
JALR
```

The dispatcher reports `LoweringFailure` at the delay-slot PC with a transfer-specific or generalized message.

Control transfers inside a delay slot remain outside scope.

## Synthetic startup E2E

The current synthetic startup reaches:

```text
0x00100160  SQ    r0, 0(r2)
0x00100164  J     0x00100180
0x00100168  ADDIU r22, r0, 0x0033
...
0x00100180  JAL   0x001001a0
0x00100184  ADDIU r23, r31, 0
0x001001a0  ADDIU r24, r0, 0x0055
0x001001a4  JR    r31
0x001001a8  NOP
```

The milestone replaces the old `JR` boundary with an executable return and arranges the previously poison call-continuation region to exercise `JALR` aliasing.

### Approved expanded layout

```text
0x00100180  JAL   0x001001a0
0x00100184  ADDIU r23, r31, 0          ; sees direct-call link

0x00100188  LUI   r5, 0x0010
0x0010018c  ORI   r5, r5, 0x01c0       ; r5 = 0x001001c0
0x00100190  JALR  r5, r5               ; rd == rs aliasing case
0x00100194  ADDIU r6, r5, 0            ; sees JALR link 0x00100198
0x00100198  poison
0x0010019c  poison/guard

0x001001a0  ADDIU r24, r0, 0x0055
0x001001a4  JR    r31
0x001001a8  ADDIU r29, r0, 0x0077      ; proves JR delay executes
0x001001ac..0x001001bc                  ; poison/guard region

0x001001c0  ADDIU r7, r0, 0x0066       ; proves indirect target entry
0x001001c4  BNE   ...                   ; new unsupported boundary
0x001001c8  NOP                         ; mapped BNE delay, not executed
```

Poison registers in both unreachable guard regions must remain unchanged. The only intended path through the call-continuation region is `LUI/ORI/JALR/delay`; the direct callee remains independently reachable through the earlier `JAL` target at `0x001001a0`.

### Expected final state

```text
reason                 = ControlFlow
next_pc                = 0x001001c4
blocks_executed        = 7
instructions_executed  = 94

GPR22.low64             = 0x33
GPR23.low64             = 0x00100188
GPR24.low64             = 0x55
GPR29.low64             = 0x77
GPR31.low64             = 0x00100188

GPR5.low64              = 0x00100198
GPR6.low64              = 0x00100198
GPR7.low64              = 0x66

SQ target 0x004e2680    = 16 zero bytes
poison registers        = unchanged
```

### Accounting

The previous `J/JAL` startup fixture reached the unsupported `JR` after five blocks and 87 instructions.

With `JR/JALR` support:

- first two startup BEQ blocks: unchanged;
- `SQ + J + J-delay`: unchanged;
- `JAL + JAL-delay`: unchanged;
- callee block becomes `ADDIU + JR + JR-delay`, adding two executed instructions and returning to `0x00100188`: cumulative 89;
- return-continuation block executes `LUI + ORI + JALR + JALR-delay`: +4, cumulative 93;
- indirect target block executes one `ADDIU` before unsupported `BNE`: +1, cumulative 94;
- `BNE` and its delay slot are not executed or counted.

Total blocks: 7.

## External legal ELF boundary

Do not assume the real legal Burnout 3 ELF has the same post-`SQ` `JR/JALR` layout as the synthetic fixture.

The existing external startup harness remains conservative unless the legal ELF is actually run and inspected. Synthetic CI validation proves the new semantics and infrastructure only; it does not confer `EXTERNALLY_VALIDATED` status.

## TDD gates

### Gate 1 — IR validation

RED/GREEN coverage:

- valid `IndirectJump`;
- valid `IndirectCall`;
- invalid/non-GPR/multiple/missing target inputs;
- target GPR index out of range;
- forbidden direct/branch PC fields;
- nonzero `link_pc` or `link_gpr` on `IndirectJump`;
- `IndirectCall link_gpr` out of range;
- misaligned/incorrect `IndirectCall link_pc`;
- missing or multiple delay instructions;
- invalid delay IR propagated;
- existing terminators reject nonzero `link_gpr`.

### Gate 2 — reference executor

Cover:

- JR returns low32 target;
- JR snapshots target before delay-slot source mutation;
- JR does not modify link registers;
- JALR snapshots target before link;
- JALR `rd == rs` aliasing;
- JALR `rd == 0`;
- JALR link is `PC+8` and zero-extended into low64;
- destination high64 preserved;
- delay sees new link;
- delay write to link destination wins afterward;
- body failure prevents link/delay;
- lower-level delay memory failure leaves link committed.

### Gate 3 — x64 differential

Reference/native differential coverage for:

- JR;
- JALR;
- `rd == rs`;
- `rd == 0`;
- nonzero destination high64 preservation;
- target source high64 ignored for next PC;
- delay mutates target source after snapshot;
- delay reads new link;
- delay overwrites link destination;
- representative body;
- `Store128` delay success through helper call;
- `Store128` delay failure and committed link state;
- next-PC equality between reference and native paths.

### Gate 4 — dispatcher/cache

Cover:

- JR at block entry;
- JR after eligible body prefix;
- JALR at block entry;
- JALR after eligible body prefix;
- exact link destination and link PC;
- `rd == rs` through dispatcher;
- cache hit with unchanged guest words but changed runtime target GPR produces the new target;
- no recompilation caused by target-register value changes;
- terminator mutation recompiles;
- delay-slot mutation recompiles;
- cached `end_pc_exclusive == terminator_pc + 8`;
- `SQ` in JR delay rejected;
- `SQ` in JALR delay rejected;
- invalid dynamic target is counted in the completed indirect block and fails on the following analysis iteration.

### Gate 5 — startup E2E

Require exact:

```text
ControlFlow
next_pc = 0x001001c4
blocks = 7
instructions = 94
```

Also require direct-call state, JR delay proof, JALR aliasing/link-before-delay proof, indirect target entry proof, unchanged poison registers, and preserved startup `SQ` memory result.

### Gate 6 — full Windows CI

Run and require green:

- all CTest targets;
- dedicated indirect-transfer validation tests;
- dedicated indirect-transfer reference-executor tests;
- dedicated indirect-transfer x64 differential tests;
- dedicated indirect-transfer dispatcher/cache tests;
- startup E2E;
- frame pacing telemetry;
- 120 Hz pacing probe;
- analyzer package staging/validation;
- pacing-probe package staging/validation.

Existing BEQ, direct J/JAL, SQ, guest-memory bridge, cache identity, W^X, and pacing behavior must not regress.

## Completion criteria

The milestone is complete when all of the following are true:

- `IndirectJump` and `IndirectCall` are typed and fully validated.
- Reference and native execution match for JR/JALR semantics.
- JALR snapshots target before link, including `rd == rs`.
- JALR supports arbitrary `rd`, including zero.
- Link is `PC+8`, written before delay, low64-only with high64 preservation.
- Native target snapshot survives helper-capable successful delay slots.
- Dispatcher executes JR/JALR and caches their guest code spans correctly.
- Cached indirect blocks respond to runtime target-register changes without recompilation.
- Invalid dynamic targets fail deterministically on the next analysis iteration after the indirect block is counted.
- Dispatcher still rejects SQ in all supported control-transfer delay slots.
- Synthetic startup reaches the new BNE boundary at `0x001001c4` with exactly 7 blocks and 94 executed instructions.
- Startup SQ remains correct.
- Full Windows CI, pacing, and package validation are green.
- Documentation reports only `CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION` until a legal external ELF is actually run.

## Deferred next work

After this milestone, the next control-flow boundary in the approved synthetic startup is `BNE`. Broader startup progress will then require a separate design choice between expanding conditional-branch coverage and prioritizing guest loads/BSS-clearing loops encountered in the legal ELF. That decision is intentionally deferred until JR/JALR v0 is complete and externally inspected where possible.
