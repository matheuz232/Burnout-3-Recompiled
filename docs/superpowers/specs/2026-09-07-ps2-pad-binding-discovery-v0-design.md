# PS2 PAD Binding Discovery v0 Design

Date: 2026-09-07
Repository: `matheuz232/Burnout-3-Recompiled`
Base: `98ece10a49de54ed867e2778661d357c35ec4308`
Milestone: `PS2 PAD Binding Discovery v0`

## 1. Purpose

`PS2 PAD Guest Bridge v0` already provides a reusable `Ps2PadHleService` and a generic `IR5900GuestCallService`, but the runtime still requires explicit `Ps2PadHleBindings` containing the guest entry-point PCs for `padInit`, `padPortOpen`, `padGetState`, `padRead`, `padPortClose`, and `padEnd`.

This milestone adds an analysis-only discovery layer that can inspect a complete lawful PS2 ELF and produce evidence-backed PAD binding candidates without hardcoding Burnout 3 addresses, committing proprietary executable bytes, or weakening the production ELF loader.

The v0 milestone ends at discovery/reporting. It does not automatically activate HLE bindings in runtime. Runtime confirmation and automatic activation are separate milestones.

## 2. Architectural position

The discovery path is separate from execution:

```text
complete user ELF bytes
        |
        v
existing strict parse_ps2_elf()
        |
        +--------------------+
        |                    |
        v                    v
existing PT_LOAD view   optional ELF metadata view
        |                    |
        v                    v
R5900 reachability      symbol-table evidence
        |                    |
        +----------+---------+
                   |
                   v
        PAD static candidate scanner
                   |
                   v
        PadBindingDiscoveryResult
                   |
                   v
   text / machine-readable diagnostic report
```

`Ps2PadHleService` is not changed by this milestone.

## 3. Non-negotiable safety and correctness constraints

- The existing `parse_ps2_elf()` validation remains authoritative for loadability.
- Section/symbol parsing is analysis-only and cannot make an invalid ELF loadable.
- Missing or malformed optional section metadata must never weaken PT_LOAD bounds checks.
- No Burnout 3 executable bytes, extracted function bodies, signatures made from proprietary bytes, or copyrighted binary payloads may be committed.
- No Burnout 3 guest PC may be invented or hardcoded in production code or tests.
- Synthetic ELF fixtures and public PS2SDK-derived constants/metadata are allowed.
- Static heuristics alone never activate HLE.
- Ambiguity must be reported explicitly rather than resolved by guessing.

## 4. Evidence model

