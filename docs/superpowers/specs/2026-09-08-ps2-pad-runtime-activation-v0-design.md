# PS2 PAD Runtime Activation v0 — Design

Date: 2026-09-08
Status: DESIGN_APPROVED_IN_CHAT / SPEC_REVIEW_PENDING
Base: `63f644a60d965eb32425dfa3a5b01aba6bc82712`
Branch: `design/ps2-pad-runtime-activation-v0`

## 1. Purpose

This milestone defines the first policy that may transform previously collected PAD binding evidence into a complete `Ps2PadHleBindings` object suitable for constructing `Ps2PadHleService`.

The milestone keeps three concepts independent:

```text
static confidence
      !=
runtime confirmation
      !=
HLE activation readiness
```

A function may remain statically `Candidate` or `Unresolved` while becoming activation-eligible if execution uniquely confirms one nonzero **evidence-backed** guest PC for that function.

Activation readiness is global and atomic: v0 materializes bindings only when all six PAD-facing functions are individually eligible and all six selected PCs are pairwise distinct.

## 2. Validated foundation

Previous milestones already provide:

- PAD binding discovery from exact ELF symbols and conservative static fingerprints;
- runtime observation of completed `JAL`/`JALR` calls after the delay slot;
- per-function ABI compatibility checks;
- `Unobserved`, `ObservedIncompatible`, `RuntimeConfirmed`, and `RuntimeAmbiguous` states;
- deterministic `PAD_RUNTIME_CONFIRMATION_V0` reporting;
- synthetic dispatcher → observer → PAD confirmation integration;
- `Ps2PadHleService`, which consumes `Ps2PadHleBindings`.

The current HLE has lifecycle dependencies:

```text
padInit
  ↓
padPortOpen
  ↓
padGetState / padRead
  ↓
padPortClose
  ↓
padEnd
```

`padRead` faults when initialization/open state is missing. Therefore unrestricted partial activation would create unsafe hybrid execution where dependent calls are intercepted while prerequisite calls still execute as guest code.

## 3. Goals

The milestone must:

1. define a pure activation-decision policy;
2. preserve static confidence unchanged;
3. require every activation PC to be backed by discovery evidence for the same function;
4. accept `Trusted`, `Candidate`, or `Unresolved` confidence when the same evidence-backed PC is uniquely runtime-confirmed;
5. reject `Unobserved`, `ObservedIncompatible`, and `RuntimeAmbiguous` functions;
6. reject malformed cross-input identity/confidence state;
7. reject missing or zero selected PCs;
8. require all six functions to be eligible;
9. require all six selected PCs to be pairwise distinct;
10. materialize `Ps2PadHleBindings` only in global `Ready` state;
11. provide deterministic diagnostics/reporting;
12. prove synthetic end-to-end activation with the existing `Ps2PadHleService` and dispatcher;
13. avoid `WinMain` wiring until a real guest-execution runtime exists.

## 4. Non-goals

This milestone does not:

- identify real Burnout 3 PAD addresses;
- claim real-game PAD confirmation;
- hot-swap guest-call services during `R5900BlockDispatcher::run()`;
- mutate dispatcher options after construction;
- add a production R5900 game loop to `WinMain`;
- add SIF/PADMAN/IOP/SIO2 emulation;
- weaken `Ps2PadHleService` lifecycle checks;
- add partial auto-activation;
- promote static confidence;
- weaken ELF/PT_LOAD validation;
- add proprietary game bytes/assets/hashes or hardcoded game addresses;
- claim boot, rendering, audio, menu, or gameplay support.

## 5. Activation policy model

Expected production unit:

```text
src/analysis/ps2_pad_activation.h
```

### 5.1 Public model

