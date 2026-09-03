# PS2 ELF Memory Map — Design

## Scope

Implement the first PS2-address-to-native-backing layer consumed by the ELF loader. This milestone maps only loadable regions described by validated `PT_LOAD` program headers. It does not yet model the full PlayStation 2 RAM, scratchpad, MMIO, DMA, VU memory, GS registers, IOP memory, or cache aliases.

This conservative scope avoids inventing hardware behavior before the original Burnout 3 executable has been inspected.

## Input

The mapper consumes a validated `Ps2ElfImage` and uses each `Ps2ElfLoadSegment`:

- `virtual_address` as the guest-visible base address;
- `file_size` bytes copied from the ELF segment payload;
- `memory_size` as the mapped guest region size;
- bytes in `[file_size, memory_size)` zero-initialized to represent ELF BSS;
- `flags` retained as metadata for later permission analysis.

`physical_address` is preserved by the ELF parser but is not used as the translation key in this milestone. Future executable evidence may require explicit physical/alias handling.

## Output

`Ps2MemoryMap` owns one native byte vector per mapped ELF region and exposes:

- `regions()` for analysis/debug metadata;
- `translate(address, length)` mutable and const bounded byte spans;
- typed little-endian helpers `read_u8/read_u16/read_u32` and `write_u8/write_u16/write_u32`;
- explicit errors when building a map from invalid/overlapping guest ranges.

No guest address is converted directly to a native pointer outside this class.

## Validation rules

Map construction rejects:

- guest address overflow (`virtual_address + memory_size` beyond 32-bit address space);
- overlapping non-empty mapped regions;
- impossible ELF payload/view mismatch;
- duplicate/ambiguous guest ranges.

Zero-length `PT_LOAD` memory regions are ignored.

Translation rejects:

- unmapped addresses;
- accesses crossing a region boundary;
- arithmetic overflow in `address + length`.

Typed helpers are explicitly little-endian and permit unaligned accesses because the memory layer represents bytes, not CPU alignment policy. CPU-level alignment exceptions, if required by the original code, belong in the future instruction/runtime layer.

## Testing

Synthetic tests cover:

1. mapping one ELF load segment;
2. file bytes copied and BSS tail zero-filled;
3. successful bounded translation;
4. unmapped and cross-boundary translation rejection;
5. overlapping ELF regions rejected;
6. 32-bit guest range overflow rejected;
7. little-endian read/write helpers;
8. writes visible through subsequent reads/translation.

No proprietary Burnout 3 data is included.

## Next gate

After this layer is validated on GCC, Clang and MSVC, the next executable-analysis step is to feed a legally supplied Burnout 3 ELF into the existing parser and this mapper, record its real entry point/load regions, and only then expand the memory model with observed PS2-specific regions and aliases.