Each of the six PAD functions is represented independently.

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
    PadBindingEvidenceKind kind{};
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

}
```

Exact type names may be adjusted for repository consistency; semantics are fixed.

`PadBindingResolution::guest_pc` means one uniquely selected PC. It is empty whenever evidence is absent or ambiguous. Individual candidate PCs remain available in `evidence`.

### 4.1 Confidence rules

For v0:

- one unique exact accepted ELF symbol PC -> `Trusted`, with that `guest_pc` selected;
- one unique static-fingerprint PC and no exact symbol -> `Candidate`, with that `guest_pc` selected;
- no evidence -> `Unresolved`, `guest_pc = nullopt`;
- multiple different exact symbol PCs for the same logical PAD function -> `Unresolved`, `guest_pc = nullopt`, with ambiguity diagnostic;
- multiple different static-fingerprint PCs and no exact symbol -> `Candidate`, `guest_pc = nullopt`, with ambiguity diagnostic;
- exact symbol plus fingerprint at the same PC -> `Trusted`, selecting the symbol PC and retaining both evidence records;
- exact symbol plus conflicting fingerprint elsewhere -> exact symbol remains `Trusted`; the symbol PC is selected and the conflict is reported;
- fingerprint collisions are never promoted to `Trusted` in v0.

`ElfSymbol` trust is rule-based rather than score-based; its evidence score is always `0`. `StaticFingerprint` uses the integer score defined below.

Runtime trace confirmation is intentionally not part of this enum in v0; it belongs to `PAD Runtime Confirmation v0`.

## 5. Analysis-only ELF metadata parser

The current `Ps2ElfImage` intentionally exposes only entry point and PT_LOAD segments. V0 must not expand loader semantics merely to discover optional symbols.

Add a separate analysis unit, for example:

- `src/analysis/elf32_metadata.h`
- `src/analysis/elf32_metadata.cpp`

It consumes the original ELF byte span after `parse_ps2_elf()` has already succeeded.

The parser may inspect:

- ELF32 section-header table;
- section-name string table;
- `SHT_SYMTAB` and `SHT_DYNSYM` when present;
- linked symbol string table;
- symbol value, size, binding, type, and section index.

It must use checked integer arithmetic for every `offset + count * entsize` calculation.

Optional metadata outcomes are distinct from loader outcomes:

```cpp
enum class Elf32MetadataStatus {
    Available,
    Absent,
    Malformed,
};
```

V0 interprets `e_shoff == 0` or `e_shnum == 0` as `Absent`. ELF extended section numbering / `SHN_XINDEX` is not required in v0; encountering metadata that requires unsupported extended numbering produces `Malformed` plus a deterministic diagnostic rather than guessing.

`Absent` is normal for stripped retail ELFs. `Malformed` produces a discovery diagnostic but does not retroactively change the already-established loader result.

Only symbols with a nonzero guest value and a function-compatible type (`STT_FUNC`, or `STT_NOTYPE` when the exact name is present) are eligible for PAD binding evidence.

## 6. Accepted PAD symbol names

V0 accepts only an explicit canonical alias set. No fuzzy name matching.

Canonical logical functions:

```text
padInit
padPortOpen
padGetState
padRead
padPortClose
padEnd
```

Accepted symbol spellings are:

```text
padInit        _padInit
padPortOpen    _padPortOpen
padGetState    _padGetState
padRead        _padRead
padPortClose   _padPortClose
padEnd         _padEnd
```

Comparison is exact and case-sensitive.

A symbol PC must lie inside an executable PT_LOAD segment (`PF_X`) before it can become binding evidence. A matching name outside executable load memory is rejected with a diagnostic.

## 7. Static fingerprint candidate scanner

The scanner is deliberately conservative. It does not match proprietary Burnout bytes and it does not use exact binary signatures extracted from the game.

It operates on decoded R5900 instructions and existing control-flow/reachability metadata, using public PS2SDK behavior as semantic anchors.

Public PS2SDK libpad provides stable semantic constants including:

```text
PAD_BIND_RPC_ID1_NEW = 0x80000100
PAD_BIND_RPC_ID2_NEW = 0x80000101
PAD_BIND_RPC_ID1_OLD = 0x8000010f
PAD_BIND_RPC_ID2_OLD = 0x8000011f
PAD_RPCCMD_OPEN_NEW  = 0x01
PAD_RPCCMD_CLOSE_NEW = 0x0e
PAD_RPCCMD_END_NEW   = 0x0f
PAD_RPCCMD_INIT      = 0x10
```

The implementation may encode these public constants and structural relationships, but not compiled bytes from Burnout 3.

### 7.1 V0 fingerprint philosophy

Fingerprinting identifies candidates by a weighted set of semantic features rather than a byte-for-byte signature.

Examples of admissible features:

- construction/use of one or more public PAD RPC IDs or command constants;
- direct-call topology within a candidate function;
- argument-validation shape visible in decoded control flow;
- use of 64-byte alignment mask `0x3f` in an open-like function;
- port/slot range checks consistent with public libpad behavior;
- nearby relationship between several PAD-like candidate functions;
- return-shape and small constant behavior compatible with the public API.

Examples of inadmissible evidence:

- a raw sequence copied from the user's Burnout ELF;
- a proprietary function hash;
- a hardcoded Burnout-specific address;
- a private SDK binary signature.

### 7.2 Candidate score

Use an integer score for diagnostics, not probability.

```text
0       no candidate
1..99   weak evidence, do not emit as PAD candidate
100+    emit Candidate evidence
```

The implementation plan must assign explicit integer weights to every v0 fingerprint feature before implementation begins. Those weights are then fixed by synthetic tests. They must be derived from public PS2SDK semantics, not tuned against proprietary Burnout bytes.

A function cannot become `Trusted` solely from its static score, regardless of score magnitude.

## 8. Discovery orchestration

Add a pure analysis API, for example:

```cpp
[[nodiscard]] PadBindingDiscoveryResult discover_ps2_pad_bindings(
    std::span<const std::uint8_t> elf_bytes,
    const recompiler::Ps2ElfImage& image,
    const runtime::Ps2MemoryMap& memory,
    const R5900ReachabilityGraph& graph);
