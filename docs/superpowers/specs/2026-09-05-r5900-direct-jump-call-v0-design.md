# R5900 Direct J/JAL + Delay Slot v0 Design

Date: 2026-09-05
Branch: `feature/r5900-direct-jump-call-v0`
Base: `feature/r5900-beq-delay-slot-v0` at `e232282fd4b997053cf26c95721bc457f8d66610`

## Goal

Extend the Windows-native R5900 startup path beyond the synthetic direct-jump boundary after the already validated startup `SQ` by adding direct `J` and `JAL` terminators, one architectural delay slot, and `JAL` link-register semantics to the IR, validator, reference executor, x86-64 backend, dispatcher, and startup end-to-end fixture.

The current integrated path is already CI-validated through:

- startup entry `0x00100008`;
- first ordinary `BEQ` and its delay slot;
- second ordinary `BEQ` and its delay slot;
- `SQ` at `0x00100160` writing sixteen zero bytes to `0x004e2680`;
- 3 completed synthetic blocks / 82 guest instructions;
- a synthetic `J` sentinel at `0x00100164`, where dispatch currently stops.

This milestone converts that synthetic `J` boundary into executable control flow, then proves a synthetic `JAL` and stops at a mapped `JR` boundary that remains intentionally unsupported.

The actual legal external ELF has not yet been executed through the native startup harness in this environment. No real-file post-`SQ` opcode or PC is assumed by this design, and this milestone must not claim external validation merely because the synthetic fixture advances.

## Scope

In scope:

1. `R5900IrTerminatorKind::DirectJump` for `J`.
2. `R5900IrTerminatorKind::DirectCall` for `JAL`.
3. Exactly one architectural delay-slot instruction for each direct transfer.
4. `JAL` link semantics: write GPR31 low 64 bits with the zero-extended 32-bit `PC + 8` value before executing the delay slot, preserving GPR31 high 64 bits.
5. Reference/native differential execution.
6. Dispatcher recognition, cache fingerprinting, and continuation through supported direct targets.
7. Synthetic startup proof through `J`, `JAL`, and into a callee body before stopping at `JR`.

Explicitly out of scope:

- `JR` execution;
- `JALR` execution;
- branch-and-link families (`BLTZAL`, `BGEZAL`, likely variants);
- branch-likely behavior;
- exception behavior for a control transfer in a delay slot;
- guest-memory operations in dispatcher-managed control-transfer delay slots;
- generic call stack/function discovery;
- return-address prediction;
- syscall/HLE, graphics, audio, input, or game boot.

## Existing architecture and constraints

The decoder already classifies `J`, `JAL`, `JR`, and `JALR` as jump-class instructions and already computes direct targets for `J`/`JAL` using:

```text
base   = PC + 4
target = (base & 0xf0000000) | ((instruction_index & 0x03ffffff) << 2)
```

The control-flow analyzer already distinguishes direct jump, direct call, indirect jump, and indirect call block endings and fetches the architectural delay slot.

The IR currently has only `Fallthrough` and `BranchEqual64` block terminators. The x64 backend and reference block executor therefore have no semantic representation for a direct jump/call even though analysis can identify them.

The existing library dependency direction remains unchanged:

```text
b3r_recompiler
    ^
    |
b3r_runtime
    ^
    |
b3r_analysis
    ^
    |
b3r_recompiler_dispatcher_x64
```

No new dependency edge may be introduced by this milestone.

## Selected IR architecture

Use two explicit terminator kinds rather than a generic transfer with optional link state or a synthetic body write.

```cpp
enum class R5900IrTerminatorKind {
    Fallthrough = 0,
    BranchEqual64,
    DirectJump,
    DirectCall,
};
```

Retain the existing branch fields for `BranchEqual64` and add direct-transfer fields:

```cpp
struct R5900IrTerminator {
    std::uint32_t guest_pc{};
    std::uint32_t guest_raw{};
    R5900IrTerminatorKind kind{R5900IrTerminatorKind::Fallthrough};
    std::vector<R5900IrOperand> inputs{};

    // Conditional-branch state.
    std::uint32_t taken_pc{};
    std::uint32_t fallthrough_pc{};

    // Direct-transfer state.
    std::uint32_t target_pc{};
    std::uint32_t link_pc{};

    std::vector<R5900IrInstruction> delay_slot{};
};
```

`DirectJump` contract:

```text
inputs          = empty
taken_pc        = 0
fallthrough_pc  = 0
target_pc       = decoded J target
link_pc         = 0
delay_slot      = exactly one lowered instruction
```

`DirectCall` contract:

```text
inputs          = empty
taken_pc        = 0
fallthrough_pc  = 0
target_pc       = decoded JAL target
link_pc         = uint32(guest_pc + 8)
delay_slot      = exactly one lowered instruction
```

Keeping `DirectJump` and `DirectCall` distinct makes the architectural side effect visible in typed IR. It avoids hidden register writes in the body and prevents invalid optional-link combinations from spreading into validator and backend code.

## `JAL` link semantics

For a `JAL` at guest instruction PC `P`:

```text
link32 = uint32(P + 8)
GPR31.low64 = uint64(link32)
GPR31.high64 = preserved
execute architectural delay slot exactly once
next_pc = direct target
```

The link write occurs before the delay slot. Therefore the delay-slot instruction observes the new GPR31 value. If the delay slot itself writes GPR31, that later write wins according to normal instruction ordering.

This matches the established R5900 interpreter model used by PCSX2: `JAL` performs its link operation before branch/delay-slot execution, and the link helper writes only the low 64-bit half of the 128-bit GPR. In the interpreter execution loop, the PC has already advanced by four before the opcode handler, so the helper's next-PC-plus-four value corresponds to architectural instruction `PC + 8`.

The link value is zero-extended from the 32-bit guest PC into the low 64-bit GPR half. No sign extension is applied.

## IR validation

The block validator continues validating the body before the terminator.

All terminator PCs used by the active kind must be 4-byte aligned.

### `Fallthrough`

Existing rules remain unchanged and additionally require `target_pc == 0` and `link_pc == 0`.

### `BranchEqual64`

Existing rules remain unchanged and additionally require `target_pc == 0` and `link_pc == 0`.

### `DirectJump`

Valid only when:

- `inputs` is empty;
- `taken_pc == 0`;
- `fallthrough_pc == 0`;
- `target_pc` is 4-byte aligned;
- `link_pc == 0`;
- `delay_slot.size() == 1`;
- the delay-slot IR instruction independently validates.

A target address of zero is not rejected solely for being zero; mapping/executability is a dispatcher/runtime concern, not an IR-shape concern.

### `DirectCall`

Valid only when:

- `inputs` is empty;
- `taken_pc == 0`;
- `fallthrough_pc == 0`;
- `target_pc` is 4-byte aligned;
- `link_pc` is 4-byte aligned;
- `link_pc == uint32(guest_pc + 8)`;
- `delay_slot.size() == 1`;
- the delay-slot IR instruction independently validates.

Malformed transfer terminators fail before reference execution or native code publication.

## Reference executor

Block execution remains ordered as:

1. reset top-level memory-fault status;
2. validate the complete block;
3. execute body instructions in order;
4. execute terminator semantics;
5. return the next guest PC.

`DirectJump`:

```text
execute delay slot
if delay fails -> propagate failure
return target_pc
```

`DirectCall`:

```text
GPR31.low64 = uint64(link_pc)
preserve GPR31.high64
normalize GPR0
execute delay slot
if delay fails -> propagate failure with the link already committed
return target_pc
```

A body failure occurs before the terminator. Therefore a failing body `Store128` must not write the `JAL` link and must not execute the delay slot.

A delay-slot failure is ordered after the `JAL` link. The link remains committed because it architecturally precedes the delay-slot instruction. Dispatcher v0 will reject memory-bearing delay slots, but direct IR/reference tests may still exercise this ordering.

## Windows x86-64 backend

Add a direct-transfer compile path shared by `DirectJump` and `DirectCall` where practical.

High-level generated sequence:

```text
[optional helper-frame prologue]
normalize GPR0
emit body

if DirectCall:
    GPR31.low64 = zero_extend(link_pc)
    // do not touch GPR31.high64

normalize GPR0
emit delay slot exactly once
normalize GPR0
EAX = target_pc
[optional helper-frame epilogue]
RET
```

The existing generated calling convention remains:

```text
RCX = R5900IrExecutionState*
RDX = R5900IrExecutionContext*
EAX = next guest PC
```

For `DirectCall`, emitted code may materialize `link_pc` with a 32-bit immediate into `EAX` (which zeroes the upper 32 bits of RAX on x86-64) and then store RAX to GPR31 low64. It must not write the GPR31 high64 slot.

