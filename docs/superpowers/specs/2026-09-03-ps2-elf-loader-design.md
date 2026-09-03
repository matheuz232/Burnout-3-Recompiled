# PS2 ELF Loader — Design

## Scope

Implement the first executable-analysis primitive for Burnout 3: a strict, read-only parser for PlayStation 2 ELF executables. This milestone does not execute MIPS code, emulate PS2 hardware, map runtime RAM, or infer game-specific semantics.

## Supported input

The parser accepts a byte span and requires:

- ELF magic `0x7F 'E' 'L' 'F'`;
- ELFCLASS32;
- ELFDATA2LSB;
- ELF version 1;
- executable ELF type (`ET_EXEC`);
- MIPS machine (`EM_MIPS`);
- 52-byte ELF32 header;
- 32-byte ELF32 program-header entries.

All integer fields are decoded explicitly as little-endian values. The implementation does not reinterpret untrusted input with packed C structs.

## Output

`Ps2ElfImage` owns a copy of the source bytes and exposes:

- entry point;
- program-header table metadata;
- a list of `PT_LOAD` segments;
- per-segment file offset, virtual address, physical address, file size, memory size, flags, and alignment;
- a bounded view of each loadable segment's file bytes.

## Validation

The parser rejects malformed input before returning an image:

- truncated ELF header;
- invalid magic/class/endian/version/type/machine;
- invalid header/program-header sizes;
- program-header table outside the file;
- load segment file ranges outside the file;
- load segment `p_memsz < p_filesz`;
- arithmetic overflow while computing ranges.

Zero program headers are valid structurally, though a real game executable is expected to contain loadable segments.

## Architecture

Files live under `src/recompiler/` because this parser feeds executable analysis and later static/binary recompilation work. It remains platform-independent and testable on the Linux host.

The next subsystem will consume `Ps2ElfImage` to construct the initial PS2 memory map. That later memory layer, not this parser, decides how `p_vaddr`/`p_paddr` map to native backing storage.

## Testing

Synthetic in-memory ELF fixtures cover:

1. valid ELF32/MIPS executable with one loadable segment;
2. malformed magic;
3. wrong machine;
4. truncated program-header table;
5. segment data outside the file;
6. `p_memsz < p_filesz`;
7. multiple program headers with only `PT_LOAD` exposed as loadable segments.
