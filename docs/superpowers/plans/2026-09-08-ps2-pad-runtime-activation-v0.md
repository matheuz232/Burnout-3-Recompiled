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
- Canonical function identity mismatches in discovery/runtime inputs are rejected and diagnosed.
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
- Reuse existing test targets and do not edit the large `CMakeLists.txt` in this milestone:
  - policy → `r5900_analysis_report_tests`
  - activation formatter → `ps2_pad_runtime_report_tests`
  - HLE integration → `r5900_block_dispatcher_guest_call_windows_tests`

---

## File Structure

### Create

- `src/analysis/ps2_pad_activation.h`
  - Pure policy/model only.
  - Owns activation enums, per-function/global decisions, provenance validation, duplicate-PC validation, diagnostics, and `make_ps2_pad_activation_decision()`.
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

### Fixed synthetic policy fixture

Use exactly these six PCs in portable policy/report tests:

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

Use this fixed confidence pattern:

```text
padInit       Trusted
padPortOpen   Candidate
padGetState   Unresolved
padRead       Candidate
padPortClose  Trusted
padEnd        Trusted
```

- [ ] **Step 1: Write the first RED policy test**

Add `#include "analysis/ps2_pad_activation.h"` and `<array>` to `tests/r5900_analysis_report_tests.cpp`.

Add helpers:

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
        auto& result = runtime.functions[i];
        result.function = static_cast<PadBindingFunction>(i);
        result.runtime_status = PadRuntimeConfirmationStatus::RuntimeConfirmed;
        result.guest_pc = pcs[i];
        result.calls_observed = 1u;
        result.compatible_calls = 1u;
    }
    return runtime;
}
```

Add:

```cpp
void test_pad_activation_ready_requires_six_evidence_backed_distinct_confirmations() {
    using namespace b3r::analysis;
    const auto discovery = activation_discovery(kActivationPcs);
    const auto runtime = activation_runtime(kActivationPcs);
    const auto decision = make_ps2_pad_activation_decision(discovery, runtime);

    expect(decision.readiness == PadActivationReadiness::Ready,
           "six evidence-backed confirmed distinct PCs must be activation-ready");
    expect(decision.bindings.has_value(),
           "Ready activation must materialize complete bindings");
    expect(decision.bindings->pad_init == kActivationPcs[0] &&
               decision.bindings->pad_port_open == kActivationPcs[1] &&
               decision.bindings->pad_get_state == kActivationPcs[2] &&
               decision.bindings->pad_read == kActivationPcs[3] &&
               decision.bindings->pad_port_close == kActivationPcs[4] &&
               decision.bindings->pad_end == kActivationPcs[5],
           "bindings must exactly match the six confirmed evidence PCs");

    for (const auto& function : decision.functions) {
        expect(function.eligibility == PadActivationEligibility::Eligible,
               "all six functions must be individually eligible");
        expect(function.reason == PadActivationReason::EligibleRuntimeConfirmed,
               "eligible function must expose runtime-confirmed reason");
    }
}
```

Call the new test from `main()`.

- [ ] **Step 2: Commit RED and verify Windows CI fails for missing activation API**

```bash
git add tests/r5900_analysis_report_tests.cpp
git commit -m "test: define PS2 PAD activation policy"
```

Expected CI state:

```text
Configure PASS
Build FAIL
cause: analysis/ps2_pad_activation.h or make_ps2_pad_activation_decision missing
```

Record run ID and job ID.

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

Define the public enums/structs exactly as listed above.

Inside `ps2_pad_activation_detail`, define exactly one private PC formatter; do not include a report header from this policy unit:

```cpp
[[nodiscard]] inline std::string format_activation_pc(std::uint32_t pc) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << pc;
    return out.str();
}
```

Implement per-function classification in canonical index order. For each index:

1. `expected = static_cast<PadBindingFunction>(i)`.
2. Copy static confidence and runtime status to output.
3. If discovery or runtime slot function != expected:
   - reject;
   - reason `CanonicalFunctionMismatch`;
   - clear output `guest_pc`;
   - add `canonical_function_mismatch function=<canonical-name>`.
4. Map non-confirmed runtime states directly to rejection reasons.
5. `RuntimeConfirmed + guest_pc absent` → `MissingGuestPc`.
6. `RuntimeConfirmed + guest_pc == 0` → `ZeroGuestPc`.
7. Search `discovery.resolutions[i].evidence` for an item with both:
   - `evidence.function == expected`
   - `evidence.guest_pc == selected_pc`
8. If no exact same-function evidence item exists:
   - reject;
   - reason `RuntimePcNotEvidenceBacked`;
   - clear output PC;
   - diagnostic `runtime_pc_not_evidence_backed function=<canonical-name>`.
9. Otherwise set `Eligible`, `EligibleRuntimeConfirmed`, and selected PC.

After per-function classification:

```cpp
const auto eligible_count = std::count_if(
    decision.functions.begin(), decision.functions.end(),
    [](const PadActivationFunctionDecision& item) {
        return item.eligibility == PadActivationEligibility::Eligible;
    });