Helper-frame detection must include both the body and delay-slot sequence, exactly as for `BranchEqual64`, so a valid direct-transfer IR block containing `Store128` in its delay slot remains ABI-correct even though the dispatcher excludes that case in v0.

Existing Win64 requirements remain authoritative:

- 32-byte shadow space for nested helper calls;
- 16-byte stack alignment at each `CALL`;
- no reliance on volatile registers surviving helper calls;
- restoration of RSP on all return paths;
- RW -> RX publication and `FlushInstructionCache`.

## Dispatcher integration

The dispatcher recognizes a supported direct transfer only when the analyzer ending and decoded terminator agree:

```text
DirectJump block end + final decoded instruction J   -> supported J
DirectCall block end + final decoded instruction JAL -> supported JAL
```

`JR`, `JALR`, and all other jump/branch forms remain ordinary control-flow boundaries.

For a supported `J`/`JAL` block:

1. all preceding eligible instruction sites become the IR body;
2. the direct-transfer instruction is not lowered as a body instruction;
3. the analyzer-provided delay slot must exist and be readable;
4. the terminator and delay-slot guest words are included in the exact cache fingerprint/byte comparison;
5. the direct target comes from the decoder's existing `direct_target(guest_pc)` result;
6. `JAL` uses `link_pc = guest_pc + 8`;
7. the delay slot lowers to exactly one IR instruction;
8. `SQ` in a `J`/`JAL` delay slot is explicitly rejected with `LoweringFailure` in dispatcher v0;
9. the block compiles/caches and executes natively;
10. successful native `next_pc` becomes the next dispatcher PC.

A supported direct transfer must not set the dispatcher's post-body `boundary_reason`; otherwise the dispatcher would incorrectly execute the native transfer and then overwrite `next_pc` with the old terminator PC.

### Unsupported indirect boundary after a body prefix

The existing prefix behavior remains intentional. If analysis returns a block containing eligible body instructions followed by unsupported `JR`/`JALR`, the dispatcher may execute and count the eligible body prefix as a completed native block, then return `ControlFlow` at the unsupported transfer PC without executing that transfer or its delay slot.

This behavior is used by the synthetic E2E gate below.

## Cache behavior

Cache identity remains keyed by start PC plus the exact ordered guest-word vector and FNV fingerprint.

For `J`/`JAL` blocks, `guest_words` includes:

```text
all executable body words
terminator word
delay-slot word
```

Therefore mutation of the direct transfer or its delay slot invalidates exact cache reuse and recompiles on the next entry.

The target and link values are derived deterministically from those guest words and guest PC during block reconstruction; no additional cache key is required.

## Synthetic startup E2E fixture

The synthetic fixture extends the current mapped code region after the validated startup `SQ`.

Required layout:

```text
0x00100160  SQ    r0, 0(r2)
0x00100164  J     0x00100180
0x00100168  ADDIU r22, r0, 0x0033     ; J delay slot
0x0010016c..0x0010017c                 ; poison fallthrough region

0x00100180  JAL   0x001001a0
0x00100184  ADDIU r23, r31, 0         ; observes new link
0x00100188..0x0010019c                 ; poison call-fallthrough region

0x001001a0  ADDIU r24, r0, 0x0055     ; proves callee entry
0x001001a4  JR    r31                  ; next unsupported boundary
0x001001a8  NOP                        ; mapped JR delay slot, not executed
```

The poison regions use valid instructions that mutate dedicated registers if incorrectly executed. The E2E test asserts those registers remain unchanged, proving the dispatcher followed the `J` and `JAL` targets rather than linear fallthrough.

Expected result from startup entry:

```text
reason                 = ControlFlow
next_pc                = 0x001001a4
blocks_executed        = 5
instructions_executed  = 87

GPR22.low64             = 0x33
GPR23.low64             = 0x00100188
GPR24.low64             = 0x55
GPR31.low64             = 0x00100188
GPR31.high64            = preserved from its value immediately before JAL
SQ target 0x004e2680    = 16 zero bytes
poison registers        = unchanged
```

Accounting derivation:

- existing first two BEQ blocks: 81 instructions total through their delay slots;
- third block: `SQ + J + J delay` = 3 instructions, cumulative 84;
- fourth block: `JAL + JAL delay` = 2 instructions, cumulative 86;
- fifth block: callee `ADDIU` body prefix before unsupported `JR` = 1 instruction, cumulative 87;
- `JR` and its mapped delay slot are not executed or counted.

The third block remains one completed block even though it contains a body `SQ` followed by the `J` terminator.

## External legal ELF harness

Do not hard-code a post-`SQ` real-game `J`, `JAL`, `JR`, target, or block count without executing/inspecting the user's legal ELF through the native path.

The current external harness contract remains conservative:

- startup must execute at least through the validated `SQ` path;
- the `SQ` target must be mapped and fully zeroed;
- execution must advance beyond the `SQ` PC;
- later stop PC/block count remain dynamically reported.

If a future external run proves actual `J`/`JAL` execution, that evidence may upgrade status separately. Synthetic J/JAL CI coverage alone does not make the milestone `EXTERNALLY_VALIDATED`.

## TDD and verification gates

Implementation proceeds in explicit RED/GREEN slices.

### Gate 1 — IR shape and validation

Add focused tests for:

- valid `DirectJump`;
- valid `DirectCall`;
- target alignment;
- forbidden branch fields/inputs;
- `DirectJump` with nonzero link rejected;
- `DirectCall` with incorrect `link_pc` rejected;
- missing/multiple delay-slot instructions rejected;
- invalid delay-slot instruction propagated.

### Gate 2 — reference executor

Cover:

- `J` executes delay exactly once and returns target;
- `J` does not modify GPR31;
- `JAL` writes `PC+8` before delay execution;
- `JAL` zero-extends link into GPR31 low64;
- GPR31 high64 is preserved;
- delay slot can read the new link;
- delay slot writing GPR31 occurs after link and therefore wins;
- a body failure prevents link/delay execution.

### Gate 3 — Windows x64 differential

Reference and native results must match for:

- direct `J`;
- direct `JAL`;
- nonzero preexisting GPR31 high64;
- link-before-delay aliasing;
- representative body instructions before transfer;
- helper-frame correctness when direct IR includes a memory helper in body or delay-slot test coverage;
- next-PC equality and state equality.

### Gate 4 — dispatcher/cache

Cover:

- supported `J` follows target and counts terminator + delay;
- supported `JAL` follows target and writes link;
- cache hit on unchanged direct-transfer block;
- recompile when terminator word changes;
- recompile when delay-slot word changes;
- `SQ` in `J` delay rejected;
- `SQ` in `JAL` delay rejected;
- `JR` remains `ControlFlow` boundary;
- `JALR` remains `ControlFlow` boundary.

### Gate 5 — startup E2E

Update the synthetic startup fixture to the approved layout and require the exact 5-block / 87-instruction result, direct-target behavior, link-before-delay proof, poison fallthrough proof, and previously validated `SQ` memory result.

### Gate 6 — regression and CI

Run the complete Windows CI suite including:

- all CTest targets;
- dedicated direct-transfer tests;
- startup dispatcher test;
- frame-pacing telemetry;
- 120 Hz pacing probe smoke;
- analyzer/pacing package staging and validation.

No existing `BEQ`, `SQ`, cache, memory-fault, W^X, pacing, or packaging regression is acceptable.

## Completion criteria

The milestone is complete only when all of the following are true:

1. `DirectJump` and `DirectCall` are typed, validated IR terminators.
2. Reference executor implements their exact ordering semantics.
3. Windows x64 backend matches reference behavior.
4. `JAL` writes zero-extended `PC+8` to GPR31 low64 before the delay slot and preserves high64.
5. Dispatcher executes supported `J`/`JAL` and includes terminator/delay words in cache identity.
6. Dispatcher still rejects memory-bearing J/JAL delay slots in v0.
7. Synthetic startup reaches the callee body and stops before `JR` at `0x001001a4` with exactly 5 blocks / 87 instructions.
8. The startup `SQ` result at `0x004e2680` remains correct.
9. Full Windows CI is green.
10. Documentation reports the milestone as `CI_VALIDATED`; `EXTERNALLY_VALIDATED` is not claimed until a legal real ELF is actually executed and proves it.

## Deferred next milestone

The natural next control-flow milestone is `JR + JALR v0`, which will require explicit indirect-target snapshot semantics and aliasing rules (especially `JALR rd, rs` when `rd == rs`) before delay-slot execution.
