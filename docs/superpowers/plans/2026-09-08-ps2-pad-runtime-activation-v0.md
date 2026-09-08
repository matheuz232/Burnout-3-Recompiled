# PS2 PAD Runtime Activation v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert evidence-backed, uniquely runtime-confirmed PAD entry points into an atomic six-function `Ps2PadHleBindings` decision and prove those exact bindings can drive the existing PAD HLE lifecycle through the R5900 dispatcher.

**Architecture:** Add a pure activation policy between `PadRuntimeConfirmationResult` and `Ps2PadHleBindings`, plus a deterministic `PAD_ACTIVATION_V0` formatter. Activation is two-phase: observe/confirm first, compute a snapshot decision, then construct a new `Ps2PadHleService` and dispatcher only when global readiness is `Ready`. No hot-swap, no partial bindings, no `WinMain` wiring.

**Tech Stack:** C++20, CMake 3.25+, Visual Studio 2022 x64, existing `b3r_analysis`, `b3r_runtime`, `b3r_recompiler_dispatcher_x64`, Windows GitHub Actions CI.

**Spec:** `docs/superpowers/specs/2026-09-08-ps2-pad-runtime-activation-v0-design.md`

## Global Constraints

- Base implementation SHA: `63f644a60d965eb32425dfa3a5b01aba6bc82712`.
- Create `feature/ps2-pad-runtime-activation-v0` from the final plan head.
- Use TDD RED→GREEN for Tasks 1 and 2.
- Task 3 is end-to-end characterization of Task 1 plus existing production components.
- Static confidence, runtime confirmation, and activation readiness remain separate concepts.
- A runtime-confirmed PC is activation-eligible only when that exact nonzero PC exists in discovery evidence for the same canonical function.
- `Trusted`, `Candidate`, and `Unresolved` static confidence may all be eligible when runtime confirmation and provenance checks pass.
- Cross-input provenance validation must require all three conditions at each canonical index:
  - discovery function identity equals the expected canonical function;
  - runtime function identity equals the expected canonical function;
  - runtime `static_confidence` equals discovery `confidence`.
- Any cross-input mismatch is `Rejected / InputMismatch` and clears the output PC.
- Global activation is atomic: all six functions eligible, all six PCs nonzero, all six PCs pairwise-distinct, or `bindings == nullopt`.
- Duplicate selected PCs across functions keep individual functions eligible but force global `NotReady`.
- No partial auto-activation.
- No hot-swap or mutable guest-call service API on `R5900BlockDispatcher`.
- No `WinMain` integration in this milestone.
- Do not change `Ps2PadHleService` lifecycle semantics to satisfy activation tests.
- Do not weaken ELF/PT_LOAD validation.
- Use synthetic guest PCs and synthetic ELF/R5900 fixtures only.
- No proprietary Burnout 3 bytes, addresses, hashes, assets, or PCSX2 runtime dependency.
- Windows CI on the exact SHA is authoritative.
- Reuse existing test targets and do not edit `CMakeLists.txt`:
  - policy → `r5900_analysis_report_tests`
  - activation formatter → `ps2_pad_runtime_report_tests`
  - HLE integration → `r5900_block_dispatcher_guest_call_windows_tests`

---

## File Structure

### Create

- `src/analysis/ps2_pad_activation.h`
  - Pure policy/model only.
  - Owns activation enums, per-function/global decisions, cross-input provenance validation, evidence-PC validation, duplicate-PC validation, diagnostics, and `make_ps2_pad_activation_decision()`.
  - Includes `runtime/ps2_pad_hle_service.h` only for `Ps2PadHleBindings`.
  - Does not construct or call `Ps2PadHleService`.

- `src/analysis/ps2_pad_activation_report.h`
  - Pure deterministic formatter.
  - Does not compute eligibility/readiness.

### Modify

- `tests/r5900_analysis_report_tests.cpp`
- `tests/ps2_pad_runtime_report_tests.cpp`
- `tests/r5900_block_dispatcher_guest_call_windows_tests.cpp`
- `docs/PROGRESS.md` only after implementation-head CI passes.

### Create during validation

- `docs/validation/2026-09-08-ps2-pad-runtime-activation-v0.md`

### Must remain unchanged

