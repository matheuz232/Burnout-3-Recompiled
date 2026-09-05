#include "recompiler/ps2_elf.h"
#include "recompiler/windows/r5900_block_dispatcher.h"
#include "runtime/ps2_memory_map.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint32_t kSqPc = 0x00100160u;
constexpr std::uint32_t kDirectJumpPc = 0x00100164u;
constexpr std::uint32_t kDirectJumpTarget = 0x00100180u;
constexpr std::uint32_t kDirectCallPc = 0x00100180u;
constexpr std::uint32_t kDirectCallTarget = 0x001001a0u;
constexpr std::uint32_t kIndirectReturnPc = 0x001001a4u;
constexpr std::uint32_t kExpectedLinkPc = 0x00100188u;
constexpr std::uint32_t kSqTarget = 0x004e2680u;

[[noreturn]] void fail(const char* message) {
    std::cerr << "r5900_block_dispatcher_startup_windows_tests: FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset + 0u] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0u] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

void put_program_header(Bytes& bytes,
                        std::size_t offset,
                        std::uint32_t file_offset,
                        std::uint32_t guest_address,
                        std::uint32_t file_size,
                        std::uint32_t memory_size,
                        std::uint32_t flags) {
    put_u32(bytes, offset + 0u, 1u); // PT_LOAD
    put_u32(bytes, offset + 4u, file_offset);
    put_u32(bytes, offset + 8u, guest_address);
    put_u32(bytes, offset + 12u, guest_address);
    put_u32(bytes, offset + 16u, file_size);
    put_u32(bytes, offset + 20u, memory_size);
    put_u32(bytes, offset + 24u, flags);
    put_u32(bytes, offset + 28u, 0x1000u);
}

b3r::runtime::Ps2MemoryMap make_memory(const std::vector<std::uint32_t>& words,
                                       std::uint32_t base) {
    constexpr std::uint32_t kProgramHeaderOffset = 52u;
    constexpr std::uint32_t kProgramHeaderSize = 32u;
    constexpr std::uint32_t kCodePayloadOffset = 0x100u;
    constexpr std::uint32_t kDataPayloadOffset = 0x400u;
    constexpr std::uint32_t kDataGuestBase = 0x004e2600u;
    constexpr std::uint32_t kDataFileSize = 0x100u;
    constexpr std::uint32_t kDataMemorySize = 0x200u;

    const auto code_size = static_cast<std::uint32_t>(words.size() * 4u);
    expect(kCodePayloadOffset + code_size <= kDataPayloadOffset,
           "synthetic code payload must not overlap data payload");

    Bytes bytes(static_cast<std::size_t>(kDataPayloadOffset + kDataFileSize), 0u);

    bytes[0] = 0x7fu;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 1u;
    bytes[5] = 1u;
    bytes[6] = 1u;
    put_u16(bytes, 16u, 2u);
    put_u16(bytes, 18u, 8u);
    put_u32(bytes, 20u, 1u);
    put_u32(bytes, 24u, base);
    put_u32(bytes, 28u, kProgramHeaderOffset);
    put_u16(bytes, 40u, 52u);
    put_u16(bytes, 42u, kProgramHeaderSize);
    put_u16(bytes, 44u, 2u);

    put_program_header(bytes,
                       kProgramHeaderOffset,
                       kCodePayloadOffset,
                       base,
                       code_size,
                       code_size,
                       5u);
    put_program_header(bytes,
                       kProgramHeaderOffset + kProgramHeaderSize,
                       kDataPayloadOffset,
                       kDataGuestBase,
                       kDataFileSize,
                       kDataMemorySize,
                       6u);

    for (std::size_t index = 0; index < words.size(); ++index) {
        put_u32(bytes,
                static_cast<std::size_t>(kCodePayloadOffset) + index * 4u,
                words[index]);
    }

    std::fill(bytes.begin() + kDataPayloadOffset,
              bytes.begin() + kDataPayloadOffset + kDataFileSize,
              0xa5u);

    auto parsed = b3r::recompiler::parse_ps2_elf(bytes);
    expect(parsed.ok(), "synthetic startup ELF must parse");
    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "synthetic startup ELF must map");
    return std::move(*built.memory);
}

