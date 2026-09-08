# PS2 PAD Binding Discovery v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add deterministic, analysis-only discovery of the six PS2 libpad entry points from lawful user-supplied ELF metadata and conservative public-semantics fingerprints, without hardcoded Burnout 3 addresses or automatic HLE activation.

**Architecture:** Keep `parse_ps2_elf()` and `Ps2MemoryMap::from_elf()` authoritative and unchanged. Add an optional ELF32 metadata reader, exact PAD symbol evidence, a deterministic function-view builder plus conservative static fingerprint scanner, evidence merge/reporting, and an opt-in `Burnout3Analyze --pad-bindings` path. Exact accepted ELF symbols may become `Trusted`; static fingerprints remain `Candidate` only.

**Tech Stack:** C++20, CMake, MSVC/Visual Studio 2022 x64, existing R5900 decoder/control-flow/reachability code, Windows GitHub Actions CI.

**Spec:** `docs/superpowers/specs/2026-09-07-ps2-pad-binding-discovery-v0-design.md`

## Global Constraints

- Existing `parse_ps2_elf()` validation remains authoritative for loadability.
- Optional section/symbol parsing is analysis-only and cannot make an invalid ELF loadable.
- No Burnout 3 executable bytes, proprietary hashes, extracted function bodies, or game-specific guest PCs may be committed.
- Synthetic ELF fixtures and public PS2SDK constants/metadata are permitted.
- PAD symbol matching is exact and case-sensitive.
- Static fingerprints never self-promote to `Trusted`.
- Ambiguity is represented by `guest_pc=null` and rendered as `pc=none`.
- Existing analyzer output remains byte-identical when `--pad-bindings` is absent.
- Full Windows CI must pass on the exact final documentation head before `CI_VALIDATED` is claimed.

---

## File Map

Create:

```text
src/analysis/elf32_metadata.h
src/analysis/elf32_metadata.cpp
src/analysis/ps2_pad_binding_discovery.h
src/analysis/ps2_pad_binding_discovery.cpp
src/analysis/ps2_pad_fingerprint.h
src/analysis/ps2_pad_fingerprint.cpp
src/analysis/ps2_pad_binding_report.h
src/analysis/ps2_pad_binding_report.cpp
tests/elf32_metadata_tests.cpp
tests/ps2_pad_binding_discovery_tests.cpp
tests/ps2_pad_fingerprint_tests.cpp
tests/ps2_pad_binding_report_tests.cpp
docs/validation/2026-09-07-ps2-pad-binding-discovery-v0.md
```

Modify:

```text
CMakeLists.txt
src/tools/burnout3_analyze_options.h
src/tools/burnout3_analyze_options.cpp
src/tools/burnout3_analyze_app.cpp
tests/burnout3_analyze_options_tests.cpp
tests/burnout3_analyze_app_tests.cpp
docs/ANALYSIS_TOOL.md
docs/ANALYZE-USAGE.txt
docs/PROGRESS.md
```

Do not modify loader/runtime activation files:

```text
src/recompiler/ps2_elf.cpp
src/recompiler/ps2_elf.h
src/runtime/ps2_pad_hle_service.cpp
src/runtime/ps2_pad_hle_service.h
src/recompiler/windows/r5900_block_dispatcher.cpp
```

---

### Task 1: Analysis-only ELF32 metadata

**Files:** create `src/analysis/elf32_metadata.h`, `src/analysis/elf32_metadata.cpp`, `tests/elf32_metadata_tests.cpp`; modify `CMakeLists.txt`.

**Produces:**

```cpp
namespace b3r::analysis {

enum class Elf32MetadataStatus : std::uint8_t {
    Available,
    Absent,
    Malformed,
};

struct Elf32Symbol {
    std::string name{};
    std::uint32_t value{};
    std::uint32_t size{};
    std::uint8_t binding{};
    std::uint8_t type{};
    std::uint16_t section_index{};
};

struct Elf32MetadataResult {
    Elf32MetadataStatus status{Elf32MetadataStatus::Absent};
    std::vector<Elf32Symbol> symbols{};
    std::string diagnostic{};
};

[[nodiscard]] Elf32MetadataResult
parse_elf32_metadata(std::span<const std::uint8_t> bytes);

}
```

Parser constants:

