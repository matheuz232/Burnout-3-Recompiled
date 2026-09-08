# PS2 PAD Runtime Activation v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert evidence-backed, uniquely runtime-confirmed PAD entry points into an atomic six-function `Ps2PadHleBindings` decision and prove that those exact bindings can drive the existing PAD HLE lifecycle through the R5900 dispatcher.

**Architecture:** Add a pure activation policy layer between `PadRuntimeConfirmationResult` and `Ps2PadHleBindings`, then a deterministic `PAD_ACTIVATION_V0` formatter. Activation is two-phase and snapshot-based: observe/confirm first, compute a decision, then construct a new `Ps2PadHleService` and dispatcher only when the decision is globally `Ready`. No hot-swap, no partial bindings, no `WinMain` wiring.

**Tech Stack:** C++20, CMake 3.25+, Visual Studio 2022 x64, existing `b3r_analysis`, `b3r_runtime`, `b3r_recompiler_dispatcher_x64`, Windows GitHub Actions CI.

**Spec:** `docs/superpowers/specs/2026-09-08-ps2-pad-runtime-activation-v0-design.md`

## Global Constraints

- Base implementation SHA: `63f644a60d965eb32425dfa3a5b01aba6bc82712`.
- Work on a new branch `feature/ps2-pad-runtime-activation-v0` created from the plan head.
- Use TDD RED→GREEN for Tasks 1 and 2; Task 3 is end-to-end characterization of already-implemented units plus the Task 1 policy.
- Static confidence, runtime confirmation, and activation readiness remain separate concepts.
- `RuntimeConfirmed` is eligible only if the selected nonzero guest PC is present in discovery evidence for the same canonical function.
- `Trusted`, `Candidate`, and `Unresolved` static confidence are all allowed when the runtime-confirmed PC is evidence-backed.
- Any canonical-function identity mismatch between discovery/runtime slot and expected enum value is rejected and diagnosed.
- Global activation is atomic: 6/6 eligible, all nonzero, all pairwise-distinct, or no bindings at all.
- Duplicate PCs across functions keep individual functions eligible but force global `NotReady` and `bindings == nullopt`.
- No partial auto-activation.
- No hot-swap or mutable guest-call service API on `R5900BlockDispatcher`.
- No `WinMain` integration in this milestone.
- Do not change `Ps2PadHleService` lifecycle semantics to make tests pass.
- Do not weaken ELF/PT_LOAD validation.
- Use synthetic guest PCs and synthetic ELF/R5900 fixtures only; no proprietary Burnout 3 bytes, addresses, hashes, or assets.
- Windows CI on the exact SHA is authoritative.
- Reuse existing test targets instead of editing the large `CMakeLists.txt`: policy tests live in `r5900_analysis_report_tests`, activation-report tests live in `ps2_pad_runtime_report_tests`, and end-to-end activation lives in `r5900_block_dispatcher_guest_call_windows_tests`.

---

## File Structure

### Create

- `src/analysis/ps2_pad_activation.h`
  - Pure policy/model only.
  - Owns activation enums, per-function/global decision structs, provenance checks, duplicate-PC detection, deterministic diagnostics, and `make_ps2_pad_activation_decision()`.
  - May include `runtime/ps2_pad_hle_service.h` only for the `Ps2PadHleBindings` value type; it must never instantiate/call the service.

- `src/analysis/ps2_pad_activation_report.h`
  - Pure deterministic formatter only.
  - No decision logic beyond safe rendering rules such as rejected functions always printing `pc=none` and bindings only when readiness is `Ready`.

### Modify

- `tests/r5900_analysis_report_tests.cpp`
  - Host the portable activation-policy RED/GREEN matrix because the existing target already links `b3r_analysis` → `b3r_runtime`.

- `tests/ps2_pad_runtime_report_tests.cpp`
  - Host deterministic activation-report RED/GREEN coverage beside the existing runtime-confirmation formatter tests.

