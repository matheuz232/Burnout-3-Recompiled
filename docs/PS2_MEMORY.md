# PS2 Memory Model

## Status

**NOT IMPLEMENTED.** Binary analysis has not started, so concrete Burnout 3 memory regions are not asserted here.

## Required design

The runtime will centralize PS2-address translation rather than scatter native pointer casts through reconstructed code.

Target concept:

```text
PS2 virtual address -> validated runtime region -> native backing memory
```

The implementation must explicitly model and document, once evidence is available:

- main RAM;
- scratchpad;
- special/IO regions used by the game;
- alignment requirements;
- little-endian data behavior;
- invalid/unmapped accesses;
- address ranges referenced by recompiled functions.

Each mapping decision must be traceable to executable/runtime evidence.