```cpp
inline constexpr std::uint32_t kShtSymtab = 2u;
inline constexpr std::uint32_t kShtStrtab = 3u;
inline constexpr std::uint32_t kShtDynsym = 11u;
inline constexpr std::uint8_t kSttNotype = 0u;
inline constexpr std::uint8_t kSttFunc = 2u;
inline constexpr std::size_t kElf32SectionHeaderSize = 40u;
inline constexpr std::size_t kElf32SymbolSize = 16u;
```

- [ ] **Step 1: Add RED tests and CMake target**

Use synthetic ELF32 little-endian fixtures and the repository's `fail()`/`expect()` test style. Required cases:

```cpp
void test_no_section_table_is_absent();
void test_valid_symtab_and_strtab();
void test_valid_dynsym_and_strtab();
void test_truncated_section_table_is_malformed();
void test_bad_section_name_index_is_malformed();
void test_bad_linked_string_table_is_malformed();
void test_bad_symbol_entry_size_is_malformed();
void test_section_count_multiplication_overflow_is_malformed();
void test_section_range_addition_overflow_is_malformed();
void test_duplicate_symbols_are_preserved();
```

Valid fixture records:

```text
padRead value=0x00102000 size=0x40 type=STT_FUNC
_padEnd value=0x00102100 size=0x20 type=STT_NOTYPE
```