```

If `eligible_count != 6`, add `incomplete_activation_set`, sort/dedup diagnostics, return `NotReady` with `bindings == nullopt`.

If 6/6 eligible, copy the six selected PCs into a local array, sort a copy, and detect duplicate groups. For each duplicated numerical value add exactly one diagnostic:

```text
activation guest PC 0x???????? is selected by multiple PAD functions
```

Use `format_activation_pc()` for the value. If any duplicate exists, return `NotReady` with `bindings == nullopt`.

Only when all six PCs are eligible, nonzero, evidence-backed, canonical and pairwise-distinct:

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

Sort/dedup diagnostics before every return.

- [ ] **Step 4: Commit GREEN and require full Windows CI PASS**

```bash
git add src/analysis/ps2_pad_activation.h
git commit -m "feat: add atomic PS2 PAD activation policy"
```

Do not start hardening until the complete Windows workflow is green.

- [ ] **Step 5: Add the full policy hardening matrix**

Add and call these exact tests:

```cpp
void test_pad_activation_rejects_nonconfirmed_states();
void test_pad_activation_rejects_missing_and_zero_pc();
void test_pad_activation_accepts_all_static_confidence_levels();
void test_pad_activation_requires_same_function_evidence_provenance();
void test_pad_activation_rejects_canonical_identity_mismatch();
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
confirmed PC absent from same-function evidence -> RuntimePcNotEvidenceBacked
same numerical PC present only under another function's evidence -> RuntimePcNotEvidenceBacked
mismatched discovery slot identity -> CanonicalFunctionMismatch
mismatched runtime slot identity -> CanonicalFunctionMismatch
two functions select same PC -> both individually Eligible, global NotReady, no bindings
three functions select same PC -> exactly one duplicate-group diagnostic
5/6 eligible -> NotReady, no bindings, incomplete_activation_set
same inputs twice -> same readiness, functions, bindings, diagnostics
source discovery/runtime objects retain original confidence/status/PC/evidence after decision creation
```

- [ ] **Step 6: Commit hardening and require full Windows CI PASS**

```bash
git add tests/r5900_analysis_report_tests.cpp
git commit -m "test: harden PS2 PAD activation policy"
```

Record exact Task 1 hardened SHA and CI evidence.

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

Include:

```cpp
#include "analysis/ps2_pad_activation_report.h"
```

Reuse the same six fixed PCs and confidence pattern from Task 1. Build the ready decision by calling the real Task 1 policy.

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

Also assert rendering the same decision twice is byte-identical.

- [ ] **Step 2: Commit RED and verify Windows CI fails for missing formatter**

```bash
git add tests/ps2_pad_runtime_report_tests.cpp
git commit -m "test: define PS2 PAD activation report"
```

Expected CI:

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

Define exact names:

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
CanonicalFunctionMismatch   canonical_function_mismatch
RuntimePcNotEvidenceBacked  runtime_pc_not_evidence_backed
```

Use existing canonical helpers:

```cpp
ps2_pad_binding_detail::function_name(...)
ps2_pad_binding_report_detail::confidence_name(...)
ps2_pad_binding_report_detail::format_pc(...)
ps2_pad_runtime_report_detail::status_name(...)
```

Render:

1. header with readiness, actual eligible count, `required=6`;
2. exactly six function lines in canonical enum order;
3. `pc=<hex>` only if `eligibility == Eligible && guest_pc.has_value()`; otherwise `pc=none`;
4. `PAD_ACTIVATION_BINDINGS` only if `readiness == Ready && bindings.has_value()`;
5. a sorted/deduplicated copy of diagnostics;
6. exactly one `PAD_ACTIVATION_END\n` terminator.