```

The orchestration order is deterministic:

1. validate that the already-parsed ELF image is available;
2. parse optional ELF metadata;
3. collect exact symbol evidence;
4. collect static fingerprint evidence;
5. merge evidence per logical PAD function;
6. apply the fixed confidence rules;
7. return resolutions and diagnostics in canonical function order.

The function does not mutate ELF bytes, memory, graph, global state, or runtime bindings.

## 9. Report format

Extend `Burnout3Analyze` with an optional PAD-binding report mode rather than creating another executable in v0.

Recommended CLI switch:

```text
--pad-bindings
```

When selected, append a stable machine-readable section to the normal analysis report:

```text
PAD_BINDINGS_V0 1
PAD_BINDING function=padInit confidence=trusted pc=0x00123456
PAD_BINDING_EVIDENCE function=padInit kind=elf_symbol pc=0x00123456 score=0
PAD_BINDING function=padPortOpen confidence=candidate pc=0x00124500
PAD_BINDING_EVIDENCE function=padPortOpen kind=static_fingerprint pc=0x00124500 score=160
PAD_BINDING function=padRead confidence=candidate pc=none
PAD_BINDING_EVIDENCE function=padRead kind=static_fingerprint pc=0x00125000 score=130
PAD_BINDING_EVIDENCE function=padRead kind=static_fingerprint pc=0x00126000 score=125
PAD_BINDING_DIAGNOSTIC function=padRead code=ambiguous_static_candidates
PAD_BINDING function=padGetState confidence=unresolved pc=none
PAD_BINDINGS_END
```

Formatting rules:

- canonical function order is fixed;
- each `PAD_BINDING` line is followed immediately by its evidence lines, sorted by evidence kind then guest PC;
- guest PCs are lowercase 8-digit hexadecimal with `0x` prefix;
- missing/ambiguous selected PC is literal `none`;
- diagnostic codes are stable lowercase snake_case tokens;
- diagnostics must not contain guest instruction bytes or memory dumps;
- repeated runs on identical input/options produce byte-identical PAD-binding sections.

The existing report remains unchanged when `--pad-bindings` is absent.

## 10. Explicit manifest boundary

A future explicit binding manifest remains the highest-confidence source, but it is not implemented in this milestone. V0 discovery output is designed so a later milestone can convert reviewed/trusted resolutions into such a manifest without changing `Ps2PadHleService`.

## 11. Integration with existing components

### Existing components reused unchanged

- `recompiler::parse_ps2_elf()` for authoritative ELF validation;
- `runtime::Ps2MemoryMap::from_elf()` for guest memory mapping;
- `analysis::analyze_r5900_reachability()` for decoded CFG/call metadata;
- `runtime::Ps2PadHleBindings` as the eventual destination type, but not populated automatically in v0.

### New analysis responsibilities

- optional section/symbol metadata;
- PAD symbol resolution;
- conservative public-semantics fingerprinting;
- evidence merging;
- deterministic reporting.

## 12. Failure handling

The discovery feature must distinguish hard failures from discovery limitations.

Hard failure:

- base ELF parse/load analysis fails using existing rules;
- normal analyzer prerequisites fail.

Nonfatal discovery limitation:

- no section table;
- no symbol table;
- stripped ELF;
- malformed optional metadata after the base ELF already passed loader validation;
- no fingerprint candidate;
- multiple fingerprint candidates;
- conflicting symbol/fingerprint evidence.

Nonfatal limitations produce unresolved/candidate output plus diagnostics. They do not cause the analyzer to fabricate a binding.

## 13. Determinism and resource limits

- No network access at analysis runtime.
- No dependency on host locale.
- No timestamps in PAD-binding report sections.
- Stable sort keys: logical function enum, evidence kind, guest PC.
- Candidate scanning is restricted to executable PT_LOAD memory and to the existing analysis block limit.
- Integer overflow or invalid span calculations terminate metadata parsing safely with `Malformed`.

## 14. TDD requirements

All production behavior is introduced RED -> GREEN.

### Metadata parser tests

Synthetic ELF fixtures cover:

- no section table -> `Absent`;
- valid `.symtab` + `.strtab`;
- valid `.dynsym` path;
- truncated section table;
- invalid section-name index;
- invalid linked string table;
- invalid symbol entry size;
- unsupported extended section numbering;
- multiplication/addition overflow guards;
- duplicate symbols;
- function and NOTYPE accepted only under the exact-name rules.

### Symbol binding tests

Synthetic executable segments cover all six PAD names independently:

- canonical spelling;
- accepted leading-underscore alias;
- wrong case rejected;
- prefix/suffix fuzzy matches rejected;
- zero-valued symbol rejected;
- non-executable symbol rejected;
- duplicate same-PC evidence remains unambiguous;
- duplicate different-PC symbols become unresolved.

### Fingerprint tests

Use synthetic R5900 instruction/control-flow fixtures built from public constants only:

- below-threshold feature set is ignored;
- one threshold candidate is selected as candidate;
- multiple candidate PCs produce `Candidate` with `guest_pc = nullopt`;
- a high static score never becomes trusted;
- no proprietary function bytes appear in fixtures.

### Merge/report tests

- exact symbol alone -> trusted;
- fingerprint alone -> candidate;
- symbol + matching fingerprint -> trusted;
- symbol + conflicting fingerprint -> trusted symbol plus diagnostic;
- ambiguous static candidates serialize with `pc=none` plus all evidence lines;
- deterministic canonical ordering;
- byte-identical repeated output;
- analyzer output unchanged without `--pad-bindings`.

### Regression gate

Full Windows CI must retain:

- all existing CTest tests;
- `ps2_pad_hle_service_tests` PASS;
- `r5900_block_dispatcher_guest_call_windows_tests` PASS;
- frame pacing telemetry PASS;
- 120 Hz pacing probe PASS;
- analyzer package validation PASS;
- pacing package validation PASS.

## 15. Success criteria

`PS2 PAD Binding Discovery v0` is complete only when:

1. a complete valid ELF can be analyzed without modifying loader semantics;
2. exact public PAD symbol names, when present in executable load memory, resolve deterministically;
3. stripped/no-symbol inputs degrade safely to candidate/unresolved output;
4. static fingerprints use only public semantic constants and synthetic test fixtures;
5. static fingerprints never self-promote to trusted;
6. the analyzer can emit the stable `PAD_BINDINGS_V0` section on request;
7. no proprietary bytes or game-specific PCs are committed;
8. full Windows CI is green on the exact final documentation head.

## 16. Explicit non-goals

Not part of this v0:

- runtime call tracing;
- runtime confirmation of static candidates;
- automatic `Ps2PadHleBindings` activation;
- game-specific hardcoded addresses;
- SIF RPC emulation;
- PADMAN or IOP execution;
- SIO2 emulation;
- pressure mode or rumble;
- port 1/multitap/multiplayer;
- boot/menu/gameplay claims;
- discovering unrelated libraries.

## 17. Follow-on milestones

The approved sequence is:

```text
A. PS2 PAD Binding Discovery v0
        |
        v
B. PS2 PAD Runtime Confirmation v0
        |
        v
C. PS2 PAD Auto-Binding Integration v0
```

Milestone B will add a generic R5900 call observer/probe and use runtime argument/sequence evidence to confirm static candidates. Milestone C will activate `Ps2PadHleService` only from trusted, externally measured or confirmed bindings.