- [ ] **Step 2: Run RED**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target elf32_metadata_tests
ctest --test-dir build -C Release -R "^elf32_metadata_tests$" --output-on-failure
```

Expected RED: missing metadata production files/API. Invalid synthetic fixture construction does not count.

- [ ] **Step 3: Implement checked parser**

Required helpers:

```cpp
[[nodiscard]] bool checked_add(std::size_t a, std::size_t b, std::size_t& out) noexcept;
[[nodiscard]] bool checked_mul(std::size_t a, std::size_t b, std::size_t& out) noexcept;
[[nodiscard]] std::uint16_t read_u16_le(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept;
[[nodiscard]] std::uint32_t read_u32_le(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept;
```

Exact outcomes:

```text
e_shoff == 0 or e_shnum == 0 -> Absent
section header entry size != 40 -> Malformed
section-header span overflow/out-of-range -> Malformed
e_shstrndx outside section count when section table exists -> Malformed
SHT_SYMTAB/SHT_DYNSYM sh_entsize != 16 -> Malformed
symbol sh_link outside section count -> Malformed
linked section type != SHT_STRTAB -> Malformed
symbol/string span overflow/out-of-range -> Malformed
st_name outside linked string table -> Malformed
symbol name missing NUL terminator -> Malformed
otherwise -> Available and preserve symbol order
```

Do not add PT_LOAD validation here.

- [ ] **Step 4: Run GREEN subset**

```powershell
cmake --build build --config Release --target elf32_metadata_tests ps2_elf_tests ps2_elf_analysis_tests
ctest --test-dir build -C Release -R "^(elf32_metadata_tests|ps2_elf_tests|ps2_elf_analysis_tests)$" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/analysis/elf32_metadata.h src/analysis/elf32_metadata.cpp tests/elf32_metadata_tests.cpp
git commit -m "feat: parse analysis-only ELF32 metadata"
```

Reviewer gate: loader files are unchanged.

---

### Task 2: Exact PAD symbol evidence and merge rules

**Files:** create `src/analysis/ps2_pad_binding_discovery.h`, `src/analysis/ps2_pad_binding_discovery.cpp`, `tests/ps2_pad_binding_discovery_tests.cpp`; modify `CMakeLists.txt`.

**Produces:**

```cpp
namespace b3r::analysis {

enum class PadBindingFunction : std::uint8_t {
    PadInit,
    PadPortOpen,
    PadGetState,
    PadRead,
    PadPortClose,
    PadEnd,
};

enum class PadBindingEvidenceKind : std::uint8_t {
    ElfSymbol,
    StaticFingerprint,
};

enum class PadBindingConfidence : std::uint8_t {
    Unresolved,
    Candidate,
    Trusted,
};

struct PadBindingEvidence {
    PadBindingFunction function{};
    PadBindingEvidenceKind kind{PadBindingEvidenceKind::ElfSymbol};
    std::uint32_t guest_pc{};
    std::uint32_t score{};
    std::string detail{};
};

struct PadBindingResolution {
    PadBindingFunction function{};
    PadBindingConfidence confidence{PadBindingConfidence::Unresolved};
    std::optional<std::uint32_t> guest_pc{};
    std::vector<PadBindingEvidence> evidence{};
};

struct PadSymbolEvidenceResult {
    std::vector<PadBindingEvidence> evidence{};
    std::vector<std::string> diagnostics{};
};

struct PadBindingDiscoveryResult {
    std::array<PadBindingResolution, 6> resolutions{};
    std::vector<std::string> diagnostics{};
};

[[nodiscard]] PadSymbolEvidenceResult
collect_ps2_pad_symbol_evidence(const Elf32MetadataResult& metadata,
                                const recompiler::Ps2ElfImage& image);

[[nodiscard]] PadBindingDiscoveryResult
resolve_ps2_pad_binding_evidence(std::span<const PadBindingEvidence> evidence,
                                 std::span<const std::string> diagnostics = {});

}
```

Accepted names, and only these names:

```text
padInit _padInit
padPortOpen _padPortOpen
padGetState _padGetState
padRead _padRead
padPortClose _padPortClose
padEnd _padEnd
```

Eligibility:

```text
value == 0 -> reject
STT_FUNC -> eligible when name is accepted
STT_NOTYPE -> eligible only when name is accepted
other type -> reject
PC must lie in file-backed range of PF_X PT_LOAD
accepted name outside executable PT_LOAD -> reject and emit diagnostic
```

- [ ] **Step 1: Write RED tests**

```cpp
void test_each_canonical_pad_symbol_maps_to_expected_function();
void test_leading_underscore_aliases_are_accepted();
void test_wrong_case_and_prefix_suffix_names_are_rejected();
void test_zero_value_symbol_is_rejected();
void test_non_function_symbol_is_rejected();
void test_non_executable_matching_symbol_emits_diagnostic();
void test_duplicate_same_pc_symbol_is_trusted_once();
void test_duplicate_different_pc_symbols_are_unresolved();
void test_static_fingerprint_alone_is_candidate();
void test_symbol_plus_matching_fingerprint_is_trusted();
void test_symbol_plus_conflicting_fingerprint_keeps_symbol_trusted_and_reports_conflict();
void test_multiple_fingerprint_pcs_are_candidate_with_no_selected_pc();
```

`ElfSymbol` evidence uses `score=1000` and `detail` equal to the exact symbol spelling.

- [ ] **Step 2: Run RED**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target ps2_pad_binding_discovery_tests
ctest --test-dir build -C Release -R "^ps2_pad_binding_discovery_tests$" --output-on-failure
```

Expected RED: missing discovery production API.

- [ ] **Step 3: Implement symbol collector and resolver**

Resolution table:

```text
0 symbol PCs + 0 fingerprint PCs -> Unresolved, guest_pc=null
0 symbol PCs + 1 fingerprint PC -> Candidate, guest_pc=fingerprint PC
0 symbol PCs + 2 or more fingerprint PCs -> Candidate, guest_pc=null
1 symbol PC -> Trusted, guest_pc=symbol PC
2 or more distinct symbol PCs -> Unresolved, guest_pc=null
1 symbol PC + conflicting fingerprint PCs -> Trusted at symbol PC + conflict diagnostic
```

Sort evidence by function enum, evidence kind, guest PC, score, detail. Collapse byte-identical duplicate evidence only after distinct-PC ambiguity is computed.

- [ ] **Step 4: Run GREEN subset**

```powershell
cmake --build build --config Release --target ps2_pad_binding_discovery_tests elf32_metadata_tests
ctest --test-dir build -C Release -R "^(ps2_pad_binding_discovery_tests|elf32_metadata_tests)$" --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/analysis/ps2_pad_binding_discovery.h src/analysis/ps2_pad_binding_discovery.cpp tests/ps2_pad_binding_discovery_tests.cpp
git commit -m "feat: resolve PS2 PAD symbol evidence"
```

Reviewer gate: no fuzzy matching and no HLE activation.

---

### Task 3: Public-semantics static fingerprint scanner

**Files:** create `src/analysis/ps2_pad_fingerprint.h`, `src/analysis/ps2_pad_fingerprint.cpp`, `tests/ps2_pad_fingerprint_tests.cpp`; modify `CMakeLists.txt`.

**Produces:**

```cpp
namespace b3r::analysis {

struct PadFingerprintFeatures {
    bool rpc_bind_new_1{};
    bool rpc_bind_new_2{};
    bool rpc_bind_old_1{};
    bool rpc_bind_old_2{};
    bool command_init{};
    bool command_open{};
    bool command_close{};
    bool command_end{};
    bool alignment_mask_0x3f{};
    bool port_bound_2{};
    bool slot_bound_8{};
    bool state_stable_6{};
    bool copies_32_bytes{};
    std::size_t direct_call_count{};
};

struct PadFingerprintCandidate {
    PadBindingFunction function{};
    std::uint32_t guest_pc{};
    std::uint32_t score{};
    PadFingerprintFeatures features{};
};

[[nodiscard]] std::vector<PadFingerprintCandidate>
scan_ps2_pad_fingerprints(const runtime::Ps2MemoryMap& memory,
                          const R5900ReachabilityGraph& graph);

[[nodiscard]] std::vector<PadBindingEvidence>
make_ps2_pad_fingerprint_evidence(std::span<const PadFingerprintCandidate> candidates);

}
```

Public constants:

```cpp
inline constexpr std::uint32_t kPadBindRpcId1New = 0x80000100u;
inline constexpr std::uint32_t kPadBindRpcId2New = 0x80000101u;
inline constexpr std::uint32_t kPadBindRpcId1Old = 0x8000010fu;
inline constexpr std::uint32_t kPadBindRpcId2Old = 0x8000011fu;
inline constexpr std::uint32_t kPadRpcCommandOpenNew = 0x01u;
inline constexpr std::uint32_t kPadRpcCommandCloseNew = 0x0eu;
inline constexpr std::uint32_t kPadRpcCommandEndNew = 0x0fu;
inline constexpr std::uint32_t kPadRpcCommandInit = 0x10u;
inline constexpr std::uint32_t kPadStateStable = 0x06u;
```

#### Deterministic candidate-function views

Build candidate roots from:

```text
graph.entry_pc
every non-indirect R5900ReachabilityCall target that is present in graph.blocks
```

Sort and deduplicate roots numerically. For each root, form one function view by BFS over blocks already in `graph.blocks`.

Follow only edge kinds:

```text
BranchTaken
BranchNotTaken
DirectJump
CallContinuation
Fallthrough
```

Never follow:

```text
DirectCall
IndirectCall
IndirectJump
```

When a traversed edge targets another candidate root different from the current root, stop at that boundary. Sort collected blocks by `start_pc`. Do not analyze any block not already in the reachability graph.

This definition is the only function-boundary heuristic in v0.

#### Exact feature extraction rules

Scan `block.instructions` and `block.delay_slot`.

Recognize 32-bit RPC IDs only from a same-register constant construction inside one basic block:

```text
LUI rt, hi16
followed before another write to rt by
ORI rt, rt, lo16
or ADDIU rt, rt, signed_lo16
```

Recognize small constants only through these decoded forms:

```text
ORI rt, zero, imm
ADDIU rt, zero, imm
```

Recognize bounds only through:

```text
SLTIU rt, rs, 2 -> port_bound_2
SLTIU rt, rs, 8 -> slot_bound_8
```

Recognize alignment only through:

```text
ANDI rt, rs, 0x003f -> alignment_mask_0x3f
```

Recognize `copies_32_bytes` only when all conditions are true inside one function view:

```text
a register is initialized to 32 by ORI/ADDIU from zero
that register is decremented by ADDIU using -1 or -4
there is a backward conditional branch in the same function view
at least one decoded load and one decoded store occur in the loop's block set
```

A bare immediate value 32 does not set the feature.

`direct_call_count` is the number of `R5900ReachabilityCall` records whose `source_block` belongs to the function view and whose call is non-indirect.

#### Fixed scores

```text
PadInit:
  first PAD_BIND_RPC_ID                 +60
  second distinct PAD_BIND_RPC_ID       +40
  command 0x10                          +50
  direct_call_count >= 2                +20

PadPortOpen:
  command 0x01                          +40
  mask 0x3f                             +70
  port bound 2                          +30
  slot bound 8                          +30

PadGetState:
  state constant 0x06                   +80
  port bound 2                          +20
  slot bound 8                          +20

PadRead:
  copies 32 bytes                       +80
  port bound 2                          +20
  slot bound 8                          +20

PadPortClose:
  command 0x0e                          +100
  port bound 2                          +20
  slot bound 8                          +20

PadEnd:
  command 0x0f                          +120
```

Threshold:

```text
score < 100 -> do not emit
score >= 100 -> emit Candidate
static evidence never produces Trusted
```

- [ ] **Step 1: Write RED tests using synthetic R5900 only**

```cpp
void test_function_views_stop_at_direct_call_roots();
void test_function_views_follow_branch_and_jump_edges();
void test_score_below_100_is_not_emitted();
void test_pad_init_public_rpc_features_reach_threshold();
void test_pad_port_open_alignment_and_bounds_reach_threshold();
void test_pad_get_state_requires_stable_plus_bounds();
void test_pad_read_requires_copy_loop_plus_bounds();
void test_pad_port_close_command_is_candidate();
void test_pad_end_command_is_candidate();
void test_multiple_candidates_for_same_function_are_preserved();
void test_non_executable_or_unreachable_blocks_are_not_scanned();
void test_bare_immediate_32_does_not_set_copy_feature();
```

Synthetic words may encode only public R5900 instructions and the constants listed above.

- [ ] **Step 2: Run RED**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target ps2_pad_fingerprint_tests
ctest --test-dir build -C Release -R "^ps2_pad_fingerprint_tests$" --output-on-failure
```

Expected RED: scanner API missing or no candidates emitted.

- [ ] **Step 3: Implement view builder, extractor, and pure scorers**

Required score helpers:

```cpp
[[nodiscard]] std::uint32_t score_pad_init(const PadFingerprintFeatures& f) noexcept;
[[nodiscard]] std::uint32_t score_pad_port_open(const PadFingerprintFeatures& f) noexcept;
[[nodiscard]] std::uint32_t score_pad_get_state(const PadFingerprintFeatures& f) noexcept;
[[nodiscard]] std::uint32_t score_pad_read(const PadFingerprintFeatures& f) noexcept;
[[nodiscard]] std::uint32_t score_pad_port_close(const PadFingerprintFeatures& f) noexcept;
[[nodiscard]] std::uint32_t score_pad_end(const PadFingerprintFeatures& f) noexcept;
```

Fingerprint evidence uses `kind=StaticFingerprint`, the exact score, and a comma-separated feature list in `PadFingerprintFeatures` declaration order.

- [ ] **Step 4: Run GREEN subset**

```powershell
cmake --build build --config Release --target ps2_pad_fingerprint_tests ps2_pad_binding_discovery_tests
ctest --test-dir build -C Release -R "^(ps2_pad_fingerprint_tests|ps2_pad_binding_discovery_tests)$" --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/analysis/ps2_pad_fingerprint.h src/analysis/ps2_pad_fingerprint.cpp tests/ps2_pad_fingerprint_tests.cpp
git commit -m "feat: add conservative PS2 PAD fingerprints"
```

Reviewer gate: diff contains no game addresses, proprietary hashes, or copied proprietary instruction sequences.

---

### Task 4: Discovery orchestration and deterministic report

**Files:** modify `src/analysis/ps2_pad_binding_discovery.h`, `src/analysis/ps2_pad_binding_discovery.cpp`, `tests/ps2_pad_binding_discovery_tests.cpp`; create `src/analysis/ps2_pad_binding_report.h`, `src/analysis/ps2_pad_binding_report.cpp`, `tests/ps2_pad_binding_report_tests.cpp`; modify `CMakeLists.txt`.

**Produces:**

```cpp
[[nodiscard]] PadBindingDiscoveryResult discover_ps2_pad_bindings(
    std::span<const std::uint8_t> elf_bytes,
    const recompiler::Ps2ElfImage& image,
    const runtime::Ps2MemoryMap& memory,
    const R5900ReachabilityGraph& graph);

[[nodiscard]] std::string
render_ps2_pad_binding_report(const PadBindingDiscoveryResult& result);
```

Orchestration order:

```text
parse optional metadata
collect symbol evidence + symbol diagnostics
scan fingerprints
convert fingerprints to evidence
merge evidence
append metadata diagnostic only when Malformed
sort diagnostics lexicographically
return six resolutions in enum order
```

- [ ] **Step 1: Write RED orchestration/report tests**

Exact minimal report fixture:

```text
PAD_BINDINGS_V0 1
PAD_BINDING function=padInit confidence=trusted pc=0x00102000 evidence_count=1 max_score=1000
PAD_BINDING_EVIDENCE function=padInit kind=elf_symbol pc=0x00102000 score=1000 detail=padInit
PAD_BINDING function=padPortOpen confidence=unresolved pc=none evidence_count=0 max_score=0
PAD_BINDING function=padGetState confidence=unresolved pc=none evidence_count=0 max_score=0
PAD_BINDING function=padRead confidence=unresolved pc=none evidence_count=0 max_score=0
PAD_BINDING function=padPortClose confidence=unresolved pc=none evidence_count=0 max_score=0
PAD_BINDING function=padEnd confidence=unresolved pc=none evidence_count=0 max_score=0
PAD_BINDINGS_END
```

Also test:

```cpp
void test_report_uses_lowercase_eight_digit_pc();
void test_ambiguous_candidate_uses_pc_none_and_lists_all_evidence();
void test_diagnostics_are_sorted();
void test_repeated_render_is_byte_identical();
void test_metadata_absent_is_nonfatal_and_silent();
void test_metadata_malformed_is_diagnostic_only();
void test_symbol_fingerprint_conflict_is_visible();
```

- [ ] **Step 2: Run RED**

```powershell
cmake --build build --config Release --target ps2_pad_binding_report_tests ps2_pad_binding_discovery_tests
ctest --test-dir build -C Release -R "^(ps2_pad_binding_report_tests|ps2_pad_binding_discovery_tests)$" --output-on-failure
```

- [ ] **Step 3: Implement orchestration and renderer**

Exact text mappings:

```text
PadInit=padInit
PadPortOpen=padPortOpen
PadGetState=padGetState
PadRead=padRead
PadPortClose=padPortClose
PadEnd=padEnd
ElfSymbol=elf_symbol
StaticFingerprint=static_fingerprint
Unresolved=unresolved
Candidate=candidate
Trusted=trusted
```

`max_score` is the maximum evidence score, or zero for no evidence. Diagnostics render after evidence lines as:

```text
PAD_BINDING_DIAGNOSTIC <text>
```

- [ ] **Step 4: Run GREEN analysis subset**

```powershell
cmake --build build --config Release --target elf32_metadata_tests ps2_pad_binding_discovery_tests ps2_pad_fingerprint_tests ps2_pad_binding_report_tests ps2_elf_analysis_tests
ctest --test-dir build -C Release -R "^(elf32_metadata_tests|ps2_pad_binding_discovery_tests|ps2_pad_fingerprint_tests|ps2_pad_binding_report_tests|ps2_elf_analysis_tests)$" --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/analysis/ps2_pad_binding_discovery.h src/analysis/ps2_pad_binding_discovery.cpp src/analysis/ps2_pad_binding_report.h src/analysis/ps2_pad_binding_report.cpp tests/ps2_pad_binding_discovery_tests.cpp tests/ps2_pad_binding_report_tests.cpp
git commit -m "feat: report PS2 PAD binding discovery"
```

Reviewer gate: ambiguous resolutions always render `pc=none`.

---

### Task 5: `Burnout3Analyze --pad-bindings` and final validation

**Files:** modify `src/tools/burnout3_analyze_options.h`, `src/tools/burnout3_analyze_options.cpp`, `src/tools/burnout3_analyze_app.cpp`, `tests/burnout3_analyze_options_tests.cpp`, `tests/burnout3_analyze_app_tests.cpp`, `docs/ANALYSIS_TOOL.md`, `docs/ANALYZE-USAGE.txt`, `docs/PROGRESS.md`; create validation document.

**Options interface:**

```cpp
struct Burnout3AnalyzeOptions {
    std::string elf_path{};
    std::optional<std::string> output_path{};
    std::size_t max_blocks{4096};
    bool follow_direct_calls{false};
    bool pad_bindings{false};
    bool show_help{false};
};
```

CLI rules:

```text
--pad-bindings takes no value
--pad-bindings may appear once
without flag, existing report is byte-identical
with flag, append one blank line and one PAD_BINDINGS_V0 section
```

- [ ] **Step 1: Write RED option/app tests**

```cpp
void test_pad_bindings_flag_sets_option();
void test_duplicate_pad_bindings_is_duplicate_option_error();
void test_output_without_pad_bindings_is_exact_existing_report();
void test_pad_bindings_appends_deterministic_section();
void test_pad_bindings_with_stripped_elf_emits_unresolved_not_failure();
void test_pad_bindings_with_exact_symbol_emits_trusted_symbol();
```

Usage must contain `[--pad-bindings]`.

- [ ] **Step 2: Run RED**

```powershell
cmake --build build --config Release --target burnout3_analyze_options_tests burnout3_analyze_app_tests
ctest --test-dir build -C Release -R "^(burnout3_analyze_options_tests|burnout3_analyze_app_tests)$" --output-on-failure
```

Expected RED: flag is unknown or PAD section absent.

- [ ] **Step 3: Implement parser flag**

Add `bool saw_pad_bindings = false;` and this branch:

```cpp
if (arg == "--pad-bindings") {
    if (saw_pad_bindings) {
        return fail(Burnout3AnalyzeOptionError::DuplicateOption,
                    "--pad-bindings may only be specified once");
    }
    options.pad_bindings = true;
    saw_pad_bindings = true;
    continue;
}
```

- [ ] **Step 4: Integrate discovery into app**

Keep the current normal report path. When `options.pad_bindings` is true, perform the same authoritative parse/map/reachability prerequisites using `options.max_blocks` and `options.follow_direct_calls`, then call `discover_ps2_pad_bindings()` and append `render_ps2_pad_binding_report()`.

Authoritative parse/map/reachability failure remains `Burnout3AnalyzeRunError::AnalysisFailed`. Metadata `Absent`/`Malformed` remains nonfatal discovery state.

- [ ] **Step 5: Run GREEN integration subset**

```powershell
cmake --build build --config Release --target Burnout3Analyze burnout3_analyze_options_tests burnout3_analyze_app_tests
ctest --test-dir build -C Release -R "^(burnout3_analyze_options_tests|burnout3_analyze_app_tests|burnout3_analyze_help)$" --output-on-failure
```

- [ ] **Step 6: Commit production integration**

```bash
git add src/tools/burnout3_analyze_options.h src/tools/burnout3_analyze_options.cpp src/tools/burnout3_analyze_app.cpp tests/burnout3_analyze_options_tests.cpp tests/burnout3_analyze_app_tests.cpp
git commit -m "feat: expose PS2 PAD binding discovery"
```

- [ ] **Step 7: Run complete pre-documentation verification**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\frame_pacer_windows_tests.exe
.\build\Release\Burnout3PacingProbe.exe --seconds 1
```

Required outcome:

```text
0 CTest failures
frame_pacer_windows_tests PASS
TARGET_HZ 120
OVER_9MS 0
OVER_10MS 0
OVER_12MS 0
```

- [ ] **Step 8: Update docs and validation record**

Document exactly:

```text
--pad-bindings is opt-in
ELF symbol evidence may be Trusted
static fingerprint evidence is Candidate-only
pc=none means unresolved or ambiguous
no runtime HLE activation occurs
next milestone is PS2 PAD Runtime Confirmation v0
```

Validation record must include spec head, plan head, RED/GREEN CI runs for Tasks 1-5, exact final SHA, final Windows CI run/job, exact CTest count, pacing numbers, and pre-existing warnings.

- [ ] **Step 9: Commit documentation atomically**

```bash
git add docs/ANALYSIS_TOOL.md docs/ANALYZE-USAGE.txt docs/PROGRESS.md docs/validation/2026-09-07-ps2-pad-binding-discovery-v0.md
git commit -m "docs: record PS2 PAD binding discovery validation"
```

- [ ] **Step 10: Exact-head Windows CI gate**

Require on the documentation SHA:

```text
Configure PASS
Build PASS
Test PASS
Frame pacing telemetry PASS
Pacing probe smoke PASS
Stage analyzer package PASS
Validate analyzer package PASS
Stage pacing probe package PASS
Validate pacing probe package PASS
```

Read the completed job log and record fresh CTest/pacing values from that SHA only.

- [ ] **Step 11: Final diff audit**

Verify:

```text
no proprietary bytes
no Burnout-specific guest PCs
no loader-validation weakening
no Ps2PadHleService auto-activation
no static Candidate promoted to Trusted
no output change without --pad-bindings
```

Only after this audit and exact-head CI may the milestone be marked `CI_VALIDATED`.

---

## Required Evidence Sequence

```text
Task 1 metadata              RED -> GREEN
Task 2 symbol/merge          RED -> GREEN
Task 3 fingerprint scanner   RED -> GREEN
Task 4 report/orchestration  RED -> GREEN
Task 5 analyzer integration  RED -> GREEN
final documentation head     fresh full Windows CI
```

A RED caused by invalid CMake syntax, broken test infrastructure, or unrelated pre-existing failure does not count.

## Follow-on Boundary

The next milestone after this plan is **PS2 PAD Runtime Confirmation v0**. It may consume candidate PCs and observe guest calls/arguments, but requires its own design approval before implementation.