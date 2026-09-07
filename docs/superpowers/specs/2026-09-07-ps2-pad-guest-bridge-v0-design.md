# PS2 PAD Guest Bridge v0 Design

Status: approved design, pre-implementation
Date: 2026-09-07

## Goal

Make the already-validated `Ps2PadReport` available to EE guest code through a reusable guest-call HLE mechanism, without pretending that SIO2, SIF RPC, IOP or PADMAN are implemented.

This milestone creates two layers:

1. a generic R5900 guest-call HLE hook keyed by guest PC;
2. a host-independent PS2 PAD HLE service that exposes a minimal `libpad`-compatible surface for one virtual controller at port 0 / slot 0.

The real Burnout 3 function entry addresses remain unknown until measured from a complete lawful user-supplied ELF. Tests therefore use synthetic guest PCs and bindings.

## Existing inputs

The host input chain is already validated:

`WindowsGameInput -> GameInputState -> Ps2PadReport`

`Ps2PadReport` contains:

- virtual connection state;
- 16 PS2 digital buttons encoded active-low;
- right stick horizontal/vertical bytes;
- left stick horizontal/vertical bytes.

The guest bridge consumes only `Ps2PadReport`. It does not call XInput or Win32 APIs and does not inspect `gamepad_connected`.

## Architecture

Data flow:

```text
WindowsGameInput
      |
      v
GameInputState
      |
      v
Ps2PadReport
      |
      v
Ps2PadHleService
      |
      v
IR5900GuestCallService
      |
      v
R5900BlockDispatcher
      |
      v
EE registers / Ps2MemoryMap
```

`IR5900GuestCallService` is separate from the existing `IR5900HostSyscallService`. Guest-call HLE handles normal function-entry PCs; syscall HLE continues to handle the R5900 `SYSCALL` instruction.

The dispatcher consults the guest-call service before analyzing or compiling the block at `current_pc`.

## Generic guest-call HLE contract

Introduce a reusable interface equivalent to:

```cpp
struct R5900GuestCallRequest {
    std::uint32_t guest_pc{};
};

enum class R5900GuestCallStatus {
    Handled,
    NotHandled,
    Fault,
};

struct R5900GuestCallResult {
    R5900GuestCallStatus status{R5900GuestCallStatus::NotHandled};
    std::string message{};
};

class IR5900GuestCallService {
public:
    virtual ~IR5900GuestCallService() = default;

    [[nodiscard]] virtual R5900GuestCallResult try_handle(
        const R5900GuestCallRequest& request,
        R5900IrExecutionState& state,
        runtime::Ps2MemoryMap& memory) = 0;
};
```

`R5900BlockDispatcherOptions` gains:

```cpp
IR5900GuestCallService* guest_calls{};
```

Before normal block lookup/analysis:

- null service: continue normally;
- `NotHandled`: continue normally;
- `Handled`: increment `guest_calls_handled`, set `current_pc = low32($ra)`, set `next_pc`, and continue dispatch;
- `Fault`: stop with a new deterministic `GuestCallFailure` reason at the intercepted PC and preserve the handler diagnostic.

A handled guest call does not consume a compiled guest instruction and does not enter the native block cache.

The dispatcher must retain all existing syscall, cache, memory-fault and control-flow behavior for non-intercepted PCs.

## PAD HLE service

Create a host-independent `Ps2PadHleService` implementing `IR5900GuestCallService`.

It owns only the state required by this v0:

- initialized/not initialized;
- port 0 / slot 0 open/closed;
- last accepted 256-byte pad-area guest address;
- latest `Ps2PadReport` snapshot;
- explicit guest-PC bindings for supported HLE functions.

The service exposes a host-side setter equivalent to:

```cpp
void set_report(const input::Ps2PadReport& report) noexcept;
```

A later runtime integration can update this snapshot once per host frame after input polling. Guest `padRead` reads the latest snapshot and never performs host polling itself.

## Explicit function bindings

Bindings are provided explicitly as data, for example:

```cpp
struct Ps2PadHleBindings {
    std::uint32_t pad_init{};
    std::uint32_t pad_port_open{};
    std::uint32_t pad_get_state{};
    std::uint32_t pad_read{};
    std::uint32_t pad_port_close{};
    std::uint32_t pad_end{};
};
```

A zero or otherwise unbound address must not be intercepted.

The service must not guess, pattern-match or hard-code Burnout 3 function addresses in v0. Real bindings are added only when measured from a complete lawful user ELF.

## EE calling convention

For intercepted functions:

- arguments come from low 32 bits of `$a0..$a3` (`gpr[4]..gpr[7]`);
- return value is written to low 32 bits of `$v0` (`gpr[2]`);
- successful dispatch resumes at low 32 bits of `$ra` (`gpr[31]`);
- register high halves are not modified unless explicitly required by an existing project convention.

## Supported libpad v0 surface

Supported calls:

### `padInit(int mode)`

- valid only for `mode == 0`;
- on success: mark service initialized, return `1`;
- unsupported mode: handler fault with a clear diagnostic;
- no IOP/PADMAN/SIF initialization is claimed.

### `padPortOpen(int port, int slot, void* padArea)`

- requires initialized service;
- supports only `port == 0` and `slot == 0`;
- `padArea` must be non-zero;
- `padArea` must be 64-byte aligned;
- the complete 256-byte region must be backed by EE RAM;
- on success: record the address, mark port open, return `1`;
- failure must not alter prior open/closed state or the saved pad-area address.

No PADMAN data structure is fabricated in the 256-byte area in this milestone.

### `padGetState(int port, int slot)`

For port 0 / slot 0:

- closed: return `PAD_STATE_DISCONN` (`0x00`);
- open: return `PAD_STATE_STABLE` (`0x06`).