- `CMakeLists.txt`
- `src/platform/windows/win_main.cpp`
- `src/runtime/ps2_pad_hle_service.h`
- `src/runtime/ps2_pad_hle_service.cpp`
- `src/recompiler/windows/r5900_block_dispatcher.h`
- `src/recompiler/windows/r5900_block_dispatcher.cpp`
- `src/recompiler/ps2_elf.h`
- `src/recompiler/ps2_elf.cpp`

---

## Task 1: Atomic PAD Activation Policy

**Files:**
- Create: `src/analysis/ps2_pad_activation.h`
- Modify/Test: `tests/r5900_analysis_report_tests.cpp`

**Produces:**

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

[[nodiscard]] Ps2PadActivationDecision
make_ps2_pad_activation_decision(
    const PadBindingDiscoveryResult& discovery,
    const PadRuntimeConfirmationResult& runtime);
```

### Fixed portable fixture

Use exactly:

```cpp
constexpr std::array<std::uint32_t, 6> kActivationPcs{
    0x00101000u,
    0x00102000u,
    0x00103000u,
    0x00104000u,
    0x00105000u,
    0x00106000u,
};
```

Confidence pattern:

```text
padInit       Trusted
padPortOpen   Candidate
padGetState   Unresolved
padRead       Candidate
padPortClose  Trusted
padEnd        Trusted
```

Use one helper to return that confidence for both discovery and runtime fixtures:

```cpp
b3r::analysis::PadBindingConfidence activation_confidence(std::size_t index) {
    using b3r::analysis::PadBindingConfidence;
    if (index == 0u || index == 4u || index == 5u) {
        return PadBindingConfidence::Trusted;
    }
    if (index == 2u) {
        return PadBindingConfidence::Unresolved;
    }
    return PadBindingConfidence::Candidate;
}
```

- [ ] **Step 1: Write first RED policy test**

Add `#include "analysis/ps2_pad_activation.h"` and `<array>` to `tests/r5900_analysis_report_tests.cpp`.

Add:

```cpp
b3r::analysis::PadBindingDiscoveryResult activation_discovery(
    const std::array<std::uint32_t, 6>& pcs) {
    using namespace b3r::analysis;
    PadBindingDiscoveryResult discovery{};
    for (std::size_t i = 0; i < discovery.resolutions.size(); ++i) {
        const auto function = static_cast<PadBindingFunction>(i);
        auto& resolution = discovery.resolutions[i];
        resolution.function = function;
        resolution.confidence = activation_confidence(i);
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
        auto& result = runtime.functions[i];
        result.function = static_cast<PadBindingFunction>(i);
        result.static_confidence = activation_confidence(i);
        result.runtime_status = PadRuntimeConfirmationStatus::RuntimeConfirmed;
        result.guest_pc = pcs[i];
        result.calls_observed = 1u;
        result.compatible_calls = 1u;
    }
    return runtime;
}

void test_pad_activation_ready_requires_six_evidence_backed_distinct_confirmations() {
    using namespace b3r::analysis;
    const auto discovery = activation_discovery(kActivationPcs);
    const auto runtime = activation_runtime(kActivationPcs);
    const auto decision = make_ps2_pad_activation_decision(discovery, runtime);

    expect(decision.readiness == PadActivationReadiness::Ready,
           "six consistent evidence-backed confirmed distinct PCs must be activation-ready");
    expect(decision.bindings.has_value(),
           "Ready activation must materialize complete bindings");
    expect(decision.bindings->pad_init == kActivationPcs[0] &&
               decision.bindings->pad_port_open == kActivationPcs[1] &&
               decision.bindings->pad_get_state == kActivationPcs[2] &&
               decision.bindings->pad_read == kActivationPcs[3] &&
               decision.bindings->pad_port_close == kActivationPcs[4] &&
               decision.bindings->pad_end == kActivationPcs[5],
           "bindings must exactly match the six confirmed evidence PCs");
}
```

Call the test from `main()`.

- [ ] **Step 2: Commit RED and verify Windows CI fails because production activation API is absent**

```bash
git add tests/r5900_analysis_report_tests.cpp
git commit -m "test: define PS2 PAD activation policy"
```

Expected:

