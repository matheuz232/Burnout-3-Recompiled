# Progress

Status date: 2026-09-07

Completion rule: `implemented -> compiled -> executed/tested -> validated`.

This file is the active engineering snapshot. Detailed history remains in Git and in dated validation records under `docs/validation/`.

## Current branch

- Milestone branch: `feature/game-input-v0`
- Implementation/test head before documentation: `9cb0f61bd8bf6eda27905a3ae7db6024bb795c47`
- Latest validated Windows CI before documentation: run **#809** (`34159434149`)
- CTest on #809: **69/69 PASS**
- Game/runtime status: the project still does **not** boot Burnout 3, render the game, reach menus, or provide gameplay.

## Current engineering status

| Component | Status | Evidence / next gate |
|---|---|---|
| Repository / CMake bootstrap | DONE | C++20, CMake 3.25+, Visual Studio 2022 / Windows x64 workflow |
| Win32 bootstrap/window | CI_VALIDATED | Client size, windowed/fullscreen styles, WM_CLOSE, Escape, recreation, WM_QUIT and stale-handle cleanup validated; physical visual check remains release certification |
| QPC / 120 Hz frame pacing | CI_VALIDATED | #809 pacing telemetry and 120-frame probe passed; long physical-desktop capture remains |
| Crash handler / minidump | CI_VALIDATED | Controlled Windows CI crash path |
| PS2 ELF loader | CI_VALIDATED | ELF32 little-endian MIPS parsing and PT_LOAD validation |
| EE main RAM v0 | CI_VALIDATED | Zero-filled 32 MiB `0x00000000..0x01ffffff`; PT_LOAD copied into RAM |
| Typed guest memory | CI_VALIDATED | Little-endian u8/u16/u32/u64/u128 reads/writes |
| R5900 decoder / IR | CI_VALIDATED | Startup subset includes `LD -> Load64`, `SW -> Store32`, `SD -> Store64`, `SQ -> Store128`, `DADDU -> Add64` |
| R5900 reference executor | CI_VALIDATED | `read64`, write32/write64/write128 callbacks and precise load/store faults |
| Windows x86-64 backend | CI_VALIDATED | Native `Load64`, Store32/64/128 plus current integer/control-flow subset; native/reference differential tests |
| Native dispatcher/cache | CI_VALIDATED | On-demand lowering/compile, exact guest-word validation, cold/exact/fast replay, `read64` + store adapters, precise load/store fault diagnostics |
| `LD / Load64` | CI_VALIDATED | 32-bit wrapped effective address, strict 8-byte alignment, transactional load, destination high64 preservation, observable `LD $zero` |
| `SW / Store32` | CI_VALIDATED | Low32 source, strict 4-byte alignment, exact width-4 store faults |
| `SD / Store64` | CI_VALIDATED | Low64 source, strict 8-byte alignment, exact width-8 store faults |
| `SQ / Store128` | CI_VALIDATED | Existing v0 full-128 store semantics |
| `BEQ/BNE`, `BEQL/BNEL`, `J/JAL`, `JR/JALR` | CI_VALIDATED | Native control transfers and architectural delay-slot semantics |
| Host syscall service | CI_VALIDATED | Host boundary outside generated x64; deterministic handled/unsupported/fault accounting |
| `SetupThread` HLE `0x3c` | CI_VALIDATED | Explicit-stack mode |
| `SetupHeap` HLE `0x3d` | CI_VALIDATED | Automatic-size `-1` convention resolved from thread stack base |
| `CreateSema` HLE `0x40` | CI_VALIDATED | Per-service IDs, validated descriptor copy, transactional failures, native continuation/cache |
| Synthetic startup through previous `LD @ 0x00114f08` boundary | CI_VALIDATED | Startup-shaped test executes the LD, restores RA low64, preserves high64, and stops only at the following synthetic sentinel |
| External next-boundary probe | CI_VALIDATED | Optional external mode emits stable stop reason/PC, one 32-bit boundary word and decoder fields; only `UnsupportedInstruction`/`UnsupportedSyscall` count as discovered boundaries |
| Real external next boundary | PENDING_EXTERNAL_VALIDATION | Run the probe with a complete user-supplied lawful ELF; historical local copy is truncated and production loader correctly rejects it |
| Static/binary recompiler | IN_PROGRESS | Continue from the first boundary measured by the external probe |
| Graphics / GS / VU | TODO | No game rendering path yet |
| IOP / SPU2 / audio | TODO | No game audio path yet |
| Game input | CI_VALIDATED | Keyboard + XInput host acquisition, deterministic deadzones/merge/reconnect; PS2 PAD adapter remains pending |
| Game initialization | TODO | Not reached |
| Menu / gameplay | TODO | Not reached |

## Game Input v0

