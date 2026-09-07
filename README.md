# Burnout 3 Recompiled

Experimental native Windows x86-64 recompilation/port project for **Burnout 3: Takedown**.

## Current milestone

`Burnout 3 Recompiled - Test Build 0.1` now models the observed EE `CreateSema` startup boundary in addition to the existing native R5900 startup subset.

The established startup path includes `SetupThread`, `SetupHeap`, `SD`, `DADDU`, `SW`, direct calls and host-side EE syscall handling. The project still does **not** boot the game.

A lawful out-of-repository diagnostic of the user-supplied ELF now advances through both observed `CreateSema` calls and reaches the next unsupported guest instruction:

```text
0x00114f08  LD
```

That observation is **DIAGNOSTIC_VALIDATED** only. It is not a claim that the game boots or that the full path has been executed end-to-end by a physical Windows build.

## Current runtime/recompiler capabilities

- C++20 / CMake / Visual Studio 2022 Windows x64 project;
- native Win32 bootstrap, logging, crash/minidump support and QPC-based 120 Hz pacing infrastructure;
- PS2 ELF32 little-endian MIPS loading and conservative control-flow analysis;
- 32 MiB EE main RAM backing for `0x00000000..0x01ffffff`;
- typed little-endian 8/16/32/64/128-bit guest-memory access;
- incremental R5900 decoder/IR/reference-executor/x64 backend for the startup subset;
- `SQ -> Store128`, `SD -> Store64`, `SW -> Store32`, `DADDU -> Add64`;
- native/control-flow support for the currently required `BEQ/BNE`, `BEQL/BNEL`, `J/JAL`, `JR/JALR` and architectural delay slots;
- cached native blocks with exact guest-word validation and fast replay;
- boundary-prefix protection so a later transfer cannot execute past an earlier unsupported/faulting instruction;
- injectable host-syscall boundary outside generated x64;
- `SetupThread` (`0x3c`), `SetupHeap` (`0x3d`) and `CreateSema` (`0x40`) HLE;
- exact guest-PC/address/width memory-fault provenance.

## `CreateSema v0` contract

For EE syscall selector `0x40`, `a0.low32` is treated as a pointer to a six-word guest semaphore descriptor. v0:

- requires a 4-byte-aligned, fully readable descriptor;
- validates `max_count > 0`;
- validates `0 <= init_count <= max_count`;
- uses `init_count` as the host-side current count;
- records deterministic positive IDs `1, 2, 3, ...` without recycling;
- records `id`, `current_count`, `max_count`, `attr` and `option` in a host registry;
- ignores guest `count` and `wait_threads` as authoritative host state;
- returns the ID in `v0.low64` while preserving `v0.high64` and unrelated architectural state;
- does not mutate the guest descriptor;
- is transactional on faults: failed creation does not create a registry entry or consume an ID.

Scheduler behavior, blocking, wake queues and `DeleteSema`/`SignalSema`/`WaitSema` remain out of scope until they are reached empirically.

## Validation status

Implementation head for the completed CreateSema behavior:

```text
e7c618ebaa77538d8f9faccc932a0ef15b840ddc
```

Windows CI run **#763** (`34080792349`) on Windows Server 2022 / Visual Studio 2022 passed the complete existing suite plus the CreateSema contract. The workflow also passed frame-pacing telemetry, the one-second 120 Hz probe, analyzer-package validation and pacing-probe package validation.

Representative CreateSema TDD evidence:

```text
#756 RED  successful CreateSema contract not yet satisfied
#757 GREEN basic mapped CreateSema handling
#758 RED  second ID / validation behavior missing
#760 GREEN deterministic IDs + count validation
#761 RED  registry/alignment contract missing
#763 GREEN registry + alignment + attr/option + transactional ID commit
```

Hosted-runner timing is smoke evidence only; it is not physical-desktop 120 FPS certification.

## Lawful external ELF evidence

No proprietary Burnout 3 executable, raw instruction words, assets, audio, textures or game data are committed to this repository.

Starting from the established post-`SetupHeap` startup state, the lawful local diagnostic reaches two calls to the `CreateSema` wrapper. Both observed descriptors resolve to simple binary semaphore state:

```text
first CreateSema  -> id 1, current 1, max 1, attr 0, option 0
second CreateSema -> id 2, current 1, max 1, attr 0, option 0
```

The first returned semaphore ID is stored by the guest before the second call. After the second call returns, supported execution reaches:

```text
PC          0x00114f08
instruction LD
```

`LD` is therefore the next concrete R5900 implementation boundary. This is diagnostic evidence from the lawful external ELF, not a boot/playability claim.

## External startup validation harness

The optional Windows startup dispatcher test can consume a user-supplied ELF locally:

```powershell
.\build\Release\r5900_block_dispatcher_startup_windows_tests.exe "D:\Games\Burnout3\SLUS_210.50"
```

Its historical external mode validates the earlier startup path. Newer boundaries remain covered by synthetic/native CI plus lawful local diagnosis until the external Windows-native harness is explicitly extended and run through them.

## Analyze an external PS2 ELF

After a Release build:

```powershell
Burnout3Analyze.exe --elf "D:\Games\Burnout3\SLUS_210.50" --output "burnout3-analysis.txt"
```

The analyzer performs static analysis only. It does not emulate a PS2 or execute the game.

## Build on Windows 10/11 x64

Requirements:

- Visual Studio 2022 with Desktop development with C++;
- CMake 3.25+;
- Windows 10/11 SDK.

Release example:

```powershell
cmake --preset vs2022-release
cmake --build --preset vs2022-release
ctest --preset vs2022-release
```

## 120 FPS policy

The target presentation cadence is exactly **120.000 FPS** (`8.333333 ms` per frame). Current CI validates pacing infrastructure only. The original game's simulation rate is not assumed; simulation timing will be chosen from runtime evidence.

## Legal data policy

Only public ISA encodings and synthetic fixtures belong in the repository. Game-data analysis and external validation use files supplied externally by the owner from a legally obtained copy. Do not commit those files.

See `docs/PROGRESS.md` for the authoritative engineering snapshot.