```text
Configure PASS
Build FAIL
cause: analysis/ps2_pad_activation.h or make_ps2_pad_activation_decision missing
```

Record run/job IDs.

- [ ] **Step 3: Implement `src/analysis/ps2_pad_activation.h` minimally**

Required includes:

```cpp
#pragma once

#include "analysis/ps2_pad_binding_discovery.h"
#include "analysis/ps2_pad_runtime_confirmation.h"
#include "runtime/ps2_pad_hle_service.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
```

Define public enums/structs exactly as above.

In `ps2_pad_activation_detail`, define exactly one private PC formatter:

```cpp
[[nodiscard]] inline std::string format_activation_pc(std::uint32_t pc) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << pc;
    return out.str();
}
```

For every canonical index `i`:

```cpp
const auto expected = static_cast<PadBindingFunction>(i);
const auto& discovery_source = discovery.resolutions[i];
const auto& runtime_source = runtime.functions[i];
auto& output = decision.functions[i];
output.function = expected;
output.runtime_status = runtime_source.runtime_status;
```

Perform all three input-consistency checks before status evaluation:

```cpp
bool input_mismatch = false;
if (discovery_source.function != expected) {
    input_mismatch = true;
    decision.diagnostics.push_back(
        std::string("input_mismatch function=") +
        ps2_pad_binding_detail::function_name(expected) +
        " field=discovery_function");
}
if (runtime_source.function != expected) {
    input_mismatch = true;
    decision.diagnostics.push_back(
        std::string("input_mismatch function=") +
        ps2_pad_binding_detail::function_name(expected) +
        " field=runtime_function");
}
if (runtime_source.static_confidence != discovery_source.confidence) {
    input_mismatch = true;
    decision.diagnostics.push_back(
        std::string("input_mismatch function=") +
        ps2_pad_binding_detail::function_name(expected) +
        " field=static_confidence");
}
if (input_mismatch) {
    output.reason = PadActivationReason::InputMismatch;
    output.guest_pc.reset();
    continue;
}
```

Only after consistency succeeds:

```cpp
output.static_confidence = discovery_source.confidence;
```

Then apply this decision table:

```text
Unobserved                  -> Rejected / Unobserved
ObservedIncompatible        -> Rejected / ObservedIncompatible
RuntimeAmbiguous            -> Rejected / RuntimeAmbiguous
RuntimeConfirmed + no PC    -> Rejected / MissingGuestPc
RuntimeConfirmed + PC 0     -> Rejected / ZeroGuestPc
RuntimeConfirmed + nonzero PC absent from same-function discovery evidence
                            -> Rejected / PcNotInDiscoveryEvidence
RuntimeConfirmed + nonzero same-function evidence-backed PC
                            -> Eligible / EligibleRuntimeConfirmed
```

Evidence check is exactly:

```cpp
const bool evidence_backed = std::any_of(
    discovery_source.evidence.begin(), discovery_source.evidence.end(),
    [&](const PadBindingEvidence& evidence) {
        return evidence.function == expected &&
               evidence.guest_pc == selected_pc;
    });
```

When absent:

```cpp
output.reason = PadActivationReason::PcNotInDiscoveryEvidence;
output.guest_pc.reset();
decision.diagnostics.push_back(
    std::string("pc_not_in_discovery_evidence function=") +
    ps2_pad_binding_detail::function_name(expected) +
    " pc=" + ps2_pad_activation_detail::format_activation_pc(selected_pc));
continue;
```

When eligible:

```cpp
output.eligibility = PadActivationEligibility::Eligible;
output.reason = PadActivationReason::EligibleRuntimeConfirmed;
output.guest_pc = selected_pc;
```

After classification, count eligible functions. If count != 6:

```cpp
decision.diagnostics.push_back("incomplete_activation_set");
```

sort/dedup diagnostics and return `NotReady` with no bindings.

For 6/6 eligible, detect duplicate selected PCs by sorting a six-element PC copy. Add exactly one diagnostic per duplicated numerical group:

```text
activation guest PC 0x???????? is selected by multiple PAD functions
```

If any duplicate exists, return `NotReady` and no bindings.

Only when all six are eligible and pairwise-distinct:

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

Sort/dedup diagnostics before return.

