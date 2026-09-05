# Burnout 3 Recompiled

Experimental native Windows x86-64 recompilation/port project for **Burnout 3: Takedown**.

## Current milestone

`Burnout 3 Recompiled - Test Build 0.1` bootstrap, static-analysis, and native R5900 startup-execution infrastructure through the startup `SQ` guest-memory write, ordinary `BEQ`/`BNE`, branch-likely `BEQL`/`BNEL`, direct `J`/`JAL`, and indirect `JR`/`JALR` execution with architectural delay slots.

The current source tree contains:

- C++20/CMake project structure;
- native Win32 window bootstrap;
- structured logging and Windows minidump plumbing;
- QueryPerformanceCounter clock and 120 Hz Windows frame pacer;
- validated PS2 ELF32/MIPS structural loading;
- PT_LOAD-backed guest memory mapping with little-endian typed 8/16/32/64/128-bit access and atomic full-range 128-bit writes;
- an R5900 decoder with the narrow EE/MMI/COP1 startup subset required by the Burnout 3 entry path, including `SYNC`, `MTSAH`, `MTHI1`, `MTLO1`, `PADDUW`, `MTC1`, `CTC1`, `ADDA.S`, ordinary `BEQ`/`BNE`, scalar `AND`, and `SQ`;
- provenance-carrying R5900 IR with lowering for the startup execution subset, including NOP/ADDU/ADDIU/ORI/ANDI/LUI/AND, special HI/LO/SA writes, PADDUW, COP1 moves/accumulator add, SYNC semantics, and `Store128` lowering for `SQ`;
- block-level R5900 IR with typed `BranchEqual64`, `BranchEqualLikely64`, `BranchNotEqualLikely64`, `DirectJump`, `DirectCall`, `IndirectJump`, and `IndirectCall` terminators plus one explicit architectural delay slot; indirect calls carry an explicit link GPR;
- deterministic R5900 IR reference execution over all 32 128-bit EE GPRs plus HI/LO/HI1/LO1, SA, 32 raw FPRs, FCR31, FP accumulator state, and an opaque guest-memory callback bridge for `Store128`;
- a Windows x86-64 machine-code backend for the startup subset, with executable-page ownership and W^X allocation/protection;
- native x86-64 control-transfer emission for ordinary `BEQ`/`BNE`, branch-likely `BEQL`/`BNEL`, direct `J`/`JAL`, and indirect `JR`/`JALR`; `BEQL`/`BNEL` evaluate the low64 predicate before the slot, execute the delay exactly once only when taken, and branch around the complete emitted delay path when not taken; `BNE` reuses the equality terminator with swapped runtime destinations; indirect transfers snapshot the low 32-bit target before link/delay execution, `JALR` writes zero-extended `PC+8` to the decoded link GPR while preserving its high64 half, and the `rd == rs` / `rd == 0` cases are explicitly supported;
- native x86-64 `Store128` emission that uses a Win64 ABI-safe helper call, 32-bit EE effective-address wrap, 16-byte alignment-down semantics, and complete low/high 64-bit source forwarding;
- a Windows R5900 native block dispatcher that analyzes guest blocks, lowers/compiles them on demand, executes them through the x86-64 backend, consumes the JIT-returned `next_pc`, supports ordinary `BEQ`/`BNE`, branch-likely `BEQL`/`BNEL`, direct `J`/`JAL`, and indirect `JR`/`JALR`, applies a block budget, caches native blocks by guest PC, rejects stale cache entries after guest-code changes, and bridges mutable guest memory for body `SQ` operations;
- deterministic runtime `MemoryAccessFailure` propagation for guest stores, with completed-prefix accounting and cache retention when execution fails because the runtime address is unmapped;
- cache fingerprints that cover straight-line body words plus supported `BEQ`/`BNE`/`BEQL`/`BNEL`/`J`/`JAL`/`JR`/`JALR` terminator and delay-slot words; runtime branch predicates and indirect target values are deliberately excluded from the key so cached conditional/indirect blocks can change runtime outcomes without recompilation;
- differential Windows tests comparing reference and native x86-64 execution for integer/MMI/COP1 state, ordinary `BEQ`, branch-likely `BEQL`/`BNEL`, direct `J`/`JAL`, indirect `JR`/`JALR`, likely-branch annulment, target-snapshot/link-before-delay ordering, `rd == rs`, `rd == 0`, link high64 preservation, source-mutating delay slots, and `Store128` success/failure semantics; dispatcher tests additionally cover likely taken/not-taken behavior, cache reuse across runtime predicate changes, branch/delay-word invalidation, selected-guest-word accounting, and `SQ` delay rejection;
- a synthetic startup-shaped dispatcher test that completes **7 native guest blocks / 96 guest instructions**, covers one taken and one not-taken `BEQ`, executes `SQ` at `0x00100160`, direct `J`/`JAL`, `JR` with its delay slot, aliasing `JALR r5,r5` with link-visible delay semantics, reaches the indirect target at `0x001001c0`, executes not-taken `BNE r0,r0` at `0x001001c4` plus its NOP delay, and then stops at deterministic analysis failure `0x001001cc` because the synthetic executable fixture ends;
- an optional external-ELF validation mode for the startup dispatcher test, allowing a legally supplied `SLUS_210.50` to be tested locally without committing or uploading game data;
- conservative basic-block and reachable-CFG analysis;
- deterministic analysis reports;
- `Burnout3Analyze`, a console tool for analyzing an externally supplied PS2 ELF without executing guest code;
- portable/unit tests plus Windows-specific integration tests;
- Windows CI pinned to Visual Studio 2022.

