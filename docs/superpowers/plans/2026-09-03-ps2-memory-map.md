# PS2 ELF Memory Map Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Map validated ELF `PT_LOAD` regions into centralized native backing memory addressable by PS2 virtual addresses.

**Architecture:** Add `Ps2MemoryMap` under `src/runtime/`. It owns independent backing vectors for each non-empty load region, copies file bytes, zero-fills BSS, rejects overlaps/overflow, and centralizes all guest-address translation and little-endian scalar access.

**Tech Stack:** C++20, CMake, CTest.

**Spec:** `docs/superpowers/specs/2026-09-03-ps2-memory-map-design.md`

## Global Constraints

- No proprietary Burnout 3 data in repository/tests.
- Do not hardcode full PS2 RAM/scratchpad/MMIO ranges without executable/runtime evidence.
- Guest address translation must be centralized; no scattered guest-address pointer casts.
- All ranges are checked before access.

---

### Task 1: ELF-backed PS2 memory map

**Files:**
- Create: `src/runtime/ps2_memory_map.h`
- Create: `src/runtime/ps2_memory_map.cpp`
- Create: `tests/ps2_memory_map_tests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `docs/PS2_MEMORY.md`
- Modify: `docs/PROGRESS.md`
- Modify: `docs/VALIDATION.md`

**Interfaces:**
- Consumes: `const b3r::recompiler::Ps2ElfImage&`.
- Produces: `Ps2MemoryMap::from_elf(...) -> Ps2MemoryMapBuildResult`.
- Produces: bounded `translate(address, length)` spans and LE scalar read/write helpers.

- [x] Write synthetic memory-map tests and add the CMake test target.
- [x] Run the target and confirm RED because the memory-map API is absent.
- [x] Implement region construction, BSS zero-fill, overlap/overflow validation.
- [x] Implement bounded address translation.
- [x] Implement LE scalar read/write helpers.
- [x] Run the memory-map tests and full suite; confirm GREEN with GCC and Clang.
- [x] Update memory/progress/validation documentation.
- [ ] Publish feature branch and validate on Windows/MSVC CI.