- [ ] **Step 4: Commit GREEN and require full Windows CI PASS**

```bash
git add src/analysis/ps2_pad_activation.h
git commit -m "feat: add atomic PS2 PAD activation policy"
```

- [ ] **Step 5: Add complete policy hardening matrix**

Add and call:

```cpp
void test_pad_activation_rejects_nonconfirmed_states();
void test_pad_activation_rejects_missing_and_zero_pc();
void test_pad_activation_accepts_all_static_confidence_levels();
void test_pad_activation_rejects_input_mismatch();
void test_pad_activation_requires_same_function_discovery_evidence();
void test_pad_activation_duplicate_pc_blocks_global_readiness();
void test_pad_activation_incomplete_set_never_materializes_partial_bindings();
void test_pad_activation_is_pure_and_deterministic();
```

Required cases:

```text
Unobserved             -> Rejected / Unobserved / pc none
ObservedIncompatible   -> Rejected / ObservedIncompatible / pc none
RuntimeAmbiguous       -> Rejected / RuntimeAmbiguous / pc none
RuntimeConfirmed+none  -> Rejected / MissingGuestPc
RuntimeConfirmed+0     -> Rejected / ZeroGuestPc
Trusted+confirmed      -> Eligible
Candidate+confirmed    -> Eligible
Unresolved+confirmed   -> Eligible
discovery function mismatch -> InputMismatch + field=discovery_function diagnostic
runtime function mismatch   -> InputMismatch + field=runtime_function diagnostic
runtime/discovery confidence mismatch -> InputMismatch + field=static_confidence diagnostic
confirmed PC absent from same-function evidence -> PcNotInDiscoveryEvidence
same numerical PC existing only under another function's evidence -> PcNotInDiscoveryEvidence
all rejected outputs clear malformed source guest_pc
two functions select same PC -> both individually Eligible, global NotReady, no bindings
three functions select same PC -> exactly one duplicate-group diagnostic
5/6 eligible -> NotReady, no bindings, incomplete_activation_set
same inputs twice -> structurally identical readiness/functions/bindings/diagnostics
source discovery/runtime objects retain original function/confidence/status/PC/evidence after decision creation
```

- [ ] **Step 6: Commit hardening and require full Windows CI PASS**

```bash
git add tests/r5900_analysis_report_tests.cpp
git commit -m "test: harden PS2 PAD activation policy"
```

Record exact hardened SHA and CI evidence.

---

## Task 2: Deterministic PAD Activation Report

**Files:**
- Create: `src/analysis/ps2_pad_activation_report.h`
- Modify/Test: `tests/ps2_pad_runtime_report_tests.cpp`

**Produces:**

```cpp
[[nodiscard]] std::string
format_ps2_pad_activation_decision(
    const Ps2PadActivationDecision& decision);
```

- [ ] **Step 1: Write RED canonical report test**

Include `analysis/ps2_pad_activation_report.h`.

Create a ready decision using the real Task 1 policy with the same fixed six PCs and confidence pattern.

Expected byte-exact output:

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

Assert repeated render is byte-identical.

- [ ] **Step 2: Commit RED and verify Windows CI Build fails for missing formatter**

```bash
git add tests/ps2_pad_runtime_report_tests.cpp
git commit -m "test: define PS2 PAD activation report"
```

Expected:

```text
Configure PASS
Build FAIL
cause: analysis/ps2_pad_activation_report.h or formatter API missing
```

- [ ] **Step 3: Implement `src/analysis/ps2_pad_activation_report.h`**

Include:

```cpp
#pragma once

#include "analysis/ps2_pad_activation.h"
#include "analysis/ps2_pad_binding_report.h"
#include "analysis/ps2_pad_runtime_report.h"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>
```

Canonical strings:

```text
Eligible                    eligible
Rejected                    rejected
Ready                       ready
NotReady                    not_ready
EligibleRuntimeConfirmed    eligible_runtime_confirmed
Unobserved                  unobserved
ObservedIncompatible        observed_incompatible
RuntimeAmbiguous            runtime_ambiguous
MissingGuestPc              missing_guest_pc
ZeroGuestPc                 zero_guest_pc
InputMismatch               input_mismatch
PcNotInDiscoveryEvidence    pc_not_in_discovery_evidence
```

