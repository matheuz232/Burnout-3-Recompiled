# PS2 PAD Binding Discovery v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add deterministic, analysis-only discovery of the six PS2 libpad entry points from lawful user-supplied ELF metadata and conservative public-semantics fingerprints, without hardcoded Burnout 3 addresses or automatic HLE activation.

**Architecture:** Keep `parse_ps2_elf()` and `Ps2MemoryMap::from_elf()` authoritative and unchanged. Add an optional ELF32 metadata reader, a pure PAD evidence/resolution layer, a conservative static fingerprint scanner over already-decoded executable code, and an optional `Burnout3Analyze --pad-bindings` report path. Static fingerprints may emit `Candidate` only; exact accepted ELF symbols may emit `Trusted`.

**Tech Stack:** C++20, CMake, MSVC/Visual Studio 2022 x64, existing R5900 decoder/control-flow/reachability code, Windows GitHub Actions CI.

**Spec:** `docs/superpowers/specs/2026-09-07-ps2-pad-binding-discovery-v0-design.md`

## Global Constraints

- The existing `parse_ps2_elf()` validation remains authoritative for loadability.
- Optional section/symbol parsing is analysis-only and cannot make an invalid ELF loadable.
- No Burnout 3 executable bytes, proprietary function hashes, extracted function bodies, or game-specific guest PCs may be committed.
- Synthetic ELF fixtures and public PS2SDK constants/metadata are permitted.
- Exact PAD symbol matching is case-sensitive and restricted to the canonical names plus one leading-underscore alias.
- Static fingerprints never self-promote to `Trusted`.
- Ambiguity is emitted explicitly as `pc=none`; the implementation never chooses among colliding candidates.
- Existing analyzer output remains byte-identical when `--pad-bindings` is absent.
- Full Windows CI must pass on the exact final documentation head before the milestone is marked `CI_VALIDATED`.

---

## File Structure

### New analysis units

- `src/analysis/elf32_metadata.h`
  - analysis-only ELF32 section/symbol metadata model and parser API.
- `src/analysis/elf32_metadata.cpp`
  - checked ELF32 section-header, string-table, `SHT_SYMTAB`, and `SHT_DYNSYM` parsing.
- `src/analysis/ps2_pad_binding_discovery.h`
  - PAD function/evidence/confidence/result types and symbol-evidence merge API.
- `src/analysis/ps2_pad_binding_discovery.cpp`
  - exact-name symbol resolution, executable-range checks, confidence merge rules, and orchestration.
- `src/analysis/ps2_pad_fingerprint.h`
  - public-semantic fingerprint feature and scanner API.
- `src/analysis/ps2_pad_fingerprint.cpp`
  - deterministic feature extraction/scoring from decoded executable functions.
- `src/analysis/ps2_pad_binding_report.h`
  - deterministic PAD report renderer API.
- `src/analysis/ps2_pad_binding_report.cpp`
  - stable text serialization only.

### New tests

- `tests/elf32_metadata_tests.cpp`
- `tests/ps2_pad_binding_discovery_tests.cpp`
- `tests/ps2_pad_fingerprint_tests.cpp`
- `tests/ps2_pad_binding_report_tests.cpp`

### Existing files modified

- `src/tools/burnout3_analyze_options.h`
- `src/tools/burnout3_analyze_options.cpp`
- `src/tools/burnout3_analyze_app.cpp`
- `tests/burnout3_analyze_options_tests.cpp`
- `tests/burnout3_analyze_app_tests.cpp`
- `CMakeLists.txt`
- `docs/ANALYSIS_TOOL.md`
- `docs/ANALYZE-USAGE.txt`
- `docs/PROGRESS.md`
- `docs/validation/2026-09-07-ps2-pad-binding-discovery-v0.md`

No production changes are planned for:

