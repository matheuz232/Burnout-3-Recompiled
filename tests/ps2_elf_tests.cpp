#include "analysis/ps2_pad_binding_discovery.h"
#include "recompiler/ps2_elf.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

Bytes make_valid_elf(std::uint16_t program_header_count = 1) {
    constexpr std::uint32_t kProgramHeaderOffset = 52;
    constexpr std::uint32_t kSegmentOffset = 0x100;

    Bytes bytes(0x120, 0);
    bytes[0] = 0x7F;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 1; // ELFCLASS32
    bytes[5] = 1; // ELFDATA2LSB
    bytes[6] = 1; // EV_CURRENT

    put_u16(bytes, 16, 2); // ET_EXEC
    put_u16(bytes, 18, 8); // EM_MIPS
    put_u32(bytes, 20, 1); // EV_CURRENT
    put_u32(bytes, 24, 0x00100000);
    put_u32(bytes, 28, kProgramHeaderOffset);
    put_u16(bytes, 40, 52); // ELF32_EHDR size
    put_u16(bytes, 42, 32); // ELF32_PHDR size
    put_u16(bytes, 44, program_header_count);

    // PT_LOAD
    put_u32(bytes, kProgramHeaderOffset + 0, 1);
    put_u32(bytes, kProgramHeaderOffset + 4, kSegmentOffset);
    put_u32(bytes, kProgramHeaderOffset + 8, 0x00100000);
    put_u32(bytes, kProgramHeaderOffset + 12, 0x00100000);
    put_u32(bytes, kProgramHeaderOffset + 16, 4);
    put_u32(bytes, kProgramHeaderOffset + 20, 8);
    put_u32(bytes, kProgramHeaderOffset + 24, 5);
    put_u32(bytes, kProgramHeaderOffset + 28, 0x1000);

    bytes[kSegmentOffset + 0] = 0xDE;
    bytes[kSegmentOffset + 1] = 0xAD;
    bytes[kSegmentOffset + 2] = 0xBE;
    bytes[kSegmentOffset + 3] = 0xEF;
    return bytes;
}

Bytes make_binding_elf() {
    auto bytes = make_valid_elf();
    put_u32(bytes, 52u + 16u, 0x20u);
    put_u32(bytes, 52u + 20u, 0x20u);
    return bytes;
}

