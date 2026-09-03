# PS2 ELF Loader Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Parse and validate PS2 ELF32/MIPS executable structure without executing game code.

**Architecture:** Add a platform-independent parser under `src/recompiler/` that decodes fields explicitly from little-endian bytes and returns owned image metadata plus bounded load-segment byte views. Reject malformed ranges before constructing a successful image.

**Tech Stack:** C++20, CMake, CTest.

**Spec:** `docs/superpowers/specs/2026-09-03-ps2-elf-loader-design.md`

## Global Constraints

- No proprietary Burnout 3 binary or assets in tests/repository.
- ELF parser is read-only and performs no execution/emulation.
- Target executable format for this milestone is ELF32 little-endian MIPS ET_EXEC.
- Every input range is bounds/overflow checked before access.

---

### Task 1: ELF32/MIPS parser

**Files:**
- Create: `src/recompiler/ps2_elf.h`
- Create: `src/recompiler/ps2_elf.cpp`
- Create: `tests/ps2_elf_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `b3r::recompiler::parse_ps2_elf(std::span<const std::uint8_t>) -> Ps2ElfParseResult`.
- Produces: `Ps2ElfImage::entry_point()`, `load_segments()`, and `segment_file_bytes(index)`.

- [x] Write synthetic ELF tests and add the CMake test target.
- [x] Run the target and confirm RED because the parser API is absent.
- [x] Implement explicit little-endian ELF header/program-header decoding and validation.
- [x] Run the ELF tests and complete suite; confirm GREEN.
- [x] Update recompilation/progress documentation with evidence and next gate.
- [x] Commit the coherent ELF loader milestone.