- `tests/r5900_block_dispatcher_guest_call_windows_tests.cpp`
  - Add the synthetic `decision.bindings` → real `Ps2PadHleService` → dispatcher lifecycle characterization.

- `docs/PROGRESS.md`
  - Update only after implementation-head CI passes.

### Create during validation

- `docs/validation/2026-09-08-ps2-pad-runtime-activation-v0.md`

### Explicitly do not modify

- `src/platform/windows/win_main.cpp`
- `src/runtime/ps2_pad_hle_service.h`
- `src/runtime/ps2_pad_hle_service.cpp`
- `src/recompiler/windows/r5900_block_dispatcher.h`
- `src/recompiler/windows/r5900_block_dispatcher.cpp`
- `src/recompiler/ps2_elf.h`
- `src/recompiler/ps2_elf.cpp`

---

### Task 1: Atomic PAD Activation Policy

**Files:**
- Create: `src/analysis/ps2_pad_activation.h`
- Modify/Test: `tests/r5900_analysis_report_tests.cpp`

**Interfaces:**
- Consumes:
  - `analysis::PadBindingDiscoveryResult`
  - `analysis::PadRuntimeConfirmationResult`
  - `analysis::PadBindingFunction`
  - `analysis::PadBindingConfidence`
  - `analysis::PadRuntimeConfirmationStatus`
  - `runtime::Ps2PadHleBindings`
- Produces:

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
    CanonicalFunctionMismatch,
    RuntimePcNotEvidenceBacked,
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

[[nodiscard]] Ps2PadActivationDecision
make_ps2_pad_activation_decision(
    const PadBindingDiscoveryResult& discovery,
    const PadRuntimeConfirmationResult& runtime);
```

- [ ] **Step 1: Add the first RED fixture helpers and happy-path test**

At the end of `tests/r5900_analysis_report_tests.cpp`, before `main()`, add helpers that initialize all six canonical slots and attach one synthetic evidence PC per function:

```cpp
b3r::analysis::PadBindingDiscoveryResult activation_discovery(
    const std::array<std::uint32_t, 6>& pcs) {
    using namespace b3r::analysis;
    PadBindingDiscoveryResult discovery{};
    for (std::size_t i = 0; i < discovery.resolutions.size(); ++i) {
        const auto function = static_cast<PadBindingFunction>(i);
        auto& resolution = discovery.resolutions[i];
        resolution.function = function;
        resolution.confidence =
            i == 0u || i == 4u || i == 5u
                ? PadBindingConfidence::Trusted
                : (i == 2u ? PadBindingConfidence::Unresolved
                            : PadBindingConfidence::Candidate);
        resolution.evidence.push_back(PadBindingEvidence{
            function,
            i == 0u ? PadBindingEvidenceKind::ElfSymbol
                    : PadBindingEvidenceKind::StaticFingerprint,
            pcs[i],
            i == 0u ? 1000u : 100u,
            "synthetic-activation",
        });
    }
    return discovery;
}

b3r::analysis::PadRuntimeConfirmationResult activation_runtime(
    const std::array<std::uint32_t, 6>& pcs) {
    using namespace b3r::analysis;
    PadRuntimeConfirmationResult runtime{};
    for (std::size_t i = 0; i < runtime.functions.size(); ++i) {
        const auto function = static_cast<PadBindingFunction>(i);
        auto& result = runtime.functions[i];
        result.function = function;
        result.runtime_status = PadRuntimeConfirmationStatus::RuntimeConfirmed;
        result.guest_pc = pcs[i];
        result.compatible_calls = 1u;
        result.calls_observed = 1u;
    }
    return runtime;
}