- `src/recompiler/ps2_elf.cpp`
- `src/recompiler/ps2_elf.h`
- `src/runtime/ps2_pad_hle_service.cpp`
- `src/runtime/ps2_pad_hle_service.h`
- `src/recompiler/windows/r5900_block_dispatcher.cpp`

---

### Task 1: Analysis-only ELF32 section and symbol metadata

**Files:**
- Create: `src/analysis/elf32_metadata.h`
- Create: `src/analysis/elf32_metadata.cpp`
- Create: `tests/elf32_metadata_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: raw `std::span<const std::uint8_t>` from an ELF that has already passed `parse_ps2_elf()`.
- Produces:

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

Constants used by this analysis unit:

```cpp
inline constexpr std::uint32_t kShtSymtab = 2u;
inline constexpr std::uint32_t kShtStrtab = 3u;
inline constexpr std::uint32_t kShtDynsym = 11u;
inline constexpr std::uint8_t kSttNotype = 0u;
inline constexpr std::uint8_t kSttFunc = 2u;
inline constexpr std::size_t kElf32SectionHeaderSize = 40u;
inline constexpr std::size_t kElf32SymbolSize = 16u;
```

- [ ] **Step 1: Add the RED metadata test target and synthetic fixtures**

Create a test executable that uses the repository's existing `expect()`/`fail()` style. The synthetic ELF helper must write ELF32 little-endian fields directly and must not contain game bytes.

Required test cases:

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

The valid synthetic symbol test must include these records:

```text
name=padRead value=0x00102000 size=0x40 type=STT_FUNC
name=_padEnd value=0x00102100 size=0x20 type=STT_NOTYPE
```

- [ ] **Step 2: Run RED**

Run:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target elf32_metadata_tests
ctest --test-dir build -C Release -R "^elf32_metadata_tests$" --output-on-failure
```

Expected RED: configure or build fails because `analysis/elf32_metadata.h` or `src/analysis/elf32_metadata.cpp` does not exist. A fixture-construction failure is not an acceptable RED.

- [ ] **Step 3: Implement the minimal checked metadata parser**

Implementation rules:

```cpp
[[nodiscard]] bool checked_add(std::size_t a, std::size_t b, std::size_t& out) noexcept;
[[nodiscard]] bool checked_mul(std::size_t a, std::size_t b, std::size_t& out) noexcept;
[[nodiscard]] std::uint16_t read_u16_le(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept;
[[nodiscard]] std::uint32_t read_u32_le(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept;
```

Metadata behavior is exact:

```text
e_shoff == 0 or e_shnum == 0 -> Absent
section-header table out of range -> Malformed
section header entry size != 40 -> Malformed
SHT_SYMTAB/SHT_DYNSYM sh_entsize != 16 -> Malformed
symbol section sh_link outside section table -> Malformed
linked section type != SHT_STRTAB -> Malformed
string-table or symbol-table span out of range -> Malformed
symbol st_name outside linked string table -> Malformed
missing NUL terminator before end of linked string table -> Malformed
otherwise -> Available, preserving symbol order
```

Do not validate PT_LOAD semantics here. The caller already has the authoritative loader result.

- [ ] **Step 4: Run GREEN and full regression subset**

Run:

```powershell
cmake --build build --config Release --target elf32_metadata_tests ps2_elf_tests ps2_elf_analysis_tests
ctest --test-dir build -C Release -R "^(elf32_metadata_tests|ps2_elf_tests|ps2_elf_analysis_tests)$" --output-on-failure
```

Expected: all selected tests PASS.

- [ ] **Step 5: Commit Task 1**

```bash
git add CMakeLists.txt src/analysis/elf32_metadata.h src/analysis/elf32_metadata.cpp tests/elf32_metadata_tests.cpp
git commit -m "feat: parse analysis-only ELF32 metadata"
```

Reviewer gate: confirm no change to `src/recompiler/ps2_elf.cpp` or `src/recompiler/ps2_elf.h`.

