# PS2 PAD Runtime Activation v0 — Design

Date: 2026-09-08
Status: DESIGN_APPROVED_IN_CHAT / SPEC_REVIEW_PENDING
Base: `63f644a60d965eb32425dfa3a5b01aba6bc82712`
Branch: `design/ps2-pad-runtime-activation-v0`

## 1. Purpose

This milestone defines the first policy that may transform previously collected PAD binding evidence into a complete `Ps2PadHleBindings` object suitable for constructing `Ps2PadHleService`.

The milestone deliberately separates three concepts:

```text
static confidence
      !=
runtime confirmation
      !=
HLE activation readiness
```

A binding can remain statically `Candidate` or even `Unresolved` while becoming activation-eligible if runtime execution uniquely confirms one nonzero guest PC for that function.

Activation readiness is global and atomic: v0 materializes bindings only when all six PAD-facing functions are individually eligible and their selected guest PCs are pairwise distinct.

## 2. Existing validated foundation

The previous milestones already provide:

- analysis-only PAD binding discovery;
- exact ELF-symbol evidence and static fingerprint evidence;
- runtime observation of completed `JAL`/`JALR` calls;
- post-delay-slot `$a0..$a3` snapshots;
- per-function ABI compatibility checks;
- `Unobserved`, `ObservedIncompatible`, `RuntimeConfirmed`, and `RuntimeAmbiguous` states;
- deterministic `PAD_RUNTIME_CONFIRMATION_V0` reporting;
- synthetic dispatcher → observer → PAD confirmation integration;
- an existing `Ps2PadHleService` that accepts a complete `Ps2PadHleBindings` structure.

`Ps2PadHleService` currently relies on lifecycle state:

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

`padRead` faults when `padInit` has not completed or port 0/slot 0 is not open. Therefore unrestricted partial activation would create unsafe hybrid execution where some libpad calls execute as guest code while dependent calls are intercepted by HLE.

## 3. Goals

The milestone must:

1. define a pure activation-decision policy;
2. keep static confidence unchanged;
3. accept a uniquely runtime-confirmed guest PC regardless of whether the static confidence is `Trusted`, `Candidate`, or `Unresolved`;
4. reject `Unobserved`, `ObservedIncompatible`, and `RuntimeAmbiguous` functions;
5. reject missing or zero selected PCs;
6. require all six functions to be eligible before activation readiness becomes `Ready`;
7. require all six selected PCs to be pairwise distinct;
8. materialize `Ps2PadHleBindings` only in the globally `Ready` state;
9. provide deterministic diagnostics and a deterministic activation report;
10. prove synthetic end-to-end activation through the existing real `Ps2PadHleService` and `R5900BlockDispatcher`;
11. avoid `WinMain` wiring until the project has a real guest-execution runtime path.

## 4. Non-goals

This milestone does not:

- identify real Burnout 3 PAD addresses;
- claim the real game has confirmed six PAD entry points;
- hot-swap guest-call services during a dispatcher run;
- mutate `R5900BlockDispatcher` options after construction;
- add a new production game-execution loop to `WinMain`;
- add SIF/PADMAN/IOP/SIO2 emulation;
- change `Ps2PadHleService` lifecycle semantics;
- add partial auto-activation;
- promote `Candidate` or `Unresolved` static confidence to `Trusted`;
- weaken ELF/PT_LOAD validation;
- add proprietary Burnout 3 code, bytes, hashes, assets, or hardcoded game addresses;
- claim game boot, rendering, audio, menu, or gameplay support.

## 5. Activation policy model

### 5.1 Function eligibility

Add a pure analysis/runtime-facing activation unit, expected at:

```text
src/analysis/ps2_pad_activation.h
```

The public model is:

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

The primary API is:

```cpp
[[nodiscard]] Ps2PadActivationDecision
make_ps2_pad_activation_decision(
    const PadBindingDiscoveryResult& discovery,
    const PadRuntimeConfirmationResult& runtime);
```

### 5.2 Per-function decision table

For each canonical PAD function:

```text
runtime status              guest PC          eligibility   reason
--------------------------------------------------------------------------
Unobserved                  any               Rejected      Unobserved
ObservedIncompatible        any               Rejected      ObservedIncompatible
RuntimeAmbiguous            any               Rejected      RuntimeAmbiguous
RuntimeConfirmed            absent            Rejected      MissingGuestPc
RuntimeConfirmed            0x00000000        Rejected      ZeroGuestPc
RuntimeConfirmed            nonzero           Eligible      EligibleRuntimeConfirmed
```