```cpp
enum class PadActivationEligibility : std::uint8_t {
    Rejected,
    Eligible,
};

enum class PadActivationReadiness : std::uint8_t {
    NotReady,
    Ready,
};

enum class PadActivationReason : std::uint8_t {
    EligibleRuntimeConfirmed,
    Unobserved,
    ObservedIncompatible,
    RuntimeAmbiguous,
    MissingGuestPc,
    ZeroGuestPc,
    InputMismatch,
    PcNotInDiscoveryEvidence,
};

struct PadActivationFunctionDecision {
    PadBindingFunction function{};
    PadBindingConfidence static_confidence{PadBindingConfidence::Unresolved};
    PadRuntimeConfirmationStatus runtime_status{
        PadRuntimeConfirmationStatus::Unobserved};
    PadActivationEligibility eligibility{PadActivationEligibility::Rejected};
    PadActivationReason reason{PadActivationReason::Unobserved};
    std::optional<std::uint32_t> guest_pc{};
};

struct Ps2PadActivationDecision {
    std::array<PadActivationFunctionDecision, 6> functions{};
    PadActivationReadiness readiness{PadActivationReadiness::NotReady};
    std::optional<runtime::Ps2PadHleBindings> bindings{};
    std::vector<std::string> diagnostics{};
};
```

Primary API:

```cpp
[[nodiscard]] Ps2PadActivationDecision
make_ps2_pad_activation_decision(
    const PadBindingDiscoveryResult& discovery,
    const PadRuntimeConfirmationResult& runtime);
```

The function is pure with respect to its inputs and runtime state: it does not mutate discovery/runtime, guest RAM, dispatcher state, or HLE state.

### 5.2 Cross-input provenance validation

For canonical index `i`:

```text
expected = static_cast<PadBindingFunction>(i)
```

Before evaluating runtime status, the decision maker validates:

1. `discovery.resolutions[i].function == expected`;
2. `runtime.functions[i].function == expected`;
3. `runtime.functions[i].static_confidence == discovery.resolutions[i].confidence`.

Any failure is:

```text
eligibility = Rejected
reason = InputMismatch
guest_pc = none
```

and emits a deterministic diagnostic naming the canonical function and mismatch category.

The decision maker must not silently correlate unrelated entries merely because array positions match.

### 5.3 Evidence-backed PC requirement

A runtime-confirmed PC is eligible only if the exact PC occurs in `discovery.resolutions[i].evidence` for the same canonical function.

Evidence kind and score do not matter at this stage; existence of same-function evidence does.

Therefore:

```text
RuntimeConfirmed(nonzero PC)
AND same PC exists in same-function discovery evidence
    -> eligible candidate for activation

RuntimeConfirmed(nonzero PC)
BUT PC absent from same-function discovery evidence
    -> Rejected / PcNotInDiscoveryEvidence
```

This check prevents a manually malformed or externally fabricated runtime-result structure from authorizing an arbitrary PC.

### 5.4 Per-function decision table

After cross-input validation:

```text
runtime status              selected PC              result
--------------------------------------------------------------------------------
Unobserved                  any                      Rejected / Unobserved
ObservedIncompatible        any                      Rejected / ObservedIncompatible
RuntimeAmbiguous            any                      Rejected / RuntimeAmbiguous
RuntimeConfirmed            absent                   Rejected / MissingGuestPc
RuntimeConfirmed            0x00000000               Rejected / ZeroGuestPc
RuntimeConfirmed            nonzero, no evidence     Rejected / PcNotInDiscoveryEvidence
RuntimeConfirmed            nonzero, evidence-backed Eligible / EligibleRuntimeConfirmed
```

Rejected activation decisions always clear `guest_pc`, even if a malformed source object contains an optional PC.

### 5.5 Static confidence

Static confidence is copied from `discovery.resolutions[i].confidence` only after the cross-input consistency check succeeds.

All are individually eligible when the same evidence-backed PC is uniquely runtime-confirmed:

```text
Trusted    + RuntimeConfirmed -> Eligible
Candidate  + RuntimeConfirmed -> Eligible
Unresolved + RuntimeConfirmed -> Eligible
```

The `Unresolved` case is intentional: static evidence may remain ambiguous while runtime execution uniquely identifies one evidence-backed PC.

Activation never modifies `PadBindingConfidence`.

## 6. Global readiness

### 6.1 Atomic requirement

Global readiness is `Ready` only when:

```text
eligible_count == 6
AND every eligible PC != 0
AND all six PCs are pairwise distinct
```

Otherwise readiness is `NotReady`.