void test_pad_activation_ready_requires_six_evidence_backed_distinct_confirmations() {
    using namespace b3r::analysis;
    constexpr std::array<std::uint32_t, 6> pcs{
        0x00101000u, 0x00102000u, 0x00103000u,
        0x00104000u, 0x00105000u, 0x00106000u,
    };

    const auto discovery = activation_discovery(pcs);
    const auto runtime = activation_runtime(pcs);
    const auto decision = make_ps2_pad_activation_decision(discovery, runtime);

    expect(decision.readiness == PadActivationReadiness::Ready,
           "six evidence-backed confirmed distinct PCs must be activation-ready");
    expect(decision.bindings.has_value(),
           "Ready activation must materialize complete bindings");
    expect(decision.bindings->pad_init == pcs[0] &&
               decision.bindings->pad_port_open == pcs[1] &&
               decision.bindings->pad_get_state == pcs[2] &&
               decision.bindings->pad_read == pcs[3] &&
               decision.bindings->pad_port_close == pcs[4] &&
               decision.bindings->pad_end == pcs[5],
           "materialized bindings must match the six runtime-confirmed evidence PCs exactly");

    for (const auto& function : decision.functions) {
        expect(function.eligibility == PadActivationEligibility::Eligible,
               "every canonical function must be individually eligible");
        expect(function.reason == PadActivationReason::EligibleRuntimeConfirmed,
               "eligible function must expose runtime-confirmed reason");
        expect(function.guest_pc.has_value() && *function.guest_pc != 0u,
               "eligible function must expose its selected nonzero PC");
    }
}
```

Include `analysis/ps2_pad_activation.h` and `<array>` as needed, then call the new test from `main()`.

- [ ] **Step 2: Run Windows CI to verify the RED**

Commit only the test change first:

```bash
git add tests/r5900_analysis_report_tests.cpp
git commit -m "test: define PS2 PAD activation policy"
```

Push the feature branch and require the Windows CI build to fail because `analysis/ps2_pad_activation.h` or `make_ps2_pad_activation_decision` does not exist. Configure should remain green. Record run/job IDs for the validation ledger.

- [ ] **Step 3: Implement the minimal policy header**

Create `src/analysis/ps2_pad_activation.h` as a header-only unit. Implement canonical identity, provenance, status and global readiness in one pure function.

The per-function sequence must be exactly:

```cpp
for (std::size_t i = 0; i < decision.functions.size(); ++i) {
    const auto expected = static_cast<PadBindingFunction>(i);
    const auto& static_source = discovery.resolutions[i];
    const auto& runtime_source = runtime.functions[i];
    auto& output = decision.functions[i];

    output.function = expected;
    output.static_confidence = static_source.confidence;
    output.runtime_status = runtime_source.runtime_status;

    if (static_source.function != expected || runtime_source.function != expected) {
        output.reason = PadActivationReason::CanonicalFunctionMismatch;
        decision.diagnostics.push_back(
            std::string("canonical_function_mismatch function=") +
            ps2_pad_binding_detail::function_name(expected));
        continue;
    }

    switch (runtime_source.runtime_status) {
    case PadRuntimeConfirmationStatus::Unobserved:
        output.reason = PadActivationReason::Unobserved;
        continue;
    case PadRuntimeConfirmationStatus::ObservedIncompatible:
        output.reason = PadActivationReason::ObservedIncompatible;
        continue;
    case PadRuntimeConfirmationStatus::RuntimeAmbiguous:
        output.reason = PadActivationReason::RuntimeAmbiguous;
        continue;
    case PadRuntimeConfirmationStatus::RuntimeConfirmed:
        break;
    }

    if (!runtime_source.guest_pc.has_value()) {
        output.reason = PadActivationReason::MissingGuestPc;
        continue;
    }
    if (*runtime_source.guest_pc == 0u) {
        output.reason = PadActivationReason::ZeroGuestPc;
        continue;
    }

    const auto selected_pc = *runtime_source.guest_pc;
    const bool evidence_backed = std::any_of(
        static_source.evidence.begin(), static_source.evidence.end(),
        [&](const PadBindingEvidence& evidence) {
            return evidence.function == expected && evidence.guest_pc == selected_pc;
        });
    if (!evidence_backed) {
        output.reason = PadActivationReason::RuntimePcNotEvidenceBacked;
        decision.diagnostics.push_back(
            std::string("runtime_pc_not_evidence_backed function=") +
            ps2_pad_binding_detail::function_name(expected));
        continue;
    }

    output.eligibility = PadActivationEligibility::Eligible;
    output.reason = PadActivationReason::EligibleRuntimeConfirmed;
    output.guest_pc = selected_pc;
}
```

Then count eligible functions. If fewer than six, add exactly `incomplete_activation_set`, sort/deduplicate diagnostics, and return `NotReady`/`nullopt`.

If all six are eligible, detect duplicate PCs using a sorted copy of the six selected values. Each duplicated numerical PC group must add one deterministic diagnostic:

```text
activation guest PC 0x00102000 is selected by multiple PAD functions
```

Use the existing lowercase eight-digit formatter from `ps2_pad_binding_report_detail::format_pc()` or reproduce the exact formatting in a private detail helper if including the report header would create a dependency cycle.

Only if all six are eligible and distinct set:

```cpp
decision.readiness = PadActivationReadiness::Ready;
decision.bindings = runtime::Ps2PadHleBindings{
    decision.functions[0].guest_pc.value(),
    decision.functions[1].guest_pc.value(),
    decision.functions[2].guest_pc.value(),
    decision.functions[3].guest_pc.value(),
    decision.functions[4].guest_pc.value(),
    decision.functions[5].guest_pc.value(),
};
```

Sort and deduplicate diagnostics before return in every path.

- [ ] **Step 4: Run the focused test target and full Windows CI GREEN**

Build/run `r5900_analysis_report_tests` on Windows CI. The full workflow must pass; do not accept only the focused executable as completion evidence.

Commit:

```bash
git add src/analysis/ps2_pad_activation.h
git commit -m "feat: add atomic PS2 PAD activation policy"
```

- [ ] **Step 5: Add the complete policy hardening matrix**

Add these cases to `tests/r5900_analysis_report_tests.cpp` and call them from `main()`:

```cpp
void test_pad_activation_rejects_nonconfirmed_states();
void test_pad_activation_rejects_missing_and_zero_pc();
void test_pad_activation_accepts_trusted_candidate_and_unresolved_confidence();
void test_pad_activation_requires_runtime_pc_to_be_evidence_backed();
void test_pad_activation_rejects_canonical_identity_mismatch();
void test_pad_activation_duplicate_pc_blocks_global_readiness();
void test_pad_activation_five_of_six_never_materializes_partial_bindings();
void test_pad_activation_is_pure_and_deterministic();
```

Required assertions:

```text
Unobserved             -> Rejected / Unobserved / pc cleared
ObservedIncompatible   -> Rejected / ObservedIncompatible / pc cleared
RuntimeAmbiguous       -> Rejected / RuntimeAmbiguous / pc cleared
RuntimeConfirmed+none  -> Rejected / MissingGuestPc
RuntimeConfirmed+0     -> Rejected / ZeroGuestPc
Trusted+confirmed      -> Eligible
Candidate+confirmed    -> Eligible
Unresolved+confirmed   -> Eligible
confirmed PC not in same-function discovery evidence -> Rejected / RuntimePcNotEvidenceBacked
mismatched discovery slot function -> Rejected / CanonicalFunctionMismatch
mismatched runtime slot function -> Rejected / CanonicalFunctionMismatch
same numerical PC for two eligible functions -> both remain Eligible, global NotReady, bindings nullopt
5/6 eligible -> NotReady, bindings nullopt, incomplete_activation_set
identical inputs -> structurally identical outputs
input discovery/runtime objects retain all original confidence/status/PC/evidence values after decision creation
```

For duplicate diagnostics, assert the exact string and assert it appears only once even if the duplicate group contains three functions.

- [ ] **Step 6: Run full Windows CI on the hardened Task 1 head**

Commit only test changes:

```bash
git add tests/r5900_analysis_report_tests.cpp
git commit -m "test: harden PS2 PAD activation policy"
```

Require complete Windows CI PASS before starting Task 2.

---

### Task 2: Deterministic PAD Activation Report

**Files:**
- Create: `src/analysis/ps2_pad_activation_report.h`
- Modify/Test: `tests/ps2_pad_runtime_report_tests.cpp`

**Interfaces:**
- Consumes: `const Ps2PadActivationDecision&`
- Produces:

```cpp
[[nodiscard]] std::string
format_ps2_pad_activation_decision(
    const Ps2PadActivationDecision& decision);