The selected PC in an `Eligible` decision is the runtime-confirmed PC.

For a rejected function, `guest_pc` in the activation decision is always cleared, even if the source runtime structure is internally inconsistent and contains an optional PC.

### 5.3 Static confidence handling

Static confidence is carried through for diagnostics only.

These are all individually eligible when runtime status is uniquely confirmed with one nonzero PC:

```text
Trusted   + RuntimeConfirmed -> Eligible
Candidate + RuntimeConfirmed -> Eligible
Unresolved + RuntimeConfirmed -> Eligible
```

The last case is intentional. Runtime confirmation may uniquely resolve execution behavior that remained ambiguous during static discovery.

Activation must never mutate `PadBindingConfidence`.

### 5.4 Canonical identity validation

The decision function validates canonical function identity rather than blindly trusting array position.

For index `i`:

```text
expected function = static_cast<PadBindingFunction>(i)
```

If discovery/runtime inputs contain a mismatched function identity at that canonical slot, the function is rejected and a deterministic diagnostic is produced.

The implementation must not silently correlate unrelated records solely because their array indexes match.

## 6. Global readiness

### 6.1 Atomic requirement

Global activation readiness is `Ready` only when:

```text
eligible_count == 6
AND every eligible PC is nonzero
AND all six PCs are pairwise distinct
```

Otherwise readiness is `NotReady`.

### 6.2 No partial bindings

`bindings` is all-or-nothing:

```text
NotReady -> bindings = nullopt
Ready    -> bindings contains all six nonzero PCs
```

This is forbidden in v0:

```cpp
Ps2PadHleBindings{
    .pad_init = some_pc,
    .pad_port_open = some_pc,
    .pad_get_state = 0,
    .pad_read = some_pc,
    .pad_port_close = 0,
    .pad_end = 0,
};
```

No partial activation is emitted even when the individually eligible subset is large.

### 6.3 Duplicate activation PCs

Discovery/runtime analysis may legitimately contain the same numerical guest PC under different function identities while evidence remains unresolved.

Activation may not.

If two or more eligible functions select the same guest PC:

```text
individual decisions remain Eligible
readiness = NotReady
bindings = nullopt
```

Add one deterministic diagnostic for each duplicated PC group, for example:

```text
activation guest PC 0x00102000 is selected by multiple PAD functions
```

No ordering of `if` statements inside `Ps2PadHleService` is used as a tie-breaker.

### 6.4 Incomplete set diagnostic

When fewer than six functions are eligible, add:

```text
incomplete_activation_set
```

Diagnostics are sorted and deduplicated before returning the decision.

## 7. Two-phase activation architecture

### 7.1 Phase A — observe and decide

The observation phase uses the validated runtime-confirmation path:

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

The activation decision is a snapshot. It does not subscribe to future observations and does not mutate when new runtime evidence arrives.

To account for new observations, the caller explicitly requests a new runtime result and computes a new activation decision.

### 7.2 Phase B — construct active HLE service

Only a `Ready` decision may construct an active service:

```cpp
const auto decision =
    make_ps2_pad_activation_decision(discovery, runtime_result);

if (decision.bindings.has_value()) {
    runtime::Ps2PadHleService pad_service(*decision.bindings);

    recompiler::R5900BlockDispatcherOptions options{};
    options.guest_calls = &pad_service;

    recompiler::R5900BlockDispatcher dispatcher(memory, options);
    // Start a new dispatch phase.
}
```

### 7.3 No hot-swap

`R5900BlockDispatcherOptions::guest_calls` is configured at dispatcher construction.

v0 does not add:

- `set_guest_call_service()`;
- mutable guest-call swapping;
- cache invalidation triggered by activation;
- observer-to-HLE transitions inside a single `run()` call.

The phase boundary is explicit:

```text
finish observation phase
        ↓
freeze decision
        ↓
construct Ps2PadHleService
        ↓
construct a dispatcher configured with that service
        ↓
start activation phase
```

## 8. Deterministic activation report

Add a formatter, expected at:

```text
src/analysis/ps2_pad_activation_report.h
```

API:

```cpp
[[nodiscard]] std::string
format_ps2_pad_activation_decision(
    const Ps2PadActivationDecision& decision);
```