constexpr std::uint32_t r_type(std::uint8_t rs,
                               std::uint8_t rt,
                               std::uint8_t rd,
                               std::uint8_t sa,
                               std::uint8_t funct) {
    return (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) |
           funct;
}

constexpr std::uint32_t i_type(std::uint8_t op,
                               std::uint8_t rs,
                               std::uint8_t rt,
                               std::uint16_t imm) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           imm;
}

constexpr std::uint32_t j_type(std::uint8_t op, std::uint32_t target) {
    return (static_cast<std::uint32_t>(op) << 26u) |
           ((target >> 2u) & 0x03ffffffu);
}

constexpr std::uint32_t mmi_type(std::uint8_t rs,
                                 std::uint8_t rt,
                                 std::uint8_t rd,
                                 std::uint8_t sa,
                                 std::uint8_t funct) {
    return (0x1cu << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) |
           funct;
}

constexpr std::uint32_t cop1_type(std::uint8_t rs,
                                  std::uint8_t rt,
                                  std::uint8_t rd,
                                  std::uint8_t sa,
                                  std::uint8_t funct) {
    return (0x11u << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(sa) << 6u) |
           funct;
}

std::vector<std::uint32_t> make_synthetic_startup_words(std::uint32_t base) {
    std::vector<std::uint32_t> words;
    words.reserve(113u);

    for (std::uint8_t rd = 1u; rd <= 30u; ++rd) {
        words.push_back(mmi_type(0u, 0u, rd, 0x10u, 0x28u));
    }

    words.push_back(r_type(0u, 0u, 0u, 0u, 0x11u));
    words.push_back(r_type(0u, 0u, 0u, 0u, 0x13u));
    words.push_back(mmi_type(0u, 0u, 0u, 0u, 0x11u));
    words.push_back(mmi_type(0u, 0u, 0u, 0u, 0x13u));
    words.push_back(i_type(0x01u, 0u, 0x19u, 0u));
    words.push_back(r_type(0u, 0u, 0u, 16u, 0x0fu));

    for (std::uint8_t fs = 0u; fs < 32u; ++fs) {
        words.push_back(cop1_type(0x04u, 0u, fs, 0u, 0u));
    }

    words.push_back(cop1_type(0x06u, 0u, 31u, 0u, 0u));
    words.push_back(cop1_type(0x10u, 0u, 0u, 0u, 0x18u));
    words.push_back(i_type(0x0fu, 0u, 2u, 0x004eu));
    words.push_back(i_type(0x0du, 2u, 2u, 0x2680u));
    words.push_back(i_type(0x0fu, 0u, 3u, 0x01ecu));
    words.push_back(i_type(0x0du, 3u, 3u, 0xea00u));

    expect(words.size() == 74u, "synthetic startup prefix count mismatch");
    expect(base + static_cast<std::uint32_t>(words.size() * 4u) == 0x00100130u,
           "synthetic startup first BEQ boundary mismatch");

    words.push_back(i_type(0x04u, 0u, 0u, 6u));
    words.push_back(i_type(0x09u, 0u, 20u, 0x11u));

    for (std::uint8_t index = 0u; index < 5u; ++index) {
        words.push_back(i_type(0x0eu,
                               0u,
                               static_cast<std::uint8_t>(10u + index),
                               1u));
    }

    expect(base + static_cast<std::uint32_t>(words.size() * 4u) == 0x0010014cu,
           "synthetic startup first BEQ target layout mismatch");

    words.push_back(i_type(0x0fu, 0u, 4u, 0xffffu));
    words.push_back(i_type(0x0du, 4u, 4u, 0xfff0u));
    words.push_back(r_type(3u, 4u, 4u, 0u, 0x24u));
    words.push_back(i_type(0x04u, 2u, 4u, 7u));
    words.push_back(i_type(0x09u, 0u, 21u, 0x22u));
    words.push_back(i_type(0x1fu, 2u, 0u, 0u));

    expect(words.size() == 87u, "synthetic startup architectural fixture count mismatch");
    expect(base + static_cast<std::uint32_t>((words.size() - 1u) * 4u) == kSqPc,
           "synthetic startup SQ boundary layout mismatch");

    words.push_back(j_type(0x02u, kDirectJumpTarget));          // 0x164 J
    words.push_back(i_type(0x09u, 0u, 22u, 0x0033u));       // 0x168 delay
    words.push_back(i_type(0x09u, 0u, 25u, 1u));            // 0x16c poison
    words.push_back(i_type(0x09u, 0u, 26u, 1u));            // 0x170 poison
    words.push_back(i_type(0x09u, 0u, 27u, 1u));            // 0x174 poison
    words.push_back(i_type(0x09u, 0u, 28u, 1u));            // 0x178 poison
    words.push_back(i_type(0x09u, 0u, 30u, 1u));            // 0x17c poison
    words.push_back(j_type(0x03u, kDirectCallTarget));       // 0x180 JAL
    words.push_back(i_type(0x09u, 31u, 23u, 0u));           // 0x184 delay sees r31
    words.push_back(i_type(0x0fu, 0u, 5u, 0x0010u));       // 0x188 LUI r5,0x0010
    words.push_back(i_type(0x0du, 5u, 5u, 0x01c0u));       // 0x18c ORI r5,r5,0x01c0
    words.push_back(r_type(5u, 0u, 5u, 0u, 0x09u));        // 0x190 JALR r5,r5
    words.push_back(i_type(0x09u, 5u, 6u, 0u));            // 0x194 JALR delay sees link
    words.push_back(i_type(0x09u, 0u, 25u, 1u));           // 0x198 poison
    words.push_back(i_type(0x09u, 0u, 26u, 1u));           // 0x19c poison
    words.push_back(i_type(0x09u, 0u, 24u, 0x0055u));      // 0x1a0 direct callee
    words.push_back(r_type(31u, 0u, 0u, 0u, 0x08u));       // 0x1a4 JR r31
    words.push_back(i_type(0x09u, 0u, 29u, 0x0077u));      // 0x1a8 JR delay
    words.push_back(i_type(0x09u, 0u, 25u, 2u));           // 0x1ac poison
    words.push_back(i_type(0x09u, 0u, 26u, 2u));           // 0x1b0 poison
    words.push_back(i_type(0x09u, 0u, 27u, 2u));           // 0x1b4 poison
    words.push_back(i_type(0x09u, 0u, 28u, 2u));           // 0x1b8 poison
    words.push_back(i_type(0x09u, 0u, 30u, 2u));           // 0x1bc poison/guard
    words.push_back(i_type(0x09u, 0u, 7u, 0x0066u));       // 0x1c0 indirect target
    words.push_back(i_type(0x05u, 0u, 0u, 0u));            // 0x1c4 unsupported BNE
    words.push_back(0u);                                    // 0x1c8 mapped BNE delay
    expect(words.size() == 113u, "synthetic JR/JALR fixture count mismatch");
    expect(base + static_cast<std::uint32_t>(87u * 4u) == kDirectJumpPc,
           "synthetic direct J PC mismatch");
    expect(base + static_cast<std::uint32_t>(94u * 4u) == kDirectCallPc,
           "synthetic direct JAL PC mismatch");
    return words;
}