Other port/slot values produce a controlled handler fault in v0.

### `padRead(int port, int slot, padButtonStatus* data)`

Requires:

- initialized service;
- port 0 / slot 0;
- port open;
- non-zero `data`;
- complete 32-byte destination backed by EE RAM.

On success, build all 32 bytes locally before modifying guest RAM, then copy the complete structure atomically from the bridge's perspective.

The layout is the public `ps2sdk` `padButtonStatus` ABI:

| Offset | Size | Field | v0 value |
|---:|---:|---|---|
| 0 | 1 | `ok` | `0` when connected/readable; non-zero neutral error marker when virtual pad is disconnected |
| 1 | 1 | `mode` | `0x79` for connected DualShock 2 analog report; `0x00` when disconnected |
| 2 | 2 | `btns` | little-endian `buttons_active_low` |
| 4 | 1 | `rjoy_h` | report `right_x` |
| 5 | 1 | `rjoy_v` | report `right_y` |
| 6 | 1 | `ljoy_h` | report `left_x` |
| 7 | 1 | `ljoy_v` | report `left_y` |
| 8 | 12 | pressure fields | all zero in v0 |
| 20 | 12 | unknown/reserved | all zero in v0 |

For a disconnected virtual report, the digital mask and sticks remain neutral (`0xffff`, `0x80` values) and the status fields indicate no usable connected pad.

On success return `1`. Destination validation or state errors must leave guest memory unchanged.

### `padPortClose(int port, int slot)`

- supports only 0 / 0;
- if open, close the port and return `1`;
- if already closed, remain closed and return `1` for idempotent v0 behavior;
- clear the saved pad-area address.

### `padEnd()`

- clear initialized state;
- close the port;
- clear the saved pad-area address;
- retain the latest host report snapshot so a later re-init can reuse the next supplied sample;
- return `1`.

## Error model

The generic guest-call layer distinguishes:

- `NotHandled`: PC is not one of this service's explicit bindings; normal dispatcher execution continues;
- `Handled`: HLE call completed and dispatcher resumes through `$ra`;
- `Fault`: the PC is bound to this HLE, but arguments/state/memory are invalid or unsupported.

PAD faults must include the function name, guest PC and relevant invalid argument/state in their message.

No bound PAD call may silently fall through to guest execution after validation has started.

## Transactionality

Guest-visible writes are transactional for this v0:

- validate the full target span before any write;
- assemble `padButtonStatus` in host-local storage first;
- do not partially mutate the destination if validation fails;
- `padPortOpen` state changes occur only after all argument and memory checks pass.

## Runtime integration boundary

The v0 service itself is platform-neutral. A later bounded integration into the Windows runtime may perform:

```text
input_state = WindowsGameInput::poll()
report = encode_ps2_pad_report(input_state)
pad_hle.set_report(report)
```

exactly once per frame before guest simulation.

This design does not claim that Burnout 3 consumes the report until real guest function bindings are measured and the dispatcher is executing the relevant guest path.

## Testing strategy

Use TDD RED -> GREEN.

Portable/service tests cover:

- unbound PC -> `NotHandled`;
- each explicit PAD binding routes to the correct handler;
- ABI argument extraction from `$a0..$a3`;
- return values in `$v0`;
- `padInit(0)` success and non-zero mode failure;
- `padPortOpen` initialization requirement;
- valid 64-byte-aligned 256-byte pad area;
- null, misaligned and partially out-of-RAM pad areas;
- open-state transactionality;
- closed `padGetState` -> `0x00`;
- open `padGetState` -> `0x06`;
- exact 32-byte `padRead` layout;
- active-low buttons preserved;
- stick order `RX, RY, LX, LY`;
- pressure/reserved bytes zeroed;
- disconnected neutral report behavior;
- keyboard-originated reports work because no XInput metadata is inspected;
- invalid `padRead` destination leaves RAM unchanged;
- `padPortClose` and `padEnd` state transitions.

Dispatcher tests cover:

- guest-call service queried before block analysis/compilation;
- handled call resumes at `$ra`;
- handled count increments;
- handled calls do not enter/consume native block cache entries;
- `NotHandled` PC follows the existing dispatcher path unchanged;
- guest-call `Fault` produces `GuestCallFailure` with exact PC/message;
- existing syscall HLE remains independent and passes regression tests;
- existing cache, memory-fault and control-flow tests remain green.

Final validation requires the complete Windows CI suite, frame-pacing gates and package validation on the exact final documentation head.

## Non-goals

This milestone does not implement or claim:

- SIO2 register/device emulation;
- SIF RPC;
- IOP execution;
- PADMAN;
- multitap;
- port 1 or additional slots;
- vibration/actuators;
- pressure-sensitive mode behavior;
- `padInfoMode`, `padSetMainMode`, actuator APIs or the broader libpad API;
- automatic discovery of libpad addresses;
- game-specific binary patches;
- boot, menu or gameplay;
- real Burnout 3 input consumption without measured bindings.

## Reuse objective

`IR5900GuestCallService` is intentionally generic so future HLE services for other PS2 libraries or games can reuse the same dispatcher boundary. `Ps2PadHleService` depends only on generic EE state/memory plus `Ps2PadReport`, keeping host-platform and game-specific concerns outside the service.

## Completion criteria

The milestone is complete only when:

1. generic guest-call dispatch is implemented and regression-tested;
2. the minimal PAD HLE surface above is implemented and tested;
3. no unmeasured Burnout 3 addresses are hard-coded;
4. all existing tests remain green;
5. Windows CI validates the exact final head;
6. documentation accurately states that real game binding/execution remains pending external validation.