Use:

```cpp
ps2_pad_binding_detail::function_name(...)
ps2_pad_binding_report_detail::confidence_name(...)
ps2_pad_binding_report_detail::format_pc(...)
ps2_pad_runtime_report_detail::status_name(...)
```

Rendering contract:

1. Header: `PAD_ACTIVATION_V0 readiness=<...> eligible=<actual-count> required=6`.
2. Exactly six function lines in canonical enum order.
3. Function PC renders only when `eligibility == Eligible && guest_pc.has_value()`; otherwise `pc=none`.
4. `PAD_ACTIVATION_BINDINGS` renders only when `readiness == Ready && bindings.has_value()`.
5. Copy/sort/dedup diagnostics before rendering.
6. Each diagnostic line is `PAD_ACTIVATION_DIAGNOSTIC <diagnostic>`.
7. End exactly with `PAD_ACTIVATION_END\n`.
8. Never render scores, args, register dumps, RAM bytes, code bytes, hashes, or proprietary data.

- [ ] **Step 4: Commit GREEN and require full Windows CI PASS**

```bash
git add src/analysis/ps2_pad_activation_report.h
git commit -m "feat: add deterministic PS2 PAD activation report"
```

- [ ] **Step 5: Harden formatter against malformed copied decisions**

Add exact tests:

```text
Rejected function carrying guest_pc -> pc=none
NotReady decision carrying bindings -> no PAD_ACTIVATION_BINDINGS
Ready decision with bindings reset -> no PAD_ACTIVATION_BINDINGS
InputMismatch reason -> input_mismatch
PcNotInDiscoveryEvidence reason -> pc_not_in_discovery_evidence
unsorted duplicate diagnostics -> sorted/deduplicated
exactly six PAD_ACTIVATION function= lines
canonical order padInit/padPortOpen/padGetState/padRead/padPortClose/padEnd
no "score=", "args=", "bytes=", "ram=", "code=", "hash="
repeat render byte-identical
```

- [ ] **Step 6: Commit hardening and require full Windows CI PASS**

```bash
git add tests/ps2_pad_runtime_report_tests.cpp
git commit -m "test: harden PS2 PAD activation report"
```

Record exact hardened SHA and CI evidence.

---

## Task 3: Synthetic Activation Through Real PAD HLE

**Files:**
- Modify/Test: `tests/r5900_block_dispatcher_guest_call_windows_tests.cpp`
- Production changes: none.

### Fixed synthetic PCs

Use exactly:

```cpp
constexpr std::array<std::uint32_t, 6> kSyntheticActivePadPcs{
    0x00100100u,
    0x00100120u,
    0x00100140u,
    0x00100160u,
    0x00100180u,
    0x001001a0u,
};
constexpr std::uint32_t kUnknownPadPc = 0x001001c0u;
```

- [ ] **Step 1: Add activation fixture using cross-input-consistent discovery/runtime data**

Add:

```cpp
#include "analysis/ps2_pad_activation.h"
#include "input/ps2_pad_report.h"
#include "runtime/ps2_pad_hle_service.h"
#include <array>
```

Build discovery/runtime records for all six functions with:

- canonical function identities in both inputs;
- matching static confidence in both inputs;
- one same-function discovery evidence record at the selected PC;
- runtime status `RuntimeConfirmed`;
- exact selected PC from `kSyntheticActivePadPcs`.

Call:

```cpp
const auto decision = make_ps2_pad_activation_decision(discovery, runtime);
expect(decision.readiness == PadActivationReadiness::Ready,
       "synthetic six-function PAD activation must be Ready");
expect(decision.bindings.has_value(),
       "Ready activation must supply bindings");
```

Do not construct another independent `Ps2PadHleBindings` literal.

- [ ] **Step 2: Construct real service from `decision.bindings` and a phase-B dispatcher**

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

R5900BlockDispatcherOptions options{};
options.guest_calls = &service;
R5900BlockDispatcher dispatcher(memory, options);
```

No observer/HLE hot-swap occurs inside this dispatcher.

- [ ] **Step 3: Execute full lifecycle through dispatcher using exact materialized binding PCs**

Use one fixed synthetic unsupported return PC backed by the fixture for `$ra` on every HLE call.

Required sequence/assertions:

```text
padInit(0)
  start_pc = decision.bindings->pad_init
  guest_calls_handled == 1
  v0 == 1
  initialized == true