---

### Task 2: Exact PAD symbol evidence and deterministic resolution merge

**Files:**
- Create: `src/analysis/ps2_pad_binding_discovery.h`
- Create: `src/analysis/ps2_pad_binding_discovery.cpp`
- Create: `tests/ps2_pad_binding_discovery_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Elf32MetadataResult`, `recompiler::Ps2ElfImage`.
- Produces:

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

struct PadBindingDiscoveryResult {
    std::array<PadBindingResolution, 6> resolutions{};
    std::vector<std::string> diagnostics{};
};

[[nodiscard]] std::vector<PadBindingEvidence>
collect_ps2_pad_symbol_evidence(const Elf32MetadataResult& metadata,
                                const recompiler::Ps2ElfImage& image);

[[nodiscard]] PadBindingDiscoveryResult
resolve_ps2_pad_binding_evidence(std::span<const PadBindingEvidence> evidence,
                                 std::span<const std::string> diagnostics = {});

}
```

Accepted names are exactly:

```text
padInit _padInit
padPortOpen _padPortOpen
padGetState _padGetState
padRead _padRead
padPortClose _padPortClose
padEnd _padEnd
```

Eligibility rules:

```text
value == 0 -> reject
symbol type STT_FUNC -> accept name candidate
symbol type STT_NOTYPE -> accept only exact accepted name
other symbol type -> reject
PC must lie in [virtual_address, virtual_address + file_size) of PF_X PT_LOAD
non-executable matching symbol -> reject with diagnostic generated by orchestration task
```

- [ ] **Step 1: Write RED tests for all six exact-name mappings and merge rules**

Required cases:

```cpp
void test_each_canonical_pad_symbol_maps_to_expected_function();
void test_leading_underscore_aliases_are_accepted();
void test_wrong_case_and_prefix_suffix_names_are_rejected();
void test_zero_value_symbol_is_rejected();
void test_non_function_symbol_is_rejected();
void test_symbol_outside_executable_pt_load_is_rejected();
void test_duplicate_same_pc_symbol_is_trusted_once();
void test_duplicate_different_pc_symbols_are_unresolved();
void test_static_fingerprint_alone_is_candidate();
void test_symbol_plus_matching_fingerprint_is_trusted();
void test_symbol_plus_conflicting_fingerprint_keeps_symbol_trusted_and_reports_conflict();
void test_multiple_fingerprint_pcs_are_candidate_with_no_selected_pc();
```

For `ElfSymbol`, set `score=1000`. Static fingerprint scores remain the scanner-provided values.

- [ ] **Step 2: Run RED**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target ps2_pad_binding_discovery_tests
ctest --test-dir build -C Release -R "^ps2_pad_binding_discovery_tests$" --output-on-failure
```

Expected RED: missing `ps2_pad_binding_discovery` production API.

- [ ] **Step 3: Implement exact-name symbol collection and evidence merge**

Canonical function order is fixed by the enum declaration. Resolution rules must be implemented literally:

```text
0 symbol PCs + 0 fingerprint PCs -> Unresolved, guest_pc=null
0 symbol PCs + 1 fingerprint PC -> Candidate, guest_pc=fingerprint PC
0 symbol PCs + 2 or more fingerprint PCs -> Candidate, guest_pc=null
1 symbol PC -> Trusted, guest_pc=symbol PC
2 or more distinct symbol PCs -> Unresolved, guest_pc=null
1 symbol PC + conflicting fingerprints -> Trusted at symbol PC + diagnostic
```

Evidence must be sorted by:

```text
function enum
kind enum
Guest PC ascending
score ascending
```

Do not discard duplicate evidence records until after distinct-PC ambiguity has been calculated. Identical duplicate records may then be collapsed deterministically.

- [ ] **Step 4: Run GREEN and metadata regression**