Do not render scores, arguments, register dumps, RAM bytes or code bytes.

- [ ] **Step 4: Commit GREEN and require full Windows CI PASS**

```bash
git add src/analysis/ps2_pad_activation_report.h
git commit -m "feat: add deterministic PS2 PAD activation report"
```

- [ ] **Step 5: Harden formatter against inconsistent copied decisions**

Add exact tests for:

```text
Rejected function carrying guest_pc -> pc=none
NotReady decision carrying bindings -> no PAD_ACTIVATION_BINDINGS
Ready decision with bindings reset -> no PAD_ACTIVATION_BINDINGS
unsorted duplicate diagnostics -> sorted and deduplicated
exactly six PAD_ACTIVATION function= lines
canonical order padInit/padPortOpen/padGetState/padRead/padPortClose/padEnd
no "score=", "args=", "bytes=", "ram=", "code="
repeat render byte-identical
```

- [ ] **Step 6: Commit Task 2 hardening and require full Windows CI PASS**

```bash
git add tests/ps2_pad_runtime_report_tests.cpp
git commit -m "test: harden PS2 PAD activation report"
```

Record exact Task 2 hardened SHA and CI evidence.

---

## Task 3: Synthetic Activation Through the Real PAD HLE Service

**Files:**
- Modify/Test: `tests/r5900_block_dispatcher_guest_call_windows_tests.cpp`
- Production code changes: none.

**Consumes:**
- `make_ps2_pad_activation_decision()`
- `decision.bindings`
- existing `runtime::Ps2PadHleService`
- existing `recompiler::R5900BlockDispatcher`

### Fixed integration PCs

Use exactly these six synthetic binding PCs:

```cpp
constexpr std::array<std::uint32_t, 6> kSyntheticActivePadPcs{
    0x00100100u,
    0x00100120u,
    0x00100140u,
    0x00100160u,
    0x00100180u,
    0x001001a0u,
};
```

Use exactly this unknown PC for the negative service check:

```cpp
constexpr std::uint32_t kUnknownPadPc = 0x001001c0u;
```

- [ ] **Step 1: Add activation includes and fixture helpers**

Add:

```cpp
#include "analysis/ps2_pad_activation.h"
#include "input/ps2_pad_report.h"
#include "runtime/ps2_pad_hle_service.h"

#include <array>
```

Build discovery/runtime results with the fixed PCs. Every runtime result is `RuntimeConfirmed`; every selected PC exists as same-function discovery evidence. Use the fixed mixed confidence pattern from Task 1.

Then:

```cpp
const auto decision = make_ps2_pad_activation_decision(discovery, runtime);
expect(decision.readiness == PadActivationReadiness::Ready,
       "synthetic six-function PAD activation must be Ready");
expect(decision.bindings.has_value(),
       "Ready activation must supply bindings");
```

The test must not construct any second independent `Ps2PadHleBindings` literal.

- [ ] **Step 2: Construct the real PAD service from `decision.bindings`**

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

No observer→HLE swap occurs inside this dispatcher. This dispatcher starts already configured for activation phase B.

- [ ] **Step 3: Execute lifecycle calls through the dispatcher using the exact decision PCs**

For each call, set `$ra` to one fixed synthetic unsupported return PC backed by the ELF fixture so the HLE call is handled first and dispatch then stops deterministically after resuming.

Call sequence and assertions:

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
  complete 256-byte span backed by EE RAM
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
  complete 32-byte destination backed by EE RAM
  guest_calls_handled == 1
  v0 == 32
  byte[0] == 0x00
  byte[1] == 0x79
  byte[2] == 0xef
  byte[3] == 0xff
  byte[4] == 0x56
  byte[5] == 0x78
  byte[6] == 0x12
  byte[7] == 0x34
  byte[8..31] == 0

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

- [ ] **Step 4: Prove the fixed unknown PC is not handled by the PAD service**

Use the service directly for this negative boundary; do not create a second dispatcher path for it:

```cpp
R5900IrExecutionState unknown_state{};
const auto unknown = service.try_handle(
    R5900GuestCallRequest{kUnknownPadPc}, unknown_state, memory);
expect(unknown.status == R5900GuestCallStatus::NotHandled,
       "unbound synthetic PC must not be handled by activated PAD HLE");
```