Bytes read_binary_file(const char* path) {
    std::ifstream input(path, std::ios::binary);
    expect(static_cast<bool>(input), "external ELF must open");
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    expect(end > 0, "external ELF must be non-empty");
    const auto size = static_cast<std::size_t>(end);
    input.seekg(0, std::ios::beg);

    Bytes bytes(size);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    expect(input.gcount() == static_cast<std::streamsize>(size),
           "external ELF read must be complete");
    return bytes;
}

void validate_external_startup(const char* path) {
    using namespace b3r::recompiler;

    const auto bytes = read_binary_file(path);
    const auto parsed = parse_ps2_elf(bytes);
    expect(parsed.ok(), "external ELF must parse as a PS2 ELF");
    expect(parsed.image->entry_point() == 0x00100008u,
           "external ELF entry point mismatch");

    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "external ELF must map into PS2 memory");

    const auto target_before = built.memory->read_u128(kSqTarget);
    expect(target_before.has_value(),
           "external ELF must map the 16-byte SQ startup target");

    R5900BlockDispatcherOptions options{};
    options.block_options.max_instructions = 256u;
    R5900BlockDispatcher dispatcher(*built.memory, options);

    R5900IrExecutionState state{};
    state.gpr[31] = {0x1122334455667788ull, 0x8877665544332211ull};

    const auto result = dispatcher.run(parsed.image->entry_point(), state, 8u);
    expect(result.instructions_executed >= 82u,
           "real startup must execute SQ after the two BEQ blocks");
    expect(result.next_pc != kSqPc,
           "real startup must advance beyond SQ");

    const auto target_after = built.memory->read_u128(kSqTarget);
    expect(target_after.has_value(),
           "external SQ target must remain mapped after execution");
    expect((*target_after)[0] == 0u && (*target_after)[1] == 0u,
           "real startup SQ must zero the full 16-byte target");

    expect(state.gpr[2].low64 == 0x00000000004e2680ull,
           "real startup r2 result mismatch");
    expect(state.gpr[3].low64 == 0x0000000001ecea00ull,
           "real startup r3 result mismatch");
    expect(state.gpr[4].low64 == 0x0000000001ecea00ull,
           "real startup second-block AND result mismatch");
    expect(state.gpr[31].low64 == 0u && state.gpr[31].high64 == 0u,
           "real startup PADDUW must clear GPR31");
    expect(state.hi == 0u && state.lo == 0u && state.hi1 == 0u && state.lo1 == 0u,
           "real startup HI/LO state mismatch");
    expect(state.sa == 0u && state.fcr31 == 0u && state.fp_acc == 0u,
           "real startup SA/COP1 state mismatch");
    for (const auto raw : state.fpr) {
        expect(raw == 0u, "real startup FPR must remain raw zero");
    }

    std::cout << "REAL_ELF_SQ_VALIDATED sq=0x" << std::hex << std::setw(8)
              << std::setfill('0') << kSqPc
              << " target=0x" << std::setw(8) << kSqTarget
              << " stop=0x" << std::setw(8) << result.next_pc
              << std::dec << " blocks=" << result.blocks_executed
              << " instructions=" << result.instructions_executed << '\n';
}