```

- [ ] **Step 1: Write the RED canonical report test**

Include `analysis/ps2_pad_activation_report.h` and add a fixture that calls the real Task 1 decision maker with six synthetic evidence-backed confirmed PCs. Expected output must be byte-exact:

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

Also assert formatting the same decision twice is byte-identical.

- [ ] **Step 2: Run Windows CI to verify RED**

Commit the report test only:

```bash
git add tests/ps2_pad_runtime_report_tests.cpp
git commit -m "test: define PS2 PAD activation report"
```

Expected failure: Build fails because `analysis/ps2_pad_activation_report.h` or the formatter API does not exist. Record run/job IDs.

- [ ] **Step 3: Implement the formatter header**

Create `src/analysis/ps2_pad_activation_report.h`.

Add private name helpers with these exact strings:

```text
PadActivationEligibility::Eligible -> eligible
PadActivationEligibility::Rejected -> rejected
PadActivationReadiness::Ready       -> ready
PadActivationReadiness::NotReady    -> not_ready
EligibleRuntimeConfirmed            -> eligible_runtime_confirmed
Unobserved                          -> unobserved
ObservedIncompatible                -> observed_incompatible
RuntimeAmbiguous                    -> runtime_ambiguous
MissingGuestPc                      -> missing_guest_pc
ZeroGuestPc                         -> zero_guest_pc
CanonicalFunctionMismatch           -> canonical_function_mismatch
RuntimePcNotEvidenceBacked          -> runtime_pc_not_evidence_backed
```

Formatting algorithm:

```cpp
std::size_t eligible_count{};
for (const auto& function : decision.functions) {
    if (function.eligibility == PadActivationEligibility::Eligible) {
        ++eligible_count;
    }
}