Validated native control transfers: `BEQ`, `BNE`, `BEQL`, `BNEL`, `J`, `JAL`, `JR`, `JALR`. `BEQL`/`BNEL` implement architectural branch-likely annulment: their delay slot executes only on the taken path. `BLEZL`/`BGTZL` and REGIMM likely/link-likely variants remain unsupported. External legal-ELF validation of this expanded path is pending. The game does not boot yet.

The startup execution state models all 32 EE GPRs as 128-bit values split into low/high 64-bit halves and additionally models HI/LO/HI1/LO1, SA, raw 32-bit FPR values, FCR31, and the floating-point accumulator. Current integer write semantics preserve the modeled upper halves where required and GPR zero is normalized explicitly.

The Windows x86-64 backend emits callable native machine code for the current startup subset and is differentially checked against the reference executor. PADDUW uses alias-safe source capture and four-lane unsigned saturating addition; COP1 `MTC1`/`CTC1` preserve raw 32-bit payloads and the current `ADDA.S` implementation operates on the modeled raw single-precision values under the explicit v0 floating-point contract. `Store128` is emitted through a narrow C++ helper rather than embedding `Ps2MemoryMap` internals in generated code. The helper path uses a Win64-compliant call frame/shadow space and reports memory faults through the shared execution context. Executable memory is allocated writable, populated, changed to execute/read, and instruction-cache-flushed before execution.

`R5900BlockDispatcher` supports ordinary `BEQ`/`BNE`, branch-likely `BEQL`/`BNEL`, direct `J`/`JAL`, and indirect `JR`/`JALR` as native block terminators plus `SQ` in straight-line block bodies. The block body, supported terminator, and architectural delay slot form one cache candidate. `BEQ` and `BNE` evaluate their low64 GPR predicates before the slot and always execute one architectural delay instruction; `BNE` reuses `BranchEqual64` with the equality and inequality destinations swapped. `BEQL` and `BNEL` also decide the low64 predicate before the slot, but implement architectural branch-likely annulment: the taken path executes the delay exactly once while the not-taken path bypasses all delay code and returns `PC+8`. `J` returns its fixed direct target; `JAL` writes `PC+8` to `GPR31.low64` before the slot while preserving the upper 64 bits. `JR`/`JALR` snapshot the low 32-bit runtime target before the slot, and `JALR` writes its decoded link GPR before the delay slot without disturbing that register's high64 half; `rd == rs` therefore jumps using the old target, while `rd == 0` suppresses the link write. `SQ` uses the low 32 bits of the base GPR plus the signed 16-bit immediate with 32-bit wrap, then silently aligns the address down to 16 bytes before writing the full 128-bit source GPR. `SQ` in dispatcher-managed `BEQ`/`BNE`/`BEQL`/`BNEL`, `J`/`JAL`, or `JR`/`JALR` delay slots remains deliberately outside v0. `BLEZL`, `BGTZL`, REGIMM likely/link-likely variants, guest loads, other guest stores, and other unsupported control flow still stop conservatively.

