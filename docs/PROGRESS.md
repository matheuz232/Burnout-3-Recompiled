# Progress

Status date: 2026-09-07

Completion rule: `implemented -> compiled -> executed/tested -> validated`.

This file is the active engineering snapshot. Detailed history remains in Git and in dated validation records under `docs/validation/`.

## Current branch

- Milestone branch: `feature/ps2-pad-guest-bridge-v0`
- Implementation/test head before documentation: `ce15accedf38a4e80c80100a9d944aad6b4aac9b`
- Latest validated implementation Windows CI before documentation: run **#834** (`34170912217`), job `101890883239`
- CTest on #834: **72/72 PASS**
- Game/runtime status: the project still does **not** boot Burnout 3, render the game, reach menus, or provide gameplay.

## Current engineering status

| Component | Status | Evidence / next gate |
|---|---|---|
| Repository / CMake bootstrap | DONE | C++20, CMake 3.25+, Visual Studio 2022 / Windows x64 workflow |
| Win32 bootstrap/window | CI_VALIDATED | Client size, windowed/fullscreen styles, WM_CLOSE, Escape, recreation, WM_QUIT and stale-handle cleanup validated; physical visual check remains release certification |
| QPC / 120 Hz frame pacing | CI_VALIDATED | #834 pacing telemetry and 120-frame probe passed; long physical-desktop capture remains |
| Crash handler / minidump | CI_VALIDATED | Controlled Windows CI crash path |
| PS2 ELF loader | CI_VALIDATED | ELF32 little-endian MIPS parsing and PT_LOAD validation |
| EE main RAM v0 | CI_VALIDATED | Zero-filled 32 MiB `0x00000000..0x01ffffff`; PT_LOAD copied into RAM |
| Typed guest memory | CI_VALIDATED | Little-endian u8/u16/u32/u64/u128 reads/writes |
| R5900 decoder / IR | CI_VALIDATED | Startup subset includes `LD -> Load64`, `SW -> Store32`, `SD -> Store64`, `SQ -> Store128`, `DADDU -> Add64` |
| R5900 reference executor | CI_VALIDATED | `read64`, write32/write64/write128 callbacks and precise load/store faults |
| Windows x86-64 backend | CI_VALIDATED | Native `Load64`, Store32/64/128 plus current integer/control-flow subset; native/reference differential tests |
| Native dispatcher/cache | CI_VALIDATED | On-demand lowering/compile, exact guest-word validation, cold/exact/fast replay, typed memory adapters and guest-call interception before cache/analysis |
| R5900 guest-call HLE boundary | CI_VALIDATED | Generic explicit-PC `Handled`/`NotHandled`/`Fault` service; `$ra` resume, precise failure provenance and no cache/block consumption for handled calls |
| `LD / Load64` | CI_VALIDATED | 32-bit wrapped effective address, strict 8-byte alignment, transactional load, destination high64 preservation, observable `LD $zero` |
| `SW / Store32` | CI_VALIDATED | Low32 source, strict 4-byte alignment, exact width-4 store faults |
| `SD / Store64` | CI_VALIDATED | Low64 source, strict 8-byte alignment, exact width-8 store faults |
| `SQ / Store128` | CI_VALIDATED | Existing v0 full-128 store semantics |
| `BEQ/BNE`, `BEQL/BNEL`, `J/JAL`, `JR/JALR` | CI_VALIDATED | Native control transfers and architectural delay-slot semantics |
| Host syscall service | CI_VALIDATED | Host boundary outside generated x64; deterministic handled/unsupported/fault accounting; remains independent from guest-call HLE |
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
| PS2 PAD report adapter v0 | CI_VALIDATED | Portable active-low PS2 button report, DualShock-style stick bytes and explicit virtual connection |
| PS2 PAD guest bridge v0 | CI_VALIDATED | Reusable port-0/slot-0 libpad-style lifecycle plus transactional 32-byte `padRead` guest-memory ABI; real Burnout function bindings/live wiring remain pending |
| Game initialization | TODO | Not reached |
| Menu / gameplay | TODO | Not reached |

## Game Input and PS2 PAD pipeline

The input stack now has three validated layers:

1. `GameInputState`: host-neutral buttons/sticks/triggers with a Windows keyboard + XInput acquisition backend.
2. `Ps2PadReport`: portable PS2-style active-low button word and four deterministic analog bytes.
3. `Ps2PadHleService`: guest-facing HLE endpoint layer able to expose that report through a 32-byte `padButtonStatus`-compatible destination.

Host acquisition includes keyboard fallback, XInput face/D-pad/start/select/shoulder/thumb/trigger mapping, radial deadzones, deterministic merge, active-controller reuse, disconnect fallback/reconnect discovery, injectable APIs and one poll per runtime frame before simulation.

The portable report keeps exact public PS2/libpad button bit positions, neutral `0xffff`, right-X/right-Y/left-X/left-Y ordering, deterministic clamping/rounding and explicit virtual connection. Pressure and rumble are not represented by v0.

Detailed evidence:

- `docs/validation/2026-09-07-game-input-v0.md`
- `docs/validation/2026-09-07-ps2-pad-report-v0.md`
- `docs/validation/2026-09-07-ps2-pad-guest-bridge-v0.md`

## PS2 PAD Guest Bridge v0

A generic `IR5900GuestCallService` now lets the dispatcher intercept explicitly bound guest function PCs before native cache lookup or block analysis. A handled guest function resumes at low32(`$ra`) without consuming a native block or guest instruction count; `NotHandled` falls through unchanged; `Fault` produces `GuestCallFailure` with the intercepted PC and exact service message. The host `SYSCALL` boundary is unchanged and independent.

`Ps2PadHleService` provides a deliberately small libpad-compatible contract:

