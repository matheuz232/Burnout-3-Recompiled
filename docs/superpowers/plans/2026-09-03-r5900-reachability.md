# R5900 Reachability Implementation Plan

**Goal:** Discover directly reachable R5900 basic blocks and preserve unresolved control-flow evidence without guessing.

**Architecture:** Extend `b3r_analysis` with a worklist traversal over `analyze_r5900_basic_block()` results. Calls are recorded separately; only explicit non-call continuation edges are followed.

**Tech Stack:** C++20, CMake, CTest.

- [x] Write reachability tests first.
- [x] Verify RED because the reachability API is absent.
- [x] Implement bounded deterministic traversal and issue recording.
- [x] Verify GREEN and complete host suite.
- [x] Verify clean GCC/Clang Release builds.
- [x] Publish and verify Windows/MSVC CI.
- [x] Update progress/validation evidence.