Host-side input acquisition is now available through a platform-neutral state plus a Windows backend:

- canonical digital buttons, two sticks, two triggers and `gamepad_connected` metadata;
- keyboard fallback through `GetAsyncKeyState`;
- XInput mapping for face buttons, D-pad, Start/Select, shoulders, thumb clicks and triggers;
- radial deadzones for both sticks;
- deterministic trigger thresholding and clamping;
- deterministic keyboard + gamepad merge;
- active-controller reuse, disconnect fallback and reconnect discovery;
- injectable APIs so CI does not require physical controller/keyboard hardware;
- exactly one `WindowsGameInput::poll()` sample per runtime frame before simulation.

The sample is deliberately not guest-visible yet. Burnout 3 still requires a later PS2 PAD/SIO2/libpad adapter before it can consume host controls.

TDD evidence:

```text
Canonical RED       19fd00c6505df6d30f65738994263e59b34f8573  CI #802 expected Configure failure
Canonical GREEN     187590ed34e42f8ecc01242bbf6ee487830c96db  CI #803 68/68 PASS
Windows map RED     6f8c942b7f1208c65c6987edf4a18f0fb45eb94c  CI #805 expected Configure failure
Windows map GREEN   3d98ff2a0542ba1f402e032918bdb6e7285c4cd9  CI #806 69/69 PASS
Reconnect RED       306ec80d37897a48c3a326eb0774ca6035450ecf  CI #807 68/69; only active-controller reuse failed
Reconnect GREEN     8c8ef999c457047aa3222fa85f6af89d730ff25f  CI #808 69/69 PASS
Runtime integration 9cb0f61bd8bf6eda27905a3ae7db6024bb795c47  CI #809 69/69 PASS
```

Detailed evidence: `docs/validation/2026-09-07-game-input-v0.md`.

## Startup boundary status

The synthetic startup gate uses public ISA encodings and synthetic data only. It crosses the previously observed `LD @ 0x00114f08` boundary. The optional external startup harness can now report the first controlled unsupported boundary after that prefix using one 32-bit guest word.

The historical external input available in the development environment remains incomplete:

```text
available file size                  3,589,632 bytes
load segment requires bytes through  4,073,344 bytes
```

The production loader correctly rejects that truncated input. Loader validation was not weakened, missing game bytes were not invented, and no proprietary game bytes are committed.

Therefore the next unsupported instruction or syscall on the real Burnout 3 path remains **unknown** and must not be guessed. A complete user-supplied lawful ELF is required to measure it.

Example local invocation on a user's own complete ELF:

```powershell
.\build\Release\r5900_block_dispatcher_createsema_windows_tests.exe C:\Games\Burnout3\SLUS_210.50
```

CI contains no game file and runs synthetic mode only.

## Windows CI evidence

Windows CI #809 (`34159434149`) on implementation commit `9cb0f61bd8bf6eda27905a3ae7db6024bb795c47`:

```text
Host                 Windows Server 2022
Generator            Visual Studio 17 2022 x64
Compiler             MSVC 19.44
Configure            PASS
Build                PASS
CTest                69/69 PASS
windows_game_input   PASS
Frame telemetry      PASS
120 Hz probe         PASS
Analyzer package     PASS
Pacing package       PASS
```

Pacing on #809 remained at a 120 Hz target: 240-sample telemetry mean 8.333 ms, P95 8.333 ms, P99 8.334 ms, zero samples above 9/10/12 ms; the 120-frame probe also averaged 8.333 ms with zero frames above those thresholds.

Hosted CI validates logic and timing behavior; a physical Windows desktop remains useful for visual/performance release certification.

## Remaining Test Build 0.1 gates

1. Run the external next-boundary probe with a complete user-supplied lawful ELF and record the first new real boundary after `0x00114f08`.
2. Continue R5900 instruction/kernel/HLE coverage from that measured boundary using the same RED -> GREEN process.
3. Implement a guest-facing PS2 PAD/SIO2/libpad adapter so Burnout 3 can consume the now-validated host input state.
4. Run a 60-second or longer physical-desktop 120 Hz pacing capture for release certification.
5. Implement GS/VU rendering, IOP/SPU2/audio and remaining game-runtime services before any boot/playability claim.

## Guardrails

- Milestone branch: `feature/game-input-v0`.
- Base for this bounded milestone: validated Win32 window documentation head `cb9ec4a0685271dfa0fd32837cd137a9f496975f` plus the approved Game Input design/plan lineage.
- No PCSX2 runtime dependency is introduced by this milestone.
- No PS2 PAD/SIO2/libpad behavior is claimed by Game Input v0.
- Never commit proprietary Burnout 3 data.
- Never claim boot, menu, rendering or gameplay without direct evidence.