out << "PAD_ACTIVATION_V0 readiness=" << readiness_name(decision.readiness)
    << " eligible=" << eligible_count << " required=6\n";

for (std::size_t i = 0; i < decision.functions.size(); ++i) {
    const auto expected = static_cast<PadBindingFunction>(i);
    const auto& source = decision.functions[i];
    out << "PAD_ACTIVATION function="
        << ps2_pad_binding_detail::function_name(expected)
        << " static_confidence=" << confidence_name(source.static_confidence)
        << " runtime_status=" << status_name(source.runtime_status)
        << " eligibility=" << eligibility_name(source.eligibility)
        << " pc=";

    if (source.eligibility == PadActivationEligibility::Eligible &&
        source.guest_pc.has_value()) {
        out << format_pc(*source.guest_pc);
    } else {
        out << "none";
    }
    out << " reason=" << reason_name(source.reason) << '\n';
}
```

Emit `PAD_ACTIVATION_BINDINGS` only when both conditions are true:

```cpp
decision.readiness == PadActivationReadiness::Ready &&
decision.bindings.has_value()
```

Copy/sort/deduplicate `decision.diagnostics` before rendering each as:

```text
PAD_ACTIVATION_DIAGNOSTIC <diagnostic>
```

Always terminate with exactly:

```text
PAD_ACTIVATION_END
```

- [ ] **Step 4: Run full Windows CI GREEN**

Commit:

```bash
git add src/analysis/ps2_pad_activation_report.h
git commit -m "feat: add deterministic PS2 PAD activation report"
```

Require complete workflow PASS.

- [ ] **Step 5: Harden formatter behavior against inconsistent input objects**

Add tests that manually mutate a copied `Ps2PadActivationDecision` after Task 1 produced it, proving the formatter does not expose unsafe data:

```text
Rejected function carrying guest_pc -> pc=none
NotReady decision carrying bindings -> no PAD_ACTIVATION_BINDINGS line
Ready decision without bindings -> no PAD_ACTIVATION_BINDINGS line
unsorted duplicate diagnostics -> sorted/deduplicated output
report contains no "score=", "args=", "bytes=", "ram=", or "code="
exactly six PAD_ACTIVATION function= lines
canonical function order is padInit, padPortOpen, padGetState, padRead, padPortClose, padEnd
```

- [ ] **Step 6: Run full Windows CI on the hardened Task 2 head**

Commit test-only hardening and require complete workflow PASS before Task 3.

---

### Task 3: Synthetic Activation Through the Real PAD HLE Service

**Files:**
- Modify/Test: `tests/r5900_block_dispatcher_guest_call_windows_tests.cpp`
- Production code changes: none expected.

**Interfaces:**
- Consumes:
  - `make_ps2_pad_activation_decision()` from Task 1
  - `decision.bindings`
  - existing `runtime::Ps2PadHleService`
  - existing `recompiler::R5900BlockDispatcher`
- Produces: end-to-end characterization proving materialized bindings are usable without hot-swap or production wiring changes.

- [ ] **Step 1: Add activation/discovery/runtime helpers using six distinct synthetic PCs**

At the top of `tests/r5900_block_dispatcher_guest_call_windows_tests.cpp`, include:

```cpp
#include "analysis/ps2_pad_activation.h"
#include "input/ps2_pad_report.h"
#include "runtime/ps2_pad_hle_service.h"
```

Add a helper whose six PCs are all in the synthetic ELF executable region and do not overlap the caller PCs. The exact values may be, for a synthetic base of `0x00100000`:

```cpp
constexpr std::array<std::uint32_t, 6> pad_pcs{
    0x00100100u,
    0x00100120u,
    0x00100140u,
    0x00100160u,
    0x00100180u,
    0x001001a0u,
};
```

Create discovery/runtime results with mixed `Trusted`, `Candidate`, and `Unresolved` confidence, each runtime result `RuntimeConfirmed`, and each PC present as evidence for the matching function.

Call the real policy and assert:

```cpp
const auto decision = make_ps2_pad_activation_decision(discovery, runtime);
expect(decision.readiness == PadActivationReadiness::Ready,
       "synthetic six-function PAD activation must be ready");