A legally supplied Burnout 3 ELF was inspected out-of-repository. Its entry point is `0x00100008`. Static inspection identifies the original 74-instruction startup body before the first `BEQ` at `0x00100130`; the first branch is taken to `0x0010014C` with a NOP delay slot. The continuation contains `LUI`, `ORI`, scalar `AND`, a second `BEQ` at `0x00100158`, and its NOP delay slot; with the startup state produced by the preceding body, that second branch is not taken and fallthrough reaches `SQ` at `0x00100160`, whose startup target is `0x004e2680`. This real-file inspection is **not** claimed as native external execution: the Windows external-ELF dispatcher path is compiled and CI-tested without game data, but the supplied ELF has not yet been run through that Windows executable in this environment.

The game still does **not** boot. Broader guest-memory loads/stores, additional control flow including `BLEZL`/`BGTZL` and REGIMM likely/link-likely variants, BSS-clearing loops beyond the current synthetic startup boundary, syscall/HLE integration, graphics, audio, input, menus, and gameplay remain unimplemented. The `BEQL + BNEL branch-likely v0` milestone is **CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION** with 51/51 Windows tests; the legally supplied external ELF has not been run through this expanded Windows native path in this environment.

## Legal data policy

No proprietary Burnout 3 executable, assets, audio, textures, symbols, dumps, or game data are included in this repository. Game-data analysis and external execution validation use files supplied externally by the owner from a legally obtained copy. Do not commit those files.

## Analyze an external PS2 ELF

After a Release build:

```powershell
Burnout3Analyze.exe --elf "D:\\Games\\Burnout3\\SLUS_210.50" --output "burnout3-analysis.txt"
```

To write the report directly to the console:

```powershell
Burnout3Analyze.exe --elf "D:\\Games\\Burnout3\\SLUS_210.50"
```

The reachable-CFG worklist is bounded. The default is 4096 blocks and can be overridden explicitly:

```powershell
Burnout3Analyze.exe --elf "D:\\Games\\Burnout3\\SLUS_210.50" --max-blocks 8192
```

This tool performs static analysis only. It does not execute PS2 instructions, emulate a PS2, infer register-indirect targets, or invoke the native x86-64 recompilation backend. See `docs/ANALYSIS_TOOL.md` for the output contract and current limitations.

## Validate the startup prefix against an external ELF

The Windows startup dispatcher test can optionally consume a user-supplied ELF. The file is read locally at runtime and is never required by CI:

```powershell
.\build\Release\r5900_block_dispatcher_startup_windows_tests.exe "D:\\Games\\Burnout3\\SLUS_210.50"
```

A successful real-file run must print a line of this form:

```text
REAL_ELF_SQ_VALIDATED sq=0x00100160 target=0x004e2680 stop=0x........ blocks=N instructions=N
```

The harness requires at least 82 executed guest instructions, proves that execution advances beyond `0x00100160`, and verifies that the full 16-byte startup target at `0x004e2680` is zero afterward. It deliberately does not hard-code a later real-file stop boundary until native/static evidence identifies it. Until that command has been executed successfully against the supplied ELF on Windows x64, the milestone remains **CI_VALIDATED / READY_FOR_EXTERNAL_VALIDATION**, not externally native-validated.

## Build on Windows 10/11 x64

Requirements:

- Visual Studio 2022 with Desktop development with C++;
- CMake 3.25+;
- Windows 10/11 SDK.

From a Developer PowerShell:

```powershell
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug
```

Release:

```powershell
cmake --preset vs2022-release
cmake --build --preset vs2022-release
ctest --preset vs2022-release
```

The Visual Studio multi-config executables are normally under the selected configuration directory, including:

```text
Burnout3Recompiled_Test.exe
Burnout3Analyze.exe
```

## Runtime bootstrap options

`Burnout3Recompiled_Test.exe` currently accepts:

```text
--debug
--verbose
--windowed
--fullscreen
--game-data <path>
--log-level <level>
--disable-audio
--frame-stats
```

Some flags are accepted before their corresponding subsystem exists. Missing functionality remains documented rather than silently simulated.

## 120 FPS policy

The target presentation cadence is exactly **120.000 FPS**, corresponding to **8.333333 ms** per frame. The current bootstrap validates schedule math independently from the Windows waiting mechanism. The Windows backend uses QPC, a waitable timer, and a short spin phase to avoid relying exclusively on `Sleep()`.

This is only the presentation/frame-pacing foundation. The original game's simulation rate is **not assumed** to be 30 or 60 Hz; simulation timing will be chosen only after binary/runtime evidence.

See `docs/PROGRESS.md` for the authoritative status.
