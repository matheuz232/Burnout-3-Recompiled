# R5900 CreateSema HLE v0

Base: `feature/r5900-or-v0 @ ea46523f077d0dd1d61a0d7d71e586285679013e`.
Work branch: `feature/r5900-createsema-v0`. Continue inline with focused RED/GREEN
tests and one final Windows CI push, as requested for economical execution.

## Evidence and scope

The external ELF diagnostic stops at `SYSCALL 0x0010be24`, `v1=0x40`,
`a0=0x01ffffa0`. PS2SDK identifies selector 0x40 as CreateSema and declares
`s32 CreateSema(ee_sema_t*)`:

- [Selector table](https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/include/syscallnr.h)
- [Public EE ABI](https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/include/kernel.h)

The six 32-bit little-endian structure fields are:

| Offset | Field | Create behavior |
|---|---|---|
| 0x00 | count | Output/status field; ignored on input |
| 0x04 | max_count | Positive signed count |
| 0x08 | init_count | Signed count in [0, max_count] |
| 0x0c | wait_threads | Output/status field; ignored on input |
| 0x10 | attr | Only zero attributes supported in this increment |
| 0x14 | option | Opaque value retained in host metadata |

This implementation supports creation only. It does not claim kernel-complete
error codes, waiting, signaling, deletion, scheduling, or nonzero attributes.
Those operations remain explicit unsupported boundaries.

## Contract

- Read low32(a0); require a non-null, four-byte-aligned, fully backed 24-byte
  descriptor. Validate the whole range before offset arithmetic or field reads.
- Copy the parameters into service-owned semaphore metadata. Current count starts
  from init_count; waiting threads start at zero. Ignore the input status fields.
- Allocate monotonically increasing positive IDs, starting at 1 per service.
  A fixed 256-entry table is a **project v0 capacity**, not a hardware limit claim.
- Success changes only v0.low64, to the zero-extended positive ID. Preserve all
  other architectural state, descriptor bytes, and SetupThread/SetupHeap contexts.
- Bad memory/count parameters and capacity exhaustion return host `Fault`,
  transactionally; nonzero attributes return `Unsupported`. These deliberate v0
  stops must not be misrepresented as emulated guest kernel error returns.
- Keep syscall execution outside generated x64 and outside the native cache.
  Replaying cached guest code must still allocate a new semaphore for each call.
- A new service starts a new namespace. Guest SetupThread does not reset it.

## Execution plan

- [x] Add focused service RED tests and synthetic native startup regression tests.
- [x] Implement bounded CreateSema metadata and validation; pass service GREEN.
- [ ] Verify native resume, returned-ID store, exact syscall fault PC, and cache reuse.
- [x] Diagnose the next external code-prefix boundary using the reference model only.
- [ ] Update progress and validate the final working branch with Windows CI.

Local verification: 38 portable tests plus the existing host-syscall regression
pass. The Windows checks remain pending: automatic approval review denied public
GitHub publishing without explicit user authorization. The branch must be pushed
and its Windows checks inspected after authorization; no CI validation is claimed.

No proprietary data is committed. Integration/main remain unchanged; this branch
can be reviewed independently. No boot, playability, or game 120 FPS claim.