void validate_synthetic_startup() {
    using namespace b3r::recompiler;

    constexpr std::uint32_t base = 0x00100008u;
    const auto words = make_synthetic_startup_words(base);
    auto memory = make_memory(words, base);

    expect(memory.read_u8(kSqTarget - 1u) == 0xa5u,
           "synthetic byte before SQ target must start as sentinel");
    const auto target_before = memory.read_u128(kSqTarget);
    expect(target_before.has_value() &&
               (*target_before)[0] == 0xa5a5a5a5a5a5a5a5ull &&
               (*target_before)[1] == 0xa5a5a5a5a5a5a5a5ull,
           "synthetic SQ target must start with non-zero sentinel bytes");
    expect(memory.read_u8(kSqTarget + 16u) == 0xa5u,
           "synthetic byte after SQ target must start as sentinel");

    R5900BlockDispatcherOptions options{};
    options.block_options.max_instructions = 128u;
    R5900BlockDispatcher dispatcher(memory, options);

    R5900IrExecutionState state{};

    const auto result = dispatcher.run(base, state, 8u);
    expect(result.reason == R5900DispatchStopReason::AnalysisFailure,
           "synthetic startup must stop when analysis reaches the end of the fixture");
    expect(result.next_pc == 0x001001ccu,
           "synthetic startup must advance beyond BNE and its delay slot");
    expect(result.blocks_executed == 7u,
           "synthetic startup must execute seven native blocks with BNE in the final block");
    expect(result.instructions_executed == 96u,
           "synthetic startup must execute ninety-six guest instructions");

    const auto target_after = memory.read_u128(kSqTarget);
    expect(target_after.has_value() &&
               (*target_after)[0] == 0u && (*target_after)[1] == 0u,
           "synthetic startup SQ must zero all 16 target bytes");
    expect(memory.read_u8(kSqTarget - 1u) == 0xa5u,
           "SQ must preserve byte immediately before target");
    expect(memory.read_u8(kSqTarget + 16u) == 0xa5u,
           "SQ must preserve byte immediately after target");

    expect(state.gpr[2].low64 == 0x00000000004e2680ull,
           "synthetic startup r2 result mismatch");
    expect(state.gpr[3].low64 == 0x0000000001ecea00ull,
           "synthetic startup r3 result mismatch");
    expect(state.gpr[4].low64 == 0x0000000001ecea00ull,
           "synthetic startup second-block AND result mismatch");
    expect(state.gpr[20].low64 == 0x11u,
           "synthetic startup first delay slot must execute");
    expect(state.gpr[21].low64 == 0x22u,
           "synthetic startup second delay slot must execute");
    expect(state.gpr[22].low64 == 0x33u,
           "J delay slot mismatch");
    expect(state.gpr[23].low64 == kExpectedLinkPc,
           "JAL delay link observation mismatch");
    expect(state.gpr[24].low64 == 0x55u,
           "direct callee entry mismatch");
    expect(state.gpr[29].low64 == 0x77u,
           "JR delay must execute");
    expect(state.gpr[5].low64 == 0x00100198u,
           "JALR rd==rs link mismatch");
    expect(state.gpr[6].low64 == 0x00100198u,
           "JALR delay must observe new link");
    expect(state.gpr[7].low64 == 0x66u,
           "indirect target entry mismatch");
    for (std::uint8_t index = 10u; index < 15u; ++index) {
        expect(state.gpr[index].low64 == 0u,
               "synthetic startup skipped XORI path must not execute");
    }
    expect(state.hi == 0u && state.lo == 0u && state.hi1 == 0u && state.lo1 == 0u,
           "synthetic startup HI/LO state mismatch");
    expect(state.sa == 0u && state.fcr31 == 0u && state.fp_acc == 0u,
           "synthetic startup SA/COP1 state mismatch");
    for (const auto raw : state.fpr) {
        expect(raw == 0u, "synthetic startup FPR must remain raw zero");
    }
    expect(state.gpr[31].low64 == kExpectedLinkPc,
           "JAL link mismatch");
    expect(state.gpr[31].high64 == 0u,
           "startup r31 high64 mismatch");
    expect(state.gpr[25].low64 == 0u && state.gpr[26].low64 == 0u &&
               state.gpr[27].low64 == 0u && state.gpr[28].low64 == 0u &&
               state.gpr[30].low64 == 0u,
           "direct-transfer poison fallthrough must remain untouched");

    std::cout << "SYNTHETIC_STARTUP_BNE_VALIDATED sq=0x00100160 target=0x004e2680 "
                 "stop=0x001001cc blocks=7 instructions=96\n";
}

} // namespace

int main(int argc, char** argv) {
    validate_synthetic_startup();

    if (argc == 2) {
        validate_external_startup(argv[1]);
    } else if (argc != 1) {
        fail("usage: r5900_block_dispatcher_startup_windows_tests.exe [external-elf-path]");
    }

    std::cout << "r5900_block_dispatcher_startup_windows_tests: PASS\n";
    return EXIT_SUCCESS;
}