### 6.2 No partial bindings

`bindings` is all-or-nothing:

```text
NotReady -> bindings = nullopt
Ready    -> bindings contains all six nonzero, distinct PCs
```

A structure with only a subset populated is forbidden in v0.

### 6.3 Duplicate activation PCs

Discovery/runtime analysis may temporarily associate the same numerical PC with more than one function. Activation may not.

If two or more individually eligible functions select the same PC:

```text
individual eligibility remains Eligible
readiness = NotReady
bindings = nullopt
```

Emit one deterministic diagnostic per duplicated PC group, formatted with lowercase eight-digit hex, e.g.:

```text
activation guest PC 0x00102000 is selected by multiple PAD functions
```

No ordering of checks inside `Ps2PadHleService` is used to break the tie.

### 6.4 Incomplete set diagnostic

When fewer than six functions are eligible, add exactly:

```text
incomplete_activation_set
```

Diagnostics are sorted and deduplicated before returning.

## 7. Two-phase activation architecture

### 7.1 Phase A — observe and decide

```text
PadBindingDiscoveryResult
        ↓
Ps2PadRuntimeConfirmation
        ↓
R5900BlockDispatcher(call_observer = confirmation)
        ↓
completed JAL/JALR observations
        ↓
PadRuntimeConfirmationResult
        ↓
make_ps2_pad_activation_decision(...)
```

The decision is an immutable snapshot from the caller's perspective. New observations require obtaining a new runtime result and computing a new decision.

A previously `Ready` decision is not automatically preserved if later evidence makes a newly computed runtime result ambiguous.

### 7.2 Phase B — construct active HLE service

Only `Ready` produces bindings:

```cpp
const auto decision =
    make_ps2_pad_activation_decision(discovery, runtime_result);

if (decision.bindings.has_value()) {
    runtime::Ps2PadHleService pad_service(*decision.bindings);

    recompiler::R5900BlockDispatcherOptions options{};
    options.guest_calls = &pad_service;

    recompiler::R5900BlockDispatcher dispatcher(memory, options);
    // Begin a new dispatch phase.
}
```

### 7.3 No hot-swap

v0 does not add:

- `set_guest_call_service()`;
- mutable guest-call swapping;
- activation-triggered cache invalidation;
- observer-to-HLE transition inside one `run()` call.

The boundary is explicit:

```text
finish observation phase
        ↓
freeze decision
        ↓
construct Ps2PadHleService
        ↓
construct dispatcher with guest_calls = &service
        ↓
start activation phase
```

## 8. Deterministic activation report

Expected formatter:

```text
src/analysis/ps2_pad_activation_report.h
```

API:

```cpp
[[nodiscard]] std::string
format_ps2_pad_activation_decision(
    const Ps2PadActivationDecision& decision);
```

### 8.1 Canonical strings

Function order:

```text
padInit
padPortOpen
padGetState
padRead
padPortClose
padEnd
```

Eligibility:

```text
eligible
rejected
```

Readiness:

```text
ready
not_ready
```

Reasons:

```text
eligible_runtime_confirmed
unobserved
observed_incompatible
runtime_ambiguous
missing_guest_pc
zero_guest_pc
input_mismatch
pc_not_in_discovery_evidence
```

### 8.2 Format

Ready example:

```text
PAD_ACTIVATION_V0 readiness=ready eligible=6 required=6
PAD_ACTIVATION function=padInit static_confidence=trusted runtime_status=runtime_confirmed eligibility=eligible pc=0x00101000 reason=eligible_runtime_confirmed
PAD_ACTIVATION function=padPortOpen static_confidence=candidate runtime_status=runtime_confirmed eligibility=eligible pc=0x00102000 reason=eligible_runtime_confirmed
PAD_ACTIVATION function=padGetState static_confidence=unresolved runtime_status=runtime_confirmed eligibility=eligible pc=0x00103000 reason=eligible_runtime_confirmed
PAD_ACTIVATION function=padRead static_confidence=candidate runtime_status=runtime_confirmed eligibility=eligible pc=0x00104000 reason=eligible_runtime_confirmed
PAD_ACTIVATION function=padPortClose static_confidence=trusted runtime_status=runtime_confirmed eligibility=eligible pc=0x00105000 reason=eligible_runtime_confirmed
PAD_ACTIVATION function=padEnd static_confidence=trusted runtime_status=runtime_confirmed eligibility=eligible pc=0x00106000 reason=eligible_runtime_confirmed
PAD_ACTIVATION_BINDINGS padInit=0x00101000 padPortOpen=0x00102000 padGetState=0x00103000 padRead=0x00104000 padPortClose=0x00105000 padEnd=0x00106000
PAD_ACTIVATION_END
```