- explicit bindings only; there are no guessed Burnout 3 addresses;
- `padInit`, `padPortOpen`, `padGetState`, `padRead`, `padPortClose`, `padEnd`;
- port 0 / slot 0 only;
- `padPortOpen` validates a complete 256-byte, 64-byte-aligned EE RAM area before state mutation;
- `padGetState` returns disconnected (`0x00`) or stable (`0x06`) from the current report snapshot;
- disconnected `padRead` returns 0 and performs no write;
- connected `padRead` validates the complete 32-byte destination first, then writes one locally assembled payload and returns 32;
- payload bytes 2-3 carry active-low buttons little-endian; bytes 4-7 carry right-X/right-Y/left-X/left-Y; remaining pressure/reserved bytes are zero in v0;
- lifecycle and destination failures are transactional from the bridge perspective.

The public PS2SDK contract independently confirms the 32-byte `padButtonStatus` field ordering, the 256-byte/64-byte `padPortOpen` area requirement, and state values `DISCONN=0x00` / `STABLE=0x06`.

**Important boundary:** this is a reusable guest-facing ABI/HLE bridge, but Burnout 3 is not yet proven to call it. The actual Burnout 3 libpad entry addresses or equivalent guest call path must be measured from a complete lawful user-supplied executable before live runtime binding. No address is guessed.

TDD evidence:

```text
Guest-call RED       932b9c57127524d638ca6abe72885e3e07b9ae05  CI #828 expected Build failure: guest-call interface absent
Guest-call GREEN     9605c48bb4b592a541925c5e677430695f669ec3  CI #830 71/71 PASS after deterministic fixture correction
PAD lifecycle RED    8a343218770955a1e7efb4f3d767a348fab65ca3  CI #831 expected Configure failure: ps2_pad_hle_service.cpp absent
PAD lifecycle GREEN  3382cb9914fd825318efec131a6837ff879e3aeb  CI #832 full workflow PASS
padRead RED          c9ebabf504903c5412f0308fed157ec3bf6c9d0d  CI #833 71/72; only ps2_pad_hle_service_tests failed
padRead GREEN        ce15accedf38a4e80c80100a9d944aad6b4aac9b  CI #834 72/72 PASS
```

## Startup boundary status

The synthetic startup gate uses public ISA encodings and synthetic data only. It crosses the previously observed `LD @ 0x00114f08` boundary. The optional external startup harness can report the first controlled unsupported boundary after that prefix using one 32-bit guest word.

The historical external input available in the development environment remains incomplete:

```text
available file size                  3,589,632 bytes
load segment requires bytes through  4,073,344 bytes
```

The production loader correctly rejects that truncated input. Loader validation was not weakened, missing game bytes were not invented, and no proprietary game bytes are committed.

Therefore both the next real R5900/kernel boundary and the exact Burnout 3 PAD guest bindings remain evidence-dependent. A complete user-supplied lawful ELF is required; neither should be guessed.

Example local startup-boundary invocation on a user's own complete ELF:

```powershell
.\build\Release\r5900_block_dispatcher_createsema_windows_tests.exe C:\Games\Burnout3\SLUS_210.50
```

CI contains no game file and runs synthetic mode only.

## Windows CI evidence

Windows CI #834 (`34170912217`), job `101890883239`, on implementation commit `ce15accedf38a4e80c80100a9d944aad6b4aac9b`:

```text
Host                                           Windows Server 2022
Generator                                      Visual Studio 17 2022 x64
Compiler                                       MSVC 19.44
Configure                                      PASS
Build                                          PASS
CTest                                          72/72 PASS
ps2_pad_hle_service_tests                      PASS
r5900_block_dispatcher_guest_call_windows_tests PASS
windows_game_input_tests                       PASS
Frame telemetry                                PASS
120 Hz probe                                   PASS
Analyzer package validation                    PASS
Pacing package validation                      PASS
```

Pacing on #834 remained at the 120 Hz target: 240-sample telemetry mean/P95/P99 were 8.333 ms with zero samples above 9/10/12 ms. The 120-frame probe also averaged 8.333 ms with zero frames above 9/10/12 ms.

The documentation commit containing this snapshot is accepted as the final milestone head only after the same full Windows CI workflow succeeds on that exact commit. The validation record intentionally cites the implementation-head run rather than creating a self-referential follow-up documentation commit.

Hosted CI validates logic and timing behavior; a physical Windows desktop remains useful for visual/performance release certification.

## Remaining Test Build 0.1 gates

1. Run the external next-boundary probe with a complete user-supplied lawful ELF and record the first new real boundary after `0x00114f08`.
2. Continue R5900 instruction/kernel/HLE coverage from that measured boundary using RED -> GREEN.
3. Resolve the actual Burnout 3 `libpad` guest entry addresses or evidence-backed equivalent call path, then wire the live per-frame `Ps2PadReport` snapshot into `Ps2PadHleService` for real game execution.
4. Run a 60-second or longer physical-desktop 120 Hz pacing capture for release certification.
5. Implement GS/VU rendering, IOP/SPU2/audio and remaining game-runtime services before any boot/playability claim.

## Guardrails

- Milestone branch: `feature/ps2-pad-guest-bridge-v0`.
- Plan head/base for implementation comparison: `7d52b9a4a6391afb308e3a5ed18c666edf7d2d81`.
- No Burnout 3 PAD entry address is hard-coded or guessed by this milestone.
- No full SIF/PADMAN/IOP/SIO2 emulation, pressure mode, rumble or multiplayer support is claimed.
- No PCSX2 runtime dependency is introduced.
- Never commit proprietary Burnout 3 data.
- Never claim boot, menu, rendering, audio or gameplay without direct evidence.