[[noreturn]] void fail(const char* message) {
    std::cerr << "ps2_elf_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void expect_error(const Bytes& bytes, b3r::recompiler::Ps2ElfError expected, const char* message) {
    const auto result = b3r::recompiler::parse_ps2_elf(bytes);
    if (result.error != expected || result.ok()) {
        std::cerr << "ps2_elf_tests: FAIL: " << message
                  << " expected error=" << static_cast<int>(expected)
                  << " got=" << static_cast<int>(result.error) << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void run_pad_binding_resolution_tests() {
    using namespace b3r::analysis;

    const auto parsed = b3r::recompiler::parse_ps2_elf(make_binding_elf());
    expect(parsed.ok(), "binding fixture ELF must parse");
    const auto& image = *parsed.image;

    {
        Elf32MetadataResult metadata{};
        metadata.status = Elf32MetadataStatus::Available;
        metadata.symbols = {
            {"padInit", 0x00100000u, 4u, 1u, kSttFunc, 1u},
            {"padPortOpen", 0x00100004u, 4u, 1u, kSttFunc, 1u},
            {"padGetState", 0x00100008u, 4u, 1u, kSttFunc, 1u},
            {"padRead", 0x0010000cu, 4u, 1u, kSttFunc, 1u},
            {"padPortClose", 0x00100010u, 4u, 1u, kSttFunc, 1u},
            {"padEnd", 0x00100014u, 4u, 1u, kSttFunc, 1u},
        };
        const auto collected = collect_ps2_pad_symbol_evidence(metadata, image);
        expect(collected.evidence.size() == 6u,
               "all six canonical PAD symbols must produce evidence");
        for (std::size_t index = 0; index < collected.evidence.size(); ++index) {
            expect(static_cast<std::size_t>(collected.evidence[index].function) == index,
                   "canonical symbols must map in PAD function enum order");
            expect(collected.evidence[index].kind == PadBindingEvidenceKind::ElfSymbol,
                   "exact symbol evidence must be tagged ElfSymbol");
            expect(collected.evidence[index].score == 1000u,
                   "exact symbol evidence must use score 1000");
        }
    }

    {
        Elf32MetadataResult metadata{};
        metadata.status = Elf32MetadataStatus::Available;
        metadata.symbols = {
            {"_padInit", 0x00100000u, 4u, 1u, kSttNotype, 1u},
            {"_padPortOpen", 0x00100004u, 4u, 1u, kSttFunc, 1u},
            {"_padGetState", 0x00100008u, 4u, 1u, kSttFunc, 1u},
            {"_padRead", 0x0010000cu, 4u, 1u, kSttFunc, 1u},
            {"_padPortClose", 0x00100010u, 4u, 1u, kSttFunc, 1u},
            {"_padEnd", 0x00100014u, 4u, 1u, kSttFunc, 1u},
        };
        const auto collected = collect_ps2_pad_symbol_evidence(metadata, image);
        expect(collected.evidence.size() == 6u,
               "all six leading-underscore aliases must be accepted");
        expect(collected.evidence.front().detail == "_padInit",
               "symbol evidence detail must preserve exact spelling");
    }

    {
        Elf32MetadataResult metadata{};
        metadata.status = Elf32MetadataStatus::Available;
        metadata.symbols = {
            {"PadRead", 0x00100000u, 4u, 1u, kSttFunc, 1u},
            {"padRead_extra", 0x00100004u, 4u, 1u, kSttFunc, 1u},
            {"xpadRead", 0x00100008u, 4u, 1u, kSttFunc, 1u},
            {"padRead", 0u, 4u, 1u, kSttFunc, 1u},
            {"padEnd", 0x0010000cu, 4u, 1u, 1u, 1u},
        };
        const auto collected = collect_ps2_pad_symbol_evidence(metadata, image);
        expect(collected.evidence.empty(),
               "fuzzy, zero-valued, or non-function PAD symbols must be rejected");
    }

    {
        Elf32MetadataResult metadata{};
        metadata.status = Elf32MetadataStatus::Available;
        metadata.symbols = {
            {"padRead", 0x00100020u, 4u, 1u, kSttFunc, 1u},
        };
        const auto collected = collect_ps2_pad_symbol_evidence(metadata, image);
        expect(collected.evidence.empty(),
               "PAD symbol outside file-backed executable range must be rejected");
        expect(collected.diagnostics.size() == 1u,
               "non-executable exact PAD symbol must emit one diagnostic");
    }

    {
        const std::vector<PadBindingEvidence> evidence{
            {PadBindingFunction::PadRead, PadBindingEvidenceKind::StaticFingerprint,
             0x00101234u, 120u, "copies_32_bytes,port_bound_2"},
        };
        const auto result = resolve_ps2_pad_binding_evidence(evidence);
        const auto& read = result.resolutions[static_cast<std::size_t>(PadBindingFunction::PadRead)];
        expect(read.confidence == PadBindingConfidence::Candidate,
               "static fingerprint alone must resolve only as candidate");
        expect(read.guest_pc == 0x00101234u,
               "single static fingerprint candidate must retain its PC");
    }

    {
        const std::vector<PadBindingEvidence> evidence{
            {PadBindingFunction::PadRead, PadBindingEvidenceKind::ElfSymbol,
             0x0010000cu, 1000u, "padRead"},
            {PadBindingFunction::PadRead, PadBindingEvidenceKind::ElfSymbol,
             0x0010000cu, 1000u, "padRead"},
        };
        const auto result = resolve_ps2_pad_binding_evidence(evidence);
        const auto& read = result.resolutions[static_cast<std::size_t>(PadBindingFunction::PadRead)];
        expect(read.confidence == PadBindingConfidence::Trusted,
               "duplicate same-PC exact symbols must remain trusted");
        expect(read.guest_pc == 0x0010000cu,
               "duplicate same-PC exact symbols must retain the single PC");
        expect(read.evidence.size() == 1u,
               "byte-identical duplicate evidence may be collapsed deterministically");
    }

    {
        const std::vector<PadBindingEvidence> evidence{
            {PadBindingFunction::PadRead, PadBindingEvidenceKind::ElfSymbol,
             0x0010000cu, 1000u, "padRead"},
            {PadBindingFunction::PadRead, PadBindingEvidenceKind::ElfSymbol,
             0x00100010u, 1000u, "_padRead"},
        };
        const auto result = resolve_ps2_pad_binding_evidence(evidence);
        const auto& read = result.resolutions[static_cast<std::size_t>(PadBindingFunction::PadRead)];
        expect(read.confidence == PadBindingConfidence::Unresolved,
               "different exact-symbol PCs must be unresolved");
        expect(!read.guest_pc.has_value(),
               "different exact-symbol PCs must never choose a winner");
        expect(!result.diagnostics.empty(),
               "different exact-symbol PCs must produce an ambiguity diagnostic");
    }

    {
        const std::vector<PadBindingEvidence> evidence{
            {PadBindingFunction::PadRead, PadBindingEvidenceKind::ElfSymbol,
             0x0010000cu, 1000u, "padRead"},
            {PadBindingFunction::PadRead, PadBindingEvidenceKind::StaticFingerprint,
             0x0010000cu, 120u, "copies_32_bytes,port_bound_2"},
        };
        const auto result = resolve_ps2_pad_binding_evidence(evidence);
        const auto& read = result.resolutions[static_cast<std::size_t>(PadBindingFunction::PadRead)];
        expect(read.confidence == PadBindingConfidence::Trusted,
               "matching symbol and fingerprint must remain trusted");
        expect(read.guest_pc == 0x0010000cu,
               "matching symbol and fingerprint must select the symbol PC");
        expect(result.diagnostics.empty(),
               "matching symbol and fingerprint must not invent a conflict");
    }

    {
        const std::vector<PadBindingEvidence> evidence{
            {PadBindingFunction::PadRead, PadBindingEvidenceKind::ElfSymbol,
             0x0010000cu, 1000u, "padRead"},
            {PadBindingFunction::PadRead, PadBindingEvidenceKind::StaticFingerprint,
             0x00100010u, 120u, "copies_32_bytes,port_bound_2"},
        };
        const auto result = resolve_ps2_pad_binding_evidence(evidence);
        const auto& read = result.resolutions[static_cast<std::size_t>(PadBindingFunction::PadRead)];
        expect(read.confidence == PadBindingConfidence::Trusted,
               "trusted exact symbol must dominate conflicting static candidate");
        expect(read.guest_pc == 0x0010000cu,
               "conflicting static candidate must not replace trusted symbol PC");
        expect(!result.diagnostics.empty(),
               "symbol/fingerprint conflict must remain visible");
    }

    {
        const std::vector<PadBindingEvidence> evidence{
            {PadBindingFunction::PadRead, PadBindingEvidenceKind::StaticFingerprint,
             0x0010000cu, 120u, "copies_32_bytes,port_bound_2"},
            {PadBindingFunction::PadRead, PadBindingEvidenceKind::StaticFingerprint,
             0x00100010u, 140u, "copies_32_bytes,port_bound_2,slot_bound_8"},
        };
        const auto result = resolve_ps2_pad_binding_evidence(evidence);
        const auto& read = result.resolutions[static_cast<std::size_t>(PadBindingFunction::PadRead)];
        expect(read.confidence == PadBindingConfidence::Candidate,
               "multiple fingerprint PCs remain candidate-only");
        expect(!read.guest_pc.has_value(),
               "multiple fingerprint PCs must render as ambiguous rather than selecting one");
    }
}

} // namespace

int main() {
    using namespace b3r::recompiler;

    run_pad_binding_resolution_tests();

    {
        const auto result = parse_ps2_elf(make_valid_elf());
        expect(result.ok(), "valid PS2 ELF must parse");
        expect(result.image.has_value(), "valid parse must return image");
        expect(result.image->entry_point() == 0x00100000u, "entry point must be decoded");
        expect(result.image->program_header_count() == 1u, "program header count must be retained");
        expect(result.image->load_segments().size() == 1u, "PT_LOAD must be exposed");

        const auto& segment = result.image->load_segments().front();
        expect(segment.file_offset == 0x100u, "segment offset must be decoded");
        expect(segment.virtual_address == 0x00100000u, "segment vaddr must be decoded");
        expect(segment.physical_address == 0x00100000u, "segment paddr must be decoded");
        expect(segment.file_size == 4u, "segment file size must be decoded");
        expect(segment.memory_size == 8u, "segment memory size must be decoded");
        expect(segment.flags == 5u, "segment flags must be decoded");
        expect(segment.alignment == 0x1000u, "segment alignment must be decoded");

        const auto file_bytes = result.image->segment_file_bytes(0);
        expect(file_bytes.size() == 4u, "segment file view must be bounded to p_filesz");
        expect(file_bytes[0] == 0xDEu && file_bytes[3] == 0xEFu, "segment bytes must match source image");
    }

    {
        auto bytes = make_valid_elf();
        bytes[0] = 0;
        expect_error(bytes, Ps2ElfError::BadMagic, "bad magic must be rejected");
    }

    {
        auto bytes = make_valid_elf();
        put_u16(bytes, 18, 3); // EM_386
        expect_error(bytes, Ps2ElfError::UnsupportedMachine, "non-MIPS ELF must be rejected");
    }

    {
        auto bytes = make_valid_elf(2);
        bytes.resize(70); // second 32-byte program header cannot fit
        expect_error(bytes, Ps2ElfError::ProgramHeaderTableOutOfBounds,
                     "truncated program-header table must be rejected");
    }

    {
        auto bytes = make_valid_elf();
        put_u32(bytes, 52 + 4, 0x11Fu);
        put_u32(bytes, 52 + 16, 4);
        expect_error(bytes, Ps2ElfError::SegmentOutOfBounds, "segment file range must be bounded");
    }

    {
        auto bytes = make_valid_elf();
        put_u32(bytes, 52 + 16, 8);
        put_u32(bytes, 52 + 20, 4);
        expect_error(bytes, Ps2ElfError::SegmentMemoryTooSmall, "p_memsz < p_filesz must be rejected");
    }

    {
        auto bytes = make_valid_elf(2);
        constexpr std::size_t second = 52 + 32;
        put_u32(bytes, second + 0, 4); // PT_NOTE, not loadable
        const auto result = parse_ps2_elf(bytes);
        expect(result.ok(), "non-PT_LOAD program headers are structurally allowed");
        expect(result.image->program_header_count() == 2u, "all program headers must be counted");
        expect(result.image->load_segments().size() == 1u, "only PT_LOAD headers become load segments");
    }

    std::cout << "ps2_elf_tests: PASS\n";
    return EXIT_SUCCESS;
}
