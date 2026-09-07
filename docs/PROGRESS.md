# Progress

Status date: 2026-09-07

Completion rule: `implemented -> compiled -> executed/tested -> validated`.

This file is the active engineering snapshot. Detailed history remains in Git and in dated validation records under `docs/validation/`.

## Current branch

- Milestone branch: `feature/ps2-pad-report-v0`
- Implementation/test head before documentation: `37a73d1026452055ca3d9415f73636c56e7fadb0`
- Latest validated Windows CI before documentation: run **#819** (`34162871241`)
- CTest on #819: **70/70 PASS**
- Game/runtime status: the project still does **not** boot Burnout 3, render the game, reach menus, or provide gameplay.

## Current engineering status

| Component | Status | Evidence / next gate |
|---|---|---|
| Repository / CMake bootstrap | DONE | C++20, CMake 3.25+, Visual Studio 2022 / Windows x64 workflow |
| Win32 bootstrap/window | CI_VALIDATED | Client size, windowed/fullscreen styles, WM_CLOSE, Escape, recreation, WM_QUIT and stale-handle cleanup validated; physical visual check remains release certification |
| QPC / 120 Hz frame pacing | CI_VALIDATED | #819 pacing telemetry and 120-frame probe passed; long physical-desktop capture remains |
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
| Game input | CI_VALIDATED | Keyboard + XInput host acquisition, deterministic deadzones/merge/reconnect |
| PS2 PAD report adapter v0 | CI_VALIDATED | Portable active-low PS2 button report, DualShock-style stick bytes, explicit virtual connection; guest libpad/PADMAN/SIO2 bridge remains pending |
| Game initialization | TODO | Not reached |
| Menu / gameplay | TODO | Not reached |

## Game Input v0

Host-side input acquisition is available through a platform-neutral state plus a Windows backend:

- canonical digital buttons, two sticks, two triggers and `gamepad_connected` metadata;
- keyboard fallback through `GetAsyncKeyState`;
- XInput mapping for face buttons, D-pad, Start/Select, shoulders, thumb clicks and triggers;
- radial deadzones for both sticks;
- deterministic trigger thresholding and clamping;
- deterministic keyboard + gamepad merge;
- active-controller reuse, disconnect fallback and reconnect discovery;
- injectable APIs so CI does not require physical controller/keyboard hardware;
- exactly one `WindowsGameInput::poll()` sample per runtime frame before simulation.

This host state can now be converted into the portable PS2 PAD report described below. It is still not guest-visible: Burnout 3 requires a later libpad/PADMAN/SIO2 bridge before it can consume the report.

Detailed evidence: `docs/validation/2026-09-07-game-input-v0.md`.

## PS2 PAD Report Adapter v0

The portable input layer now provides `GameInputState -> Ps2PadReport` without adding Win32, guest-memory, RPC, PADMAN or SIO2 dependencies:

- exact 16-button PS2/libpad bit positions;
- active-low button word with neutral `0xffff` and all-buttons-pressed `0x0000`;
- four stick bytes ordered as right X/Y then left X/Y;
- horizontal `-1.0 -> 0x00`, `0.0 -> 0x80`, `+1.0 -> 0xff`;
- vertical sign inversion so canonical positive-Y/up maps toward `0x00`;
- finite checking and clamping; NaN/Inf become neutral `0x80`;
- explicit virtual-pad connection independent from XInput-only `gamepad_connected`, preserving keyboard-only input;
- disconnected reports are fully neutral and cannot leak stale state;
- L2/R2 use only their canonical digital button bits; trigger magnitudes/pressure bytes are not serialized.

TDD evidence:

```text
Digital RED       587389abecc8ea6f41a484af5821b59666e4b113  CI #816 expected Configure failure: ps2_pad_report.cpp absent
Digital GREEN     a6da1487354c2f6f2451f48b25540f21fac66ef0  CI #817 70/70 PASS
Analog RED        cad461d5d240d67a451bb8651de19c7258c9a3e3  CI #818 69/70; only ps2_pad_report_tests failed at horizontal -1 endpoint
Analog GREEN      37a73d1026452055ca3d9415f73636c56e7fadb0  CI #819 70/70 PASS
```

Detailed evidence: `docs/validation/2026-09-07-ps2-pad-report-v0.md`.

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

Windows CI #819 (`34162871241`) on implementation commit `37a73d1026452055ca3d9415f73636c56e7fadb0`:

```text
Host                 Windows Server 2022
Generator            Visual Studio 17 2022 x64
Compiler             MSVC 19.44
Configure            PASS
Build                PASS
CTest                70/70 PASS
ps2_pad_report_tests PASS
windows_game_input   PASS
Frame telemetry      PASS
120 Hz probe         PASS
Analyzer package     PASS
Pacing package       PASS
```

Pacing on #819 remained at a 120 Hz target: 240-sample telemetry mean 8.333 ms, P95 8.333 ms, P99 8.333 ms, zero samples above 9/10/12 ms; the 120-frame probe also averaged 8.333 ms with zero frames above those thresholds.

The documentation commit containing this snapshot is accepted as the final milestone head only after the same full Windows CI workflow succeeds on that exact commit.

Hosted CI validates logic and timing behavior; a physical Windows desktop remains useful for visual/performance release certification.

## Remaining Test Build 0.1 gates

1. Run the external next-boundary probe with a complete user-supplied lawful ELF and record the first new real boundary after `0x00114f08`.
2. Continue R5900 instruction/kernel/HLE coverage from that measured boundary using the same RED -> GREEN process.
3. Bridge the validated `Ps2PadReport` into the guest-facing PS2 controller path (libpad/PADMAN/SIO2 or the measured equivalent used by Burnout 3).
4. Run a 60-second or longer physical-desktop 120 Hz pacing capture for release certification.
5. Implement GS/VU rendering, IOP/SPU2/audio and remaining game-runtime services before any boot/playability claim.

## Guardrails

- Milestone branch: `feature/ps2-pad-report-v0`.
- Base: validated Game Input documentation head `c07a855ac9ac1b5c01dc22f85fd9eb0973623c15` plus the approved PS2 PAD Report design/plan lineage.
- No PCSX2 runtime dependency is introduced by this milestone.
- No guest-visible libpad/PADMAN/SIO2 behavior, pressure mode, rumble, or game-specific PAD hook is claimed by PS2 PAD Report Adapter v0.
- Never commit proprietary Burnout 3 data.
- Never claim boot, menu, rendering or gameplay without direct evidence.