padPortOpen(0,0,pad_area)
  start_pc = decision.bindings->pad_port_open
  pad_area != 0
  pad_area % 64 == 0
  256-byte span backed
  guest_calls_handled == 1
  v0 == 1
  port_open == true
  pad_area_address == pad_area

padGetState(0,0)
  start_pc = decision.bindings->pad_get_state
  guest_calls_handled == 1
  v0 == 0x06

padRead(0,0,destination)
  start_pc = decision.bindings->pad_read
  32-byte span backed
  guest_calls_handled == 1
  v0 == 32
  bytes[0..7] == {0x00,0x79,0xef,0xff,0x56,0x78,0x12,0x34}
  bytes[8..31] == 0

padPortClose(0,0)
  start_pc = decision.bindings->pad_port_close
  guest_calls_handled == 1
  v0 == 1
  port_open == false
  pad_area_address == 0

padEnd()
  start_pc = decision.bindings->pad_end
  guest_calls_handled == 1
  v0 == 1
  initialized == false
  port_open == false
```

- [ ] **Step 4: Prove fixed unknown PC is not handled by PAD service**

Use the service directly:

```cpp
R5900IrExecutionState unknown_state{};
const auto unknown = service.try_handle(
    R5900GuestCallRequest{kUnknownPadPc}, unknown_state, memory);
expect(unknown.status == R5900GuestCallStatus::NotHandled,
       "unbound synthetic PC must not be handled by activated PAD HLE");