Blocked example:

```text
PAD_ACTIVATION_V0 readiness=not_ready eligible=5 required=6
...
PAD_ACTIVATION function=padRead static_confidence=candidate runtime_status=runtime_ambiguous eligibility=rejected pc=none reason=runtime_ambiguous
PAD_ACTIVATION_DIAGNOSTIC incomplete_activation_set
PAD_ACTIVATION_END
```

### 8.3 Formatting rules

- exactly six function lines in canonical order;
- lowercase `0x` plus exactly eight hex digits;
- rejected functions always render `pc=none`, even if a manually malformed decision object contains `guest_pc`;
- bindings line appears only when readiness is `Ready` **and** complete bindings are present;
- if readiness/bindings are internally inconsistent, formatter omits the bindings line rather than fabricating fields;
- bindings fields are canonical-order;
- diagnostics follow function/bindings lines and precede `PAD_ACTIVATION_END`;
- diagnostics are sorted/deduplicated during formatting defensively as well as by the producer;
- repeated render is byte-identical;
- output contains no score, arguments, register dumps, RAM/code bytes, or proprietary data.

## 9. Synthetic activation integration

The final behavioral gate uses only synthetic fixtures and the existing real HLE implementation.

### 9.1 Synthetic inputs

Use six distinct synthetic guest PCs. None may come from Burnout 3.

Construct discovery evidence and runtime results such that each function is uniquely confirmed at one of those evidence-backed PCs.

Static confidence deliberately mixes `Trusted`, `Candidate`, and `Unresolved`.

### 9.2 Materialization proof

The decision must produce:

```text
readiness = Ready
bindings.has_value() = true
bindings fields == exact six runtime-confirmed evidence-backed PCs
```

### 9.3 HLE lifecycle proof

Construct only from the decision:

```cpp
runtime::Ps2PadHleService service(*decision.bindings);
```

Then use a dispatcher configured with:

```cpp
options.guest_calls = &service;
```

The synthetic lifecycle must prove:

1. `padInit(0)` intercepted and successful;
2. `padPortOpen(0,0,aligned-256-byte-area)` intercepted and opens state;
3. `padGetState(0,0)` returns stable for a connected report;
4. `padRead(0,0,destination)` writes the exact expected 32-byte PS2 report and returns 32;
5. `padPortClose(0,0)` closes state;
6. `padEnd()` clears initialized/open state;
7. an unrelated synthetic guest PC is not handled by the PAD service.

The integration must use `decision.bindings` directly. It must not independently create a second hardcoded binding object for the activation phase.

### 9.4 No production runtime wiring

`src/platform/windows/win_main.cpp` remains unchanged. It currently has no R5900 guest-execution loop, so activation there would be premature architecture.

## 10. Error handling

Ordinary policy rejection does not throw.

It returns `NotReady` plus structured reasons/diagnostics.

Global diagnostics include:

- `incomplete_activation_set`;
- canonical discovery/runtime function identity mismatch;
- static-confidence mismatch between runtime result and discovery;
- runtime-selected PC missing from same-function discovery evidence;
- duplicate selected-PC groups.

The decision maker never invokes HLE, never writes guest RAM, and never modifies dispatcher state.

## 11. TDD gates

### Task 1 — Activation policy

RED defines the missing production API.

Required tests:

- six evidence-backed `RuntimeConfirmed` functions with six distinct nonzero PCs -> `Ready`;
- bindings equal the six selected PCs exactly;
- five eligible -> `NotReady`, no bindings;
- `Unobserved` rejected;
- `ObservedIncompatible` rejected;
- `RuntimeAmbiguous` rejected;
- confirmed with missing PC rejected;
- confirmed with zero PC rejected;
- confirmed PC absent from discovery evidence rejected;
- `Trusted + RuntimeConfirmed` eligible;
- `Candidate + RuntimeConfirmed` eligible;
- `Unresolved + RuntimeConfirmed` eligible;
- discovery function identity mismatch rejected/diagnosed;
- runtime function identity mismatch rejected/diagnosed;
- runtime/discovery confidence mismatch rejected/diagnosed;
- rejected output clears inconsistent source PC;
- duplicate PC across eligible functions -> global `NotReady`, no bindings;
- duplicate diagnostics deterministic;
- fewer than six eligible -> `incomplete_activation_set`;
- input discovery/runtime objects remain unchanged;
- identical inputs produce structurally identical decisions.

### Task 2 — Deterministic activation report

RED defines the formatter contract.

Required tests:

- header/end marker;
- canonical function order;
- readiness/eligible/required counts;
- canonical confidence/status/eligibility/reason strings;
- lowercase eight-digit PC;
- rejected decision renders `pc=none` despite malformed optional PC;
- bindings line only for internally consistent `Ready + bindings`;
- bindings line exactly reflects materialized fields;
- diagnostics sorted/deduplicated;
- duplicate render byte-identical;
- no score/args/RAM/code dump content.

### Task 3 — Synthetic HLE activation integration

Required coverage:

- six distinct evidence-backed confirmations -> `Ready`;
- mixed static confidence does not block readiness;
- construct `Ps2PadHleService` from `decision.bindings` only;
- lifecycle init/open/get-state/read/close/end;
- `padRead` exact report bytes;
- `$v0` results match existing HLE contract;
- unknown PC remains not handled by PAD service;
- no dispatcher guest-call hot-swap API added;
- `WinMain` unchanged;
- no proprietary data or Burnout-specific PC in fixtures.

### Task 4 — Documentation and exact-head CI

Create:

```text
docs/validation/2026-09-08-ps2-pad-runtime-activation-v0.md
```

Update:

```text
docs/PROGRESS.md
```

Validation sequence:

1. implementation-head Windows CI: Configure, Build, all CTest, frame pacing telemetry, pacing probe, analyzer package, pacing package;
2. documentation commit with `PENDING_FINAL_EXACT_HEAD_CI`;
3. full Windows CI on exact documentation SHA;
4. if green, status-only update of the same two documents to `CI_VALIDATED`, recording that run;
5. full Windows CI on exact final status SHA;
6. only then claim milestone `CI_VALIDATED`.

## 12. Integrity audit

Compare milestone head against base `63f644a60d965eb32425dfa3a5b01aba6bc82712` and confirm:

- no Burnout-specific guest PCs;
- no proprietary bytes/assets/hashes;
- no weakened `Ps2PadHleService` lifecycle checks;
- no partial activation;
- no static confidence promotion;
- no activation PC that lacks discovery evidence;
- duplicate PCs never become bindings;
- no dispatcher hot-swap API introduced solely for activation;
- `WinMain` unchanged;
- ELF/PT_LOAD validation unchanged;
- no PCSX2 runtime dependency.

## 13. Completion criteria

Mechanism may be `CI_VALIDATED` when synthetic tests prove:

```text
six distinct evidence-backed RuntimeConfirmed PAD PCs
        ↓
pure activation decision
        ↓
Ready + complete Ps2PadHleBindings
        ↓
Ps2PadHleService constructed from those exact bindings
        ↓
synthetic full PAD lifecycle intercepted successfully
```

Real Burnout 3 activation remains `PENDING_EXTERNAL_VALIDATION` until a complete lawful user-supplied ELF executes and uniquely runtime-confirms all six evidence-backed functions under this policy.

## 14. Explicit non-claims

Completion does not mean:

- Burnout 3 boots;
- Burnout 3 reaches menu/gameplay;
- Burnout 3 consumes keyboard/XInput through this PAD HLE path;
- graphics/GS/VU/IOP/SPU2/audio are complete;
- any synthetic fixture address is a real game address.
