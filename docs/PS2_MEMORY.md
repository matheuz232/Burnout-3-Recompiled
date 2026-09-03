# PS2 Memory Model

## Status

**ELF-BACKED VIRTUAL MEMORY MAP: WORKING / HOST VALIDATED.**

The project now has a centralized first-stage guest-address mapper in `src/runtime/ps2_memory_map.*`. It intentionally models only regions proven by validated ELF `PT_LOAD` program headers. Full PlayStation 2 RAM, scratchpad, MMIO, DMA/VU/GS/IOP regions and address aliases are **not** hardcoded yet.

## Current mapping model

```text
validated Ps2ElfImage
        |
        v
PT_LOAD p_vaddr + p_memsz
        |
        v
Ps2MemoryMap region metadata
        |
        v
owned native byte backing
```

For each non-empty load segment:

- `p_vaddr` is the guest-visible base address;
- `p_filesz` bytes are copied from the ELF payload;
- the region is sized to `p_memsz`;
- `[p_filesz, p_memsz)` is zero-filled as ELF BSS;
- ELF flags are retained for future permission analysis;
- overlapping guest ranges and 32-bit address overflow are rejected.

`p_paddr` remains preserved by the ELF parser but is not used as the translation key in this milestone. Physical/alias behavior will be added only when supported by executable/runtime evidence.

## Centralized access

No reconstructed subsystem should cast a PS2 virtual address directly to a native pointer. Access goes through `Ps2MemoryMap`:

```text
PS2 virtual address + length
        |
        v
bounds/overflow check
        |
        v
mapped region lookup
        |
        v
bounded std::span over native backing
```

The current API provides mutable/const byte translation plus explicit little-endian `read_u8/u16/u32` and `write_u8/u16/u32` helpers.

Unaligned scalar accesses are permitted by this byte-storage layer. CPU instruction alignment semantics, if required, belong in the future MIPS execution/recompiled-runtime layer rather than in raw memory storage.

## Validation

Synthetic tests verify:

- PT_LOAD payload copy;
- BSS zero-fill;
- bounded in-region translation;
- unmapped/cross-boundary rejection;
- 32-bit guest-address overflow rejection;
- overlap rejection;
- little-endian scalar reads/writes;
- write visibility through subsequent translation.

No proprietary Burnout 3 data is included in these tests.

## Still unresolved

Executable/runtime evidence is still required before documenting or implementing concrete Burnout 3 usage of:

- main RAM ranges/aliases;
- scratchpad;
- special/IO regions;
- cache/uncached aliases;
- DMA-visible addressing;
- VU0/VU1 memory;
- GS registers/paths;
- IOP/SPU2 memory interaction;
- alignment faults expected by translated MIPS code.

The next real-data gate is to inspect a legally supplied Burnout 3 ELF, record its entry point and PT_LOAD map, then expand this document only with observed facts.