```

Assert `kUnknownPadPc` differs from all six materialized binding fields.

- [ ] **Step 5: Commit Task 3 and require full Windows CI PASS**

```bash
git add tests/r5900_block_dispatcher_guest_call_windows_tests.cpp
git commit -m "test: activate synthetic PAD HLE from runtime decision"
```

If integration fails, debug systematically. Do not modify `Ps2PadHleService`, add partial activation, or add dispatcher hot-swap.

- [ ] **Step 6: Audit implementation scope before documentation**

Compare feature head to base `63f644a60d965eb32425dfa3a5b01aba6bc82712`.

Allowed production additions only:

```text
src/analysis/ps2_pad_activation.h
src/analysis/ps2_pad_activation_report.h
```

Allowed test changes only:

```text
tests/r5900_analysis_report_tests.cpp
tests/ps2_pad_runtime_report_tests.cpp
tests/r5900_block_dispatcher_guest_call_windows_tests.cpp
```

Require no changes to:

```text
CMakeLists.txt
src/runtime/ps2_pad_hle_service.h
src/runtime/ps2_pad_hle_service.cpp
src/platform/windows/win_main.cpp
src/recompiler/windows/r5900_block_dispatcher.h
src/recompiler/windows/r5900_block_dispatcher.cpp
src/recompiler/ps2_elf.h
src/recompiler/ps2_elf.cpp
```

Confirm every new PC literal is synthetic.

---

## Task 4: Validation Ledger and Exact-Head CI

**Files:**
- Create: `docs/validation/2026-09-08-ps2-pad-runtime-activation-v0.md`
- Modify: `docs/PROGRESS.md`

- [ ] **Step 1: Verify implementation-head Windows CI completely**

Require on exact Task 3 head:

```text
Configure                              PASS
Build                                  PASS
CTest                                  all tests PASS
r5900_analysis_report_tests            PASS
ps2_pad_runtime_report_tests           PASS
r5900_block_dispatcher_guest_call...   PASS
Frame pacing telemetry                 PASS
120 Hz pacing probe                    PASS
Analyzer package validation            PASS
Pacing package validation              PASS
```

Record exact SHA, workflow run number/ID and job ID.

- [ ] **Step 2: Create ledger with `PENDING_FINAL_EXACT_HEAD_CI`**

Document:

- base SHA and feature branch;
- spec and plan paths;
- Task 1 RED/GREEN/hardening SHAs and CI evidence;
- Task 2 RED/GREEN/hardening SHAs and CI evidence;
- Task 3 SHA and CI evidence;
- all three input-mismatch categories;
- evidence-PC provenance rejection;
- duplicate-PC rejection;
- no partial bindings;
- deterministic activation report;
- synthetic HLE lifecycle results;
- implementation-head CTest count;
- pacing/probe evidence;
- integrity audit;
- real Burnout activation remains `PENDING_EXTERNAL_VALIDATION`.

- [ ] **Step 3: Update `docs/PROGRESS.md` conservatively**

Set:

```text
Milestone: PS2 PAD Runtime Activation v0
Status: PENDING_FINAL_EXACT_HEAD_CI
Real Burnout PAD activation: PENDING_EXTERNAL_VALIDATION
```

Keep explicit non-claims for boot/render/audio/menu/gameplay.

- [ ] **Step 4: Commit exactly two docs and validate that exact SHA**

```bash
git add docs/PROGRESS.md docs/validation/2026-09-08-ps2-pad-runtime-activation-v0.md
git commit -m "docs: record PS2 PAD runtime activation validation"
```

Compare against implementation head and require exactly those two changed files. Run full Windows CI and require all gates PASS.

- [ ] **Step 5: Change only the same two docs to `CI_VALIDATED` after Step 4 passes**

Record successful pending-status documentation run and SHA. Commit:

```bash
git add docs/PROGRESS.md docs/validation/2026-09-08-ps2-pad-runtime-activation-v0.md
git commit -m "docs: mark PS2 PAD runtime activation CI validated"
```

Require exactly the same two files in the status-only diff.

- [ ] **Step 6: Run final exact-head Windows CI**

Require:

```text
Configure                    PASS
Build                        PASS
CTest                        all tests PASS
Frame pacing telemetry       PASS
120 Hz pacing probe          PASS
Analyzer package validation  PASS
Pacing package validation    PASS
```

Verify branch head still equals tested SHA.

Only then report:

```text
PS2 PAD Runtime Activation v0 = CI_VALIDATED
```

Do not create another commit only to record the final run ID.

---

## Completion Checklist

- [ ] Six cross-input-consistent, same-function evidence-backed `RuntimeConfirmed` nonzero distinct PCs produce `Ready`.
- [ ] `Trusted`, `Candidate`, and `Unresolved` confidence are preserved and may all be eligible.
- [ ] Discovery function mismatch is rejected as `InputMismatch`.
- [ ] Runtime function mismatch is rejected as `InputMismatch`.
- [ ] Runtime/discovery static-confidence mismatch is rejected as `InputMismatch`.
- [ ] `Unobserved`, `ObservedIncompatible`, and `RuntimeAmbiguous` are rejected.
- [ ] Missing and zero runtime PCs are rejected.
- [ ] Runtime PC absent from same-function discovery evidence is rejected as `PcNotInDiscoveryEvidence`.
- [ ] Duplicate selected PCs force global `NotReady` and no bindings.
- [ ] Incomplete set produces no partial `Ps2PadHleBindings`.
- [ ] `PAD_ACTIVATION_V0` is canonical and byte-deterministic.
- [ ] `InputMismatch` renders `input_mismatch`.
- [ ] `PcNotInDiscoveryEvidence` renders `pc_not_in_discovery_evidence`.
- [ ] Rejected functions always render `pc=none`.
- [ ] Bindings line appears only for `Ready + bindings`.
- [ ] Real `Ps2PadHleService` is constructed directly from `decision.bindings`.
- [ ] Synthetic init/open/get-state/read/close/end lifecycle is intercepted successfully.
- [ ] Fixed unknown PC is `NotHandled` by PAD HLE.
- [ ] `Ps2PadHleService` production behavior is unchanged.
- [ ] `R5900BlockDispatcher` production code is unchanged and gains no hot-swap API.
- [ ] `WinMain` is unchanged.
- [ ] `CMakeLists.txt` is unchanged.
- [ ] ELF/PT_LOAD validation is unchanged.
- [ ] No proprietary game data or Burnout-specific guest PC is added.
- [ ] Full Windows CI passes on implementation head.
- [ ] Full Windows CI passes on pending-status documentation head.
- [ ] Full Windows CI passes on exact final `CI_VALIDATED` head.
- [ ] Real Burnout 3 activation remains `PENDING_EXTERNAL_VALIDATION`.