```powershell
cmake --build build --config Release --target ps2_pad_binding_discovery_tests elf32_metadata_tests
ctest --test-dir build -C Release -R "^(ps2_pad_binding_discovery_tests|elf32_metadata_tests)$" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit Task 2**

```bash
git add CMakeLists.txt src/analysis/ps2_pad_binding_discovery.h src/analysis/ps2_pad_binding_discovery.cpp tests/ps2_pad_binding_discovery_tests.cpp
git commit -m "feat: resolve PS2 PAD symbol evidence"
```

Reviewer gate: verify no fuzzy name matching and no runtime/HLE activation.

---

### Task 3: Conservative public-semantics static fingerprint scanner

**Files:**
- Create: `src/analysis/ps2_pad_fingerprint.h`
- Create: `src/analysis/ps2_pad_fingerprint.cpp`
- Create: `tests/ps2_pad_fingerprint_tests.cpp`
- Modify: `src/analysis/ps2_pad_binding_discovery.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `runtime::Ps2MemoryMap`, `R5900ReachabilityGraph`.
- Produces:

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

Public constants permitted in production source:

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

#### Fixed v0 scoring table

Weights are fixed before testing against any proprietary ELF:

```text
PadInit:
  any PAD_BIND_RPC_ID match             +60
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
score < 100 -> no candidate emitted
score >= 100 -> Candidate may be emitted
no static score can produce Trusted
```

Feature extraction limits:

- Scan only basic blocks whose leader lies inside executable `PF_X` PT_LOAD file-backed memory.
- Scan only instructions already present in the existing reachability graph; do not expand reachability in this function.
- Recognize constants only through decoded immediate-bearing instructions already supported by the current R5900 decoder. If a feature cannot be proven by supported decoded instructions, leave the feature `false`.
- `copies_32_bytes` is true only when a synthetic/public-semantics fixture contains a decoded loop/count or copy-size immediate of exactly 32 associated with memory movement inside the candidate function. It must not be inferred from arbitrary occurrence of the value 32 alone.
- Candidate function PC is the basic-block/function leader used by the existing reachability representation; never manufacture a nearby address.

- [ ] **Step 1: Write RED scanner tests using synthetic R5900 fixtures only**

Required tests:

```cpp
void test_score_below_100_is_not_emitted();
void test_pad_init_public_rpc_features_reach_candidate_threshold();
void test_pad_port_open_alignment_and_bounds_reach_candidate_threshold();
void test_pad_get_state_requires_stable_plus_bounds_for_candidate();
void test_pad_read_requires_copy32_plus_bounds_for_candidate();
void test_pad_port_close_command_is_candidate();
void test_pad_end_command_is_candidate();
void test_multiple_candidates_for_same_function_are_preserved();
void test_candidate_evidence_is_never_trusted();
void test_scanner_ignores_non_executable_blocks();
```

Synthetic instruction words may encode only public R5900 instructions and the public constants listed above.

- [ ] **Step 2: Run RED**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target ps2_pad_fingerprint_tests
ctest --test-dir build -C Release -R "^ps2_pad_fingerprint_tests$" --output-on-failure
```

Expected RED: scanner API missing or threshold assertions fail with no candidates.

- [ ] **Step 3: Implement the feature extractor and fixed scorer**

Use small pure scoring functions:

```cpp
[[nodiscard]] std::uint32_t score_pad_init(const PadFingerprintFeatures& f) noexcept;
[[nodiscard]] std::uint32_t score_pad_port_open(const PadFingerprintFeatures& f) noexcept;
[[nodiscard]] std::uint32_t score_pad_get_state(const PadFingerprintFeatures& f) noexcept;
[[nodiscard]] std::uint32_t score_pad_read(const PadFingerprintFeatures& f) noexcept;
[[nodiscard]] std::uint32_t score_pad_port_close(const PadFingerprintFeatures& f) noexcept;
[[nodiscard]] std::uint32_t score_pad_end(const PadFingerprintFeatures& f) noexcept;
```

Each emitted `PadBindingEvidence` must set:

```text
kind=StaticFingerprint
score=<fixed score>
detail=<stable comma-separated feature names in declaration order>
```

Example deterministic detail string:

```text
alignment_mask_0x3f,port_bound_2,slot_bound_8
```

- [ ] **Step 4: Run GREEN and discovery regressions**

```powershell
cmake --build build --config Release --target ps2_pad_fingerprint_tests ps2_pad_binding_discovery_tests
ctest --test-dir build -C Release -R "^(ps2_pad_fingerprint_tests|ps2_pad_binding_discovery_tests)$" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit Task 3**