### 8.1 Canonical names

Use canonical function names:

```text
padInit
padPortOpen
padGetState
padRead
padPortClose
padEnd
```

Eligibility names:

```text
eligible
rejected
```

Readiness names:

```text
ready
not_ready
```

Reason names:

```text
eligible_runtime_confirmed
unobserved
observed_incompatible
runtime_ambiguous
missing_guest_pc
zero_guest_pc
```

### 8.2 Report format

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

- exactly six `PAD_ACTIVATION function=` lines in canonical enum order;
- lowercase hexadecimal guest PCs with exactly eight digits after `0x`;
- rejected functions always print `pc=none`;
- `PAD_ACTIVATION_BINDINGS` appears only when readiness is `Ready` and bindings are present;
- bindings line fields appear in canonical function order;
- diagnostics appear after the bindings/function lines and before `PAD_ACTIVATION_END`;
- diagnostics are sorted and deduplicated;
- repeated renders of the same decision are byte-identical;
- no static score, arguments, register dumps, RAM bytes, code bytes, or proprietary data are included.

## 9. Synthetic activation integration

The milestone ends with a synthetic end-to-end test using the existing real `Ps2PadHleService`.

### 9.1 Synthetic binding set

Use six distinct synthetic guest PCs, for example from one executable synthetic fixture region. They must not be copied from Burnout 3.

Construct discovery/runtime result inputs such that:

```text
padInit       -> RuntimeConfirmed PC A
padPortOpen   -> RuntimeConfirmed PC B
padGetState   -> RuntimeConfirmed PC C
padRead       -> RuntimeConfirmed PC D
padPortClose  -> RuntimeConfirmed PC E
padEnd        -> RuntimeConfirmed PC F
```

Static confidence should intentionally mix `Trusted`, `Candidate`, and `Unresolved` to prove that activation eligibility depends on runtime confirmation, not confidence promotion.

### 9.2 Materialization proof

The decision must produce:

```text
readiness = Ready
bindings.has_value() = true
bindings fields == A/B/C/D/E/F exactly
```

### 9.3 HLE lifecycle proof

Construct:

```cpp
runtime::Ps2PadHleService service(*decision.bindings);
```

Then execute synthetic guest calls through a dispatcher configured with:

```cpp
options.guest_calls = &service;
```

The lifecycle test must prove at minimum:

1. `padInit(0)` is intercepted and returns success;
2. `padPortOpen(0, 0, aligned_256_byte_area)` is intercepted and opens the port;
3. `padGetState(0, 0)` returns stable when the report is connected;
4. `padRead(0, 0, destination)` writes exactly the expected 32-byte PS2-style report and returns 32;
5. `padPortClose(0, 0)` closes the port;
6. `padEnd()` clears initialized/open state;
7. a synthetic guest PC not present in the activation bindings is not handled by `Ps2PadHleService`.

The service uses the exact six PCs materialized by the activation decision; tests must not independently construct a second hardcoded binding structure for the activation phase.

### 9.4 No production runtime wiring

The integration test is the only activation consumer required for v0.

`src/platform/windows/win_main.cpp` remains unchanged because it currently runs only the bootstrap window/input/frame-pacing loop and does not execute the R5900 guest runtime.

## 10. Error handling and diagnostics

Activation policy never throws for ordinary rejection conditions.

Ordinary policy failures return `NotReady` with structured per-function reasons and deterministic diagnostics.

Global diagnostics include:

- `incomplete_activation_set` when fewer than six functions are eligible;
- canonical-function identity mismatch diagnostics;
- duplicate selected-PC diagnostics.

The decision maker does not call `Ps2PadHleService`, mutate guest RAM, or modify dispatcher state.

## 11. Test strategy and TDD gates

### Task 1 — Activation policy

RED must first define the missing production API.

Required tests:

- all six `RuntimeConfirmed` with six distinct nonzero PCs -> `Ready`;
- bindings are exactly the six selected PCs;
- five eligible functions -> `NotReady` and `bindings == nullopt`;
- `Unobserved` -> rejected with `Unobserved` reason;
- `ObservedIncompatible` -> rejected with matching reason;
- `RuntimeAmbiguous` -> rejected with matching reason;
- `RuntimeConfirmed` with missing PC -> rejected;
- `RuntimeConfirmed` with zero PC -> rejected;
- `Trusted + RuntimeConfirmed` -> eligible;
- `Candidate + RuntimeConfirmed` -> eligible;
- `Unresolved + RuntimeConfirmed` -> eligible;
- rejected activation decision clears inconsistent source guest PC;
- duplicate activation PC across two eligible functions -> global `NotReady`, no bindings;
- duplicate-PC diagnostics deterministic;
- fewer than six eligible -> `incomplete_activation_set`;
- canonical-function identity mismatch is rejected/diagnosed;
- input discovery/runtime objects remain unchanged after decision creation;
- repeated decision creation from identical inputs is structurally identical.

GREEN must be the smallest production implementation satisfying these tests.

### Task 2 — Deterministic activation report

RED defines the missing formatter/report contract.

Required tests:

- header and end marker;
- canonical six-function order;
- readiness/eligible/required counts;
- canonical confidence/runtime-status/eligibility/reason strings;
- lowercase eight-digit PC format;
- rejected function prints `pc=none` even with inconsistent source optional PC;
- bindings line appears only for `Ready` decision;
- bindings line exactly reflects materialized bindings;
- diagnostics sorted/deduplicated;
- duplicate render byte-identical;
- report excludes static score/arguments/RAM/code dumps.

### Task 3 — Synthetic HLE activation integration

This task characterizes existing production components plus the new activation policy.

Required coverage:

- six distinct synthetic confirmed PCs -> `Ready`;
- mixed static confidence values do not block readiness;
- use `decision.bindings` directly to construct `Ps2PadHleService`;
- lifecycle: init/open/get-state/read/close/end;
- `padRead` writes the exact expected PS2 report bytes;
- `$v0` results match existing HLE contract;
- unknown/non-binding PC is not handled by the PAD service;
- no dispatcher guest-call hot-swap API is introduced;
- `WinMain` unchanged;
- no proprietary data or Burnout-specific guest PC enters repository fixtures.

### Task 4 — Documentation and exact-head CI

Create:

```text
docs/validation/2026-09-08-ps2-pad-runtime-activation-v0.md
```

Update:

```text
docs/PROGRESS.md
```

Record RED/GREEN workflow evidence.

Validation sequence:

1. implementation-head Windows CI must pass Configure, Build, all CTest tests, frame pacing telemetry, pacing probe, analyzer package, and pacing package;
2. commit documentation with milestone status `PENDING_FINAL_EXACT_HEAD_CI`;
3. run full Windows CI on that exact documentation SHA;
4. only after that run passes, update the two status documents to `CI_VALIDATED` and record the documentation run;
5. run full Windows CI again on the exact final status SHA;
6. only after that final exact-head run passes may the milestone be claimed `CI_VALIDATED`.

## 12. Integrity audit

Before completion, compare the milestone head against base `63f644a60d965eb32425dfa3a5b01aba6bc82712` and confirm:

- no Burnout-specific guest PCs were added;
- no proprietary game bytes/assets/hashes were added;
- `Ps2PadHleService` lifecycle behavior was not weakened to accommodate activation;
- no automatic partial binding activation exists;
- no static confidence is promoted by activation;
- duplicate PCs do not become active bindings;
- `R5900BlockDispatcher` does not gain hot-swap behavior merely for activation;
- `WinMain` remains free of premature guest-runtime activation wiring;
- ELF/PT_LOAD validation remains unchanged;
- no PCSX2 runtime dependency is added.

## 13. Completion criteria

The mechanism may be marked `CI_VALIDATED` when synthetic tests prove:

```text
six runtime-confirmed distinct PAD PCs
        ↓
pure activation decision
        ↓
Ready + complete Ps2PadHleBindings
        ↓
Ps2PadHleService constructed from those exact bindings
        ↓
synthetic PAD lifecycle intercepted successfully
```

This milestone still does not validate any real Burnout 3 PAD address.

Real-game activation remains `PENDING_EXTERNAL_VALIDATION` until a complete lawful user-supplied Burnout 3 ELF executes and uniquely runtime-confirms all six evidence-backed functions under this policy.

## 14. Explicit non-claims

Completion of this milestone does not mean:

- Burnout 3 boots;
- Burnout 3 reaches menu/gameplay;
- Burnout 3 is consuming keyboard/XInput through PAD HLE;
- graphics, GS, VU, IOP, SPU2, or game audio are implemented;
- any synthetic fixture address is a real game address.