expect(decision.bindings.has_value(),
       "ready activation must supply bindings for the HLE service");
```

Do not construct a second independent `Ps2PadHleBindings` literal anywhere in the activation test.

- [ ] **Step 2: Construct the real service directly from `decision.bindings`**

```cpp
b3r::runtime::Ps2PadHleService service(*decision.bindings);

b3r::input::Ps2PadReport report{};
report.connected = true;
report.buttons_active_low = 0xffefu;
report.left_x = 0x12u;
report.left_y = 0x34u;
report.right_x = 0x56u;
report.right_y = 0x78u;
service.set_report(report);
```

Use one dispatcher constructed with:

```cpp
R5900BlockDispatcherOptions options{};
options.guest_calls = &service;
R5900BlockDispatcher dispatcher(memory, options);
```

There must be no setter/hot-swap API added to the dispatcher.

- [ ] **Step 3: Execute the six-call lifecycle in separate explicit dispatch phases**

For each synthetic bound PC, seed `$ra` to a synthetic unsupported return PC so the HLE call is intercepted first and dispatcher then stops deterministically when execution resumes.

Call sequence and required assertions:

```text
padInit(0)
  guest_calls_handled == 1
  v0 == 1
  service.initialized() == true

padPortOpen(0,0,pad_area)
  pad_area is 64-byte aligned and 256 bytes backed
  guest_calls_handled == 1
  v0 == 1
  service.port_open() == true
  service.pad_area_address() == pad_area

padGetState(0,0)
  guest_calls_handled == 1
  v0 == 0x06 while connected/open