```bash
git add CMakeLists.txt src/analysis/ps2_pad_fingerprint.h src/analysis/ps2_pad_fingerprint.cpp src/analysis/ps2_pad_binding_discovery.cpp tests/ps2_pad_fingerprint_tests.cpp
git commit -m "feat: add conservative PS2 PAD fingerprints"
```

Reviewer gate: search the diff for game addresses, binary hashes, or copied proprietary instruction sequences. Reject the task if any are present.

---

### Task 4: Discovery orchestration and deterministic PAD binding report

**Files:**
- Modify: `src/analysis/ps2_pad_binding_discovery.h`
- Modify: `src/analysis/ps2_pad_binding_discovery.cpp`
- Create: `src/analysis/ps2_pad_binding_report.h`
- Create: `src/analysis/ps2_pad_binding_report.cpp`
- Create: `tests/ps2_pad_binding_report_tests.cpp`
- Modify: `tests/ps2_pad_binding_discovery_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces the final pure orchestration API:

```cpp
[[nodiscard]] PadBindingDiscoveryResult discover_ps2_pad_bindings(
    std::span<const std::uint8_t> elf_bytes,
    const recompiler::Ps2ElfImage& image,
    const runtime::Ps2MemoryMap& memory,
    const R5900ReachabilityGraph& graph);

[[nodiscard]] std::string
render_ps2_pad_binding_report(const PadBindingDiscoveryResult& result);
```

Orchestration order is fixed:

```text
parse optional ELF metadata
collect symbol evidence
scan static fingerprint candidates
convert candidates to evidence
merge all evidence
append metadata/fingerprint diagnostics
sort diagnostics lexicographically
return six resolutions in canonical enum order
```

Metadata `Absent` is not an error diagnostic. Metadata `Malformed` adds exactly one diagnostic beginning:

```text
metadata_malformed: 
```

- [ ] **Step 1: Write RED orchestration/report tests**

Required serialized output cases:

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

Additional required tests:

```cpp
void test_report_uses_lowercase_eight_digit_guest_pc();
void test_ambiguous_candidate_uses_pc_none_and_lists_all_evidence();
void test_diagnostics_are_sorted_and_stable();
void test_repeated_render_is_byte_identical();
void test_metadata_absent_is_not_reported_as_failure();
void test_metadata_malformed_is_diagnostic_only();
void test_symbol_fingerprint_conflict_is_visible();
```

- [ ] **Step 2: Run RED**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --target ps2_pad_binding_report_tests ps2_pad_binding_discovery_tests
ctest --test-dir build -C Release -R "^(ps2_pad_binding_report_tests|ps2_pad_binding_discovery_tests)$" --output-on-failure
```

Expected RED: missing renderer/orchestration API.

- [ ] **Step 3: Implement orchestration and renderer**

Renderer name mappings are exact:

```text
PadInit -> padInit
PadPortOpen -> padPortOpen
PadGetState -> padGetState
PadRead -> padRead
PadPortClose -> padPortClose
PadEnd -> padEnd
ElfSymbol -> elf_symbol
StaticFingerprint -> static_fingerprint
Unresolved -> unresolved
Candidate -> candidate
Trusted -> trusted
```