Also assert `kUnknownPadPc` differs from all six `decision.bindings` fields.

- [ ] **Step 5: Commit Task 3 and require full Windows CI PASS**

```bash
git add tests/r5900_block_dispatcher_guest_call_windows_tests.cpp
git commit -m "test: activate synthetic PAD HLE from runtime decision"
```

If the integration fails, debug the fixture/production boundary systematically. Do not change `Ps2PadHleService`, add partial activation, or add dispatcher hot-swap to make it pass.

- [ ] **Step 6: Perform the implementation-scope audit**

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

Inspect every added PC literal and confirm it belongs only to synthetic fixtures.

---

## Task 4: Validation Ledger and Exact-Head CI

**Files:**
- Create: `docs/validation/2026-09-08-ps2-pad-runtime-activation-v0.md`
- Modify: `docs/PROGRESS.md`

- [ ] **Step 1: Verify implementation-head Windows CI completely**

On the exact Task 3 head require:

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

Record exact implementation SHA, workflow run number/ID and job ID.

- [ ] **Step 2: Create validation ledger with `PENDING_FINAL_EXACT_HEAD_CI`**

Document:

- base SHA;
- feature branch;
- spec and plan paths;
- Task 1 RED/GREEN/hardening SHAs and CI evidence;
- Task 2 RED/GREEN/hardening SHAs and CI evidence;
- Task 3 SHA and CI evidence;
- evidence-provenance rejection;
- canonical identity mismatch rejection;
- duplicate-PC rejection;
- no partial bindings;
- deterministic activation report;
- synthetic HLE lifecycle results;
- CTest count;
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

Keep explicit non-claims that boot/render/audio/menu/gameplay are not implemented.

- [ ] **Step 4: Commit exactly the two documentation files and validate that exact SHA**

```bash
git add docs/PROGRESS.md docs/validation/2026-09-08-ps2-pad-runtime-activation-v0.md
git commit -m "docs: record PS2 PAD runtime activation validation"
```

Compare against implementation head and require exactly two changed files. Run full Windows CI on this exact documentation SHA and require all gates PASS.

- [ ] **Step 5: Change only the same two documents to `CI_VALIDATED` after Step 4 passes**

Record the successful pending-status documentation run and SHA in the ledger. Commit:

```bash
git add docs/PROGRESS.md docs/validation/2026-09-08-ps2-pad-runtime-activation-v0.md
git commit -m "docs: mark PS2 PAD runtime activation CI validated"
```

Compare against the prior documentation head and require exactly those two files changed.

- [ ] **Step 6: Run final exact-head Windows CI**

On the exact status commit require:

```text
Configure                    PASS
Build                        PASS
CTest                        all tests PASS
Frame pacing telemetry       PASS
120 Hz pacing probe          PASS
Analyzer package validation  PASS
Pacing package validation    PASS
```

After completion, verify the branch head still equals the tested SHA.

Only then report:

```text
PS2 PAD Runtime Activation v0 = CI_VALIDATED
```

Do not make another commit merely to record the final run ID, because that would create a new unvalidated head.

---

## Completion Checklist

- [ ] Six same-function evidence-backed `RuntimeConfirmed` nonzero distinct PCs produce `Ready`.
- [ ] `Trusted`, `Candidate`, and `Unresolved` confidence are preserved and may all be eligible.
- [ ] `Unobserved`, `ObservedIncompatible`, and `RuntimeAmbiguous` are rejected.
- [ ] Missing and zero runtime PCs are rejected.
- [ ] Runtime PC absent from same-function discovery evidence is rejected.
- [ ] Canonical discovery/runtime function mismatch is rejected and diagnosed.
- [ ] Duplicate selected PCs force global `NotReady` and no bindings.
- [ ] Incomplete set produces no partial `Ps2PadHleBindings`.
- [ ] `PAD_ACTIVATION_V0` is canonical and byte-deterministic.
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
- [ ] No proprietary game data or Burnout-specific guest PC was added.
- [ ] Full Windows CI passes on implementation head.
- [ ] Full Windows CI passes on pending-status documentation head.
- [ ] Full Windows CI passes on exact final `CI_VALIDATED` head.
- [ ] Real Burnout 3 activation remains `PENDING_EXTERNAL_VALIDATION`.