padRead(0,0,destination)
  destination is 32 bytes backed
  guest_calls_handled == 1
  v0 == 32
  destination bytes equal the existing HLE report layout:
    byte[0] = 0x00
    byte[1] = 0x79
    byte[2] = low8(buttons_active_low)
    byte[3] = high8(buttons_active_low)
    byte[4] = right_x
    byte[5] = right_y
    byte[6] = left_x
    byte[7] = left_y
    bytes[8..31] = 0

padPortClose(0,0)
  guest_calls_handled == 1
  v0 == 1
  service.port_open() == false
  service.pad_area_address() == 0

padEnd()
  guest_calls_handled == 1
  v0 == 1
  service.initialized() == false
  service.port_open() == false
```

Use the materialized activation PCs for every `dispatcher.run()` start PC.

- [ ] **Step 4: Prove unknown PC remains outside PAD HLE**

Use a synthetic executable PC not present in any binding. Either call `service.try_handle()` directly and require `NotHandled`, or run a dispatcher block at that PC and prove zero PAD guest calls are handled before normal/unsupported execution proceeds. The test must not modify the binding object.

- [ ] **Step 5: Run full Windows CI for Task 3**

Commit:

```bash
git add tests/r5900_block_dispatcher_guest_call_windows_tests.cpp
git commit -m "test: activate synthetic PAD HLE from runtime decision"
```

Require complete Windows workflow PASS.

If Task 3 fails, debug the test/production boundary systematically. Do not weaken `Ps2PadHleService` lifecycle requirements, do not add partial activation, and do not add dispatcher hot-swap merely to satisfy the fixture.

- [ ] **Step 6: Audit implementation scope before documentation**

Compare feature head to base `63f644a60d965eb32425dfa3a5b01aba6bc82712` and require:

```text
allowed production additions:
  src/analysis/ps2_pad_activation.h
  src/analysis/ps2_pad_activation_report.h

allowed test modifications:
  tests/r5900_analysis_report_tests.cpp
  tests/ps2_pad_runtime_report_tests.cpp
  tests/r5900_block_dispatcher_guest_call_windows_tests.cpp

forbidden modifications:
  src/runtime/ps2_pad_hle_service.*
  src/platform/windows/win_main.cpp
  src/recompiler/windows/r5900_block_dispatcher.*
  src/recompiler/ps2_elf.*