`max_score` is the maximum evidence score for the resolution, or zero when evidence is empty.

Diagnostics serialize as:

```text
PAD_BINDING_DIAGNOSTIC <diagnostic text>
```

Place diagnostic lines after all evidence lines and before `PAD_BINDINGS_END`.

- [ ] **Step 4: Run GREEN and complete analysis-layer subset**

```powershell
cmake --build build --config Release --target elf32_metadata_tests ps2_pad_binding_discovery_tests ps2_pad_fingerprint_tests ps2_pad_binding_report_tests ps2_elf_analysis_tests
ctest --test-dir build -C Release -R "^(elf32_metadata_tests|ps2_pad_binding_discovery_tests|ps2_pad_fingerprint_tests|ps2_pad_binding_report_tests|ps2_elf_analysis_tests)$" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit Task 4**

```bash
git add CMakeLists.txt src/analysis/ps2_pad_binding_discovery.h src/analysis/ps2_pad_binding_discovery.cpp src/analysis/ps2_pad_binding_report.h src/analysis/ps2_pad_binding_report.cpp tests/ps2_pad_binding_discovery_tests.cpp tests/ps2_pad_binding_report_tests.cpp
git commit -m "feat: report PS2 PAD binding discovery"
```

Reviewer gate: verify canonical function ordering and `pc=none` for every ambiguous resolution.

---

### Task 5: `Burnout3Analyze --pad-bindings`, documentation, and exact-head CI validation

**Files:**
- Modify: `src/tools/burnout3_analyze_options.h`
- Modify: `src/tools/burnout3_analyze_options.cpp`
- Modify: `src/tools/burnout3_analyze_app.cpp`
- Modify: `tests/burnout3_analyze_options_tests.cpp`
- Modify: `tests/burnout3_analyze_app_tests.cpp`
- Modify: `docs/ANALYSIS_TOOL.md`
- Modify: `docs/ANALYZE-USAGE.txt`
- Modify: `docs/PROGRESS.md`
- Create: `docs/validation/2026-09-07-ps2-pad-binding-discovery-v0.md`

**Interfaces:**

Modify options to:

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

CLI semantics:

```text
--pad-bindings may appear at most once
--pad-bindings takes no value
without --pad-bindings existing output is byte-identical
with --pad-bindings append exactly one blank line followed by PAD_BINDINGS_V0 section
```

- [ ] **Step 1: Write RED CLI and app integration tests**

Add option cases:

```cpp
void test_pad_bindings_flag_sets_option();
void test_duplicate_pad_bindings_is_duplicate_option_error();
```

Add analyzer app cases using synthetic ELF fixtures:

```cpp
void test_output_without_pad_bindings_is_exact_existing_report();
void test_pad_bindings_appends_deterministic_section();
void test_pad_bindings_with_stripped_elf_emits_unresolved_not_failure();
void test_pad_bindings_with_exact_symbol_emits_trusted_symbol();
```

Update usage expectation to include:

```text
[--pad-bindings]
```

- [ ] **Step 2: Run RED**

```powershell
cmake --build build --config Release --target burnout3_analyze_options_tests burnout3_analyze_app_tests
ctest --test-dir build -C Release -R "^(burnout3_analyze_options_tests|burnout3_analyze_app_tests)$" --output-on-failure
```

Expected RED: `--pad-bindings` is currently unknown or output lacks the PAD section.

- [ ] **Step 3: Implement CLI flag and app integration**

Parser state adds:

```cpp
bool saw_pad_bindings = false;
```

Flag branch semantics:

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

In `run_burnout3_analyze`, retain current normal analysis flow. Only when `options.pad_bindings` is true, parse/map/reachability for discovery using the same `max_blocks` and `follow_direct_calls` values, call `discover_ps2_pad_bindings`, render the PAD section, and append:

```text
normal report
blank line
PAD_BINDINGS_V0 section
```

If the authoritative ELF parse, memory map, or reachability step fails, return existing `AnalysisFailed`. Optional metadata `Absent`/`Malformed` does not change the run error.

- [ ] **Step 4: Run GREEN for analyzer integration**

```powershell
cmake --build build --config Release --target Burnout3Analyze burnout3_analyze_options_tests burnout3_analyze_app_tests
ctest --test-dir build -C Release -R "^(burnout3_analyze_options_tests|burnout3_analyze_app_tests|burnout3_analyze_help)$" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Run full Windows test suite before documentation**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DB3R_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\frame_pacer_windows_tests.exe
.\build\Release\Burnout3PacingProbe.exe --seconds 1
```

Expected:

```text
0 CTest failures
frame_pacer_windows_tests: PASS
TARGET_HZ 120
OVER_9MS 0
OVER_10MS 0
OVER_12MS 0
```

- [ ] **Step 6: Commit production integration**

```bash
git add src/tools/burnout3_analyze_options.h src/tools/burnout3_analyze_options.cpp src/tools/burnout3_analyze_app.cpp tests/burnout3_analyze_options_tests.cpp tests/burnout3_analyze_app_tests.cpp
git commit -m "feat: expose PS2 PAD binding discovery"
```

- [ ] **Step 7: Update documentation only after the production head is green**

`docs/ANALYSIS_TOOL.md` and `docs/ANALYZE-USAGE.txt` must document:

```text
--pad-bindings is opt-in
symbol evidence can be trusted
static fingerprint evidence is candidate-only
pc=none means unresolved or ambiguous
no HLE activation occurs in this milestone
```

`docs/PROGRESS.md` milestone row becomes `CI_VALIDATED` only after the final documentation-head CI passes.

Validation document must record:

```text
design spec path and head
implementation plan path and head
RED/ GREEN CI run numbers for Tasks 1-5
final exact documentation SHA
final Windows CI run number and job id
exact CTest pass count
pacing telemetry summary
pacing probe summary
known pre-existing warnings
explicit non-goals and next milestone B: PS2 PAD Runtime Confirmation v0
```

- [ ] **Step 8: Commit documentation atomically**

```bash
git add docs/ANALYSIS_TOOL.md docs/ANALYZE-USAGE.txt docs/PROGRESS.md docs/validation/2026-09-07-ps2-pad-binding-discovery-v0.md
git commit -m "docs: record PS2 PAD binding discovery validation"
```

- [ ] **Step 9: Verify exact documentation head in Windows CI**

The final workflow must check out exactly the documentation commit SHA. Required steps:

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

Read the completed job log and record the fresh exact CTest count and pacing numbers. Do not copy numbers from an earlier run.

- [ ] **Step 10: Final quality gate**

Compare the implementation base against the final head and verify:

```text
no proprietary bytes
no Burnout-specific guest PCs
no changes to Ps2PadHleService runtime activation
no changes weakening parse_ps2_elf validation
no automatic Trusted promotion from static fingerprints
no behavior/output change when --pad-bindings is absent
```

Only then mark `PS2 PAD Binding Discovery v0` as `CI_VALIDATED`.

---

## Required TDD/CI Evidence Summary

The execution record must contain one controlled RED and one GREEN for each behavioral task:

```text
Task 1 metadata parser      RED -> GREEN
Task 2 symbol/merge         RED -> GREEN
Task 3 fingerprint scanner  RED -> GREEN
Task 4 report/orchestration RED -> GREEN
Task 5 analyzer integration RED -> GREEN
final documentation head    fresh full Windows CI
```

A RED caused by malformed test infrastructure, invalid CMake syntax, or unrelated pre-existing failure does not count.

## Follow-on Boundary

After this plan is complete, the next approved milestone is **PS2 PAD Runtime Confirmation v0**. It may consume `PadBindingDiscoveryResult` candidates and observe guest calls/arguments, but it must be designed separately before implementation.