```

Also inspect added fixture literals and confirm they are synthetic only.

---

### Task 4: Validation Ledger and Exact-Head CI

**Files:**
- Create: `docs/validation/2026-09-08-ps2-pad-runtime-activation-v0.md`
- Modify: `docs/PROGRESS.md`

**Interfaces:**
- Consumes all RED/GREEN run IDs, job IDs, exact SHAs, test counts, pacing evidence, and integrity audit from Tasks 1–3.
- Produces the authoritative milestone status only after two documentation-head CI gates.

- [ ] **Step 1: Verify implementation-head Windows CI completely**

On the exact implementation head after Task 3, require all of:

```text
Configure                         PASS
Build                             PASS
CTest                             all tests PASS
r5900_analysis_report_tests       PASS
ps2_pad_runtime_report_tests      PASS
r5900 guest-call dispatcher tests PASS
Frame pacing telemetry            PASS
120 Hz pacing probe               PASS
Analyzer package validation       PASS
Pacing package validation         PASS
```

Record exact SHA, CI run number/ID and job ID.

- [ ] **Step 2: Create the validation ledger with status `PENDING_FINAL_EXACT_HEAD_CI`**

The ledger must document:

- base SHA;
- feature branch;
- design/spec and plan paths;
- Task 1 RED/GREEN/hardening SHAs + CI evidence;
- Task 2 RED/GREEN/hardening SHAs + CI evidence;
- Task 3 characterization SHA + CI evidence;
- policy matrix including evidence provenance and duplicate-PC rejection;
- activation report determinism;
- complete synthetic HLE lifecycle;
- implementation-head CTest count;
- pacing telemetry and probe values from CI;
- integrity audit;
- no real-game activation claims;
- explicit note that real Burnout activation remains `PENDING_EXTERNAL_VALIDATION`.

- [ ] **Step 3: Update `docs/PROGRESS.md` conservatively**

Set current milestone to `PS2 PAD Runtime Activation v0` and status to:

```text
PENDING_FINAL_EXACT_HEAD_CI
```

Do not mark `CI_VALIDATED` yet. Keep these non-claims explicit:

```text
real Burnout PAD activation: PENDING_EXTERNAL_VALIDATION
game boot/render/audio/menu/gameplay: not implemented
```

- [ ] **Step 4: Commit exactly the two documentation files and run documentation-head CI**

Commit:

```bash
git add docs/PROGRESS.md docs/validation/2026-09-08-ps2-pad-runtime-activation-v0.md
git commit -m "docs: record PS2 PAD runtime activation validation"
```

Verify the diff from implementation head contains exactly those two files. Require complete Windows CI PASS on that exact documentation SHA.

- [ ] **Step 5: Mark the two status documents `CI_VALIDATED` only after Step 4 CI passes**

Update only:

```text
docs/PROGRESS.md
docs/validation/2026-09-08-ps2-pad-runtime-activation-v0.md
```

Record the successful documentation-head run and exact SHA. Commit:

```bash
git add docs/PROGRESS.md docs/validation/2026-09-08-ps2-pad-runtime-activation-v0.md
git commit -m "docs: mark PS2 PAD runtime activation CI validated"
```

Verify the status-only diff contains exactly those two files.

- [ ] **Step 6: Run final exact-head Windows CI**

On the exact status commit require:

```text
Configure                         PASS
Build                             PASS
CTest                             all tests PASS
Frame pacing telemetry            PASS
120 Hz pacing probe               PASS
Analyzer package validation       PASS
Pacing package validation         PASS
```

Also verify the branch head still equals the tested SHA after the run completes.

Only then report:

```text
PS2 PAD Runtime Activation v0 = CI_VALIDATED
```

Do not add another commit merely to record the final run ID; that would create a new unvalidated head. Report the final run ID in chat and leave the status commit as the exact tested head.

---

## Completion Checklist

Before claiming completion, re-read the spec and verify every item below against the exact final head:

- [ ] Six evidence-backed `RuntimeConfirmed` nonzero distinct PCs produce `Ready`.
- [ ] `Trusted`, `Candidate`, and `Unresolved` confidence remain unchanged and may all be eligible.
- [ ] `Unobserved`, `ObservedIncompatible`, and `RuntimeAmbiguous` are rejected.
- [ ] Missing and zero runtime PCs are rejected.
- [ ] Runtime PC absent from same-function discovery evidence is rejected.
- [ ] Canonical discovery/runtime function identity mismatch is rejected and diagnosed.
- [ ] Duplicate activation PCs force global `NotReady` with no bindings.
- [ ] Any incomplete set produces no partial `Ps2PadHleBindings`.
- [ ] `PAD_ACTIVATION_V0` output is canonical and byte-deterministic.
- [ ] Rejected functions always render `pc=none`.
- [ ] Bindings line is emitted only for `Ready + bindings`.
- [ ] The real `Ps2PadHleService` is constructed from `decision.bindings`, not an independent test literal.
- [ ] Synthetic init/open/get-state/read/close/end lifecycle passes through dispatcher guest-call interception.
- [ ] Unknown PC remains unhandled by PAD HLE.
- [ ] `Ps2PadHleService` production behavior is unchanged.
- [ ] `R5900BlockDispatcher` has no hot-swap API/change.
- [ ] `WinMain` is unchanged.
- [ ] ELF/PT_LOAD validation is unchanged.
- [ ] No proprietary game data or Burnout-specific guest PC was added.
- [ ] Full Windows CI passes on implementation head.
- [ ] Full Windows CI passes on pending-status documentation head.
- [ ] Full Windows CI passes on exact final `CI_VALIDATED` status head.
- [ ] Real Burnout 3 activation remains `PENDING_EXTERNAL_VALIDATION`.
