#include "recompiler/ps2_elf.h"
#include "recompiler/windows/r5900_block_dispatcher.h"
#include "runtime/ps2_memory_map.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

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

b3r::runtime::Ps2MemoryMap make_memory(const std::vector<std::uint32_t>& words,
                                       std::uint32_t base) {
    constexpr std::uint32_t kProgramHeaderOffset = 52u;
    constexpr std::uint32_t kPayloadOffset = 0x100u;
    const auto payload_size = static_cast<std::uint32_t>(words.size() * 4u);
    Bytes bytes(static_cast<std::size_t>(kPayloadOffset + payload_size + 0x40u), 0u);

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
    put_u16(bytes, 42u, 32u);
    put_u16(bytes, 44u, 1u);

    put_u32(bytes, kProgramHeaderOffset + 0u, 1u);
    put_u32(bytes, kProgramHeaderOffset + 4u, kPayloadOffset);
    put_u32(bytes, kProgramHeaderOffset + 8u, base);
    put_u32(bytes, kProgramHeaderOffset + 12u, base);
    put_u32(bytes, kProgramHeaderOffset + 16u, payload_size);
    put_u32(bytes, kProgramHeaderOffset + 20u, payload_size);
    put_u32(bytes, kProgramHeaderOffset + 24u, 5u);
    put_u32(bytes, kProgramHeaderOffset + 28u, 0x1000u);

    for (std::size_t index = 0; index < words.size(); ++index) {
        put_u32(bytes,
                static_cast<std::size_t>(kPayloadOffset) + index * 4u,
                words[index]);
    }

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

std::vector<std::uint32_t> make_synthetic_startup_prefix_words(std::uint32_t base) {
    std::vector<std::uint32_t> words;
    words.reserve(76u);

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

    expect(words.size() == 74u, "synthetic startup fixture count mismatch");
    expect(base + static_cast<std::uint32_t>(words.size() * 4u) == 0x00100130u,
           "synthetic startup fixture boundary mismatch");

    words.push_back(i_type(0x04u, 0u, 0u, 1u));
    words.push_back(i_type(0x09u, 0u, 4u, 1u));
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

    R5900BlockDispatcherOptions options{};
    options.block_options.max_instructions = 256u;
    R5900BlockDispatcher dispatcher(*built.memory, options);

    R5900IrExecutionState state{};
    state.gpr[31] = {0x1122334455667788ull, 0x8877665544332211ull};

    const auto result = dispatcher.run(parsed.image->entry_point(), state, 1u);
    expect(result.reason == R5900DispatchStopReason::ControlFlow,
           "real startup dispatch must stop before control flow");
    expect(result.next_pc == 0x00100130u,
           "real startup control-flow boundary mismatch");
    expect(result.blocks_executed == 1u && result.instructions_executed == 74u,
           "real startup dispatcher must execute exactly 74 instructions");
    expect(state.gpr[2].low64 == 0x00000000004e2680ull,
           "real startup r2 result mismatch");
    expect(state.gpr[3].low64 == 0x0000000001ecea00ull,
           "real startup r3 result mismatch");
    expect(state.gpr[4].low64 == 0u,
           "real startup must stop before BEQ delay slot");
    expect(state.gpr[31].low64 == 0u && state.gpr[31].high64 == 0u,
           "real startup PADDUW must clear GPR31");
    expect(state.hi == 0u && state.lo == 0u && state.hi1 == 0u && state.lo1 == 0u,
           "real startup HI/LO state mismatch");
    expect(state.sa == 0u && state.fcr31 == 0u && state.fp_acc == 0u,
           "real startup SA/COP1 state mismatch");
    for (const auto raw : state.fpr) {
        expect(raw == 0u, "real startup FPR must remain raw zero");
    }

    std::cout << "REAL_ELF_STARTUP_VALIDATED start=0x00100008 stop=0x00100130 instructions=74\n";
}

void validate_synthetic_startup() {
    using namespace b3r::recompiler;

    constexpr std::uint32_t base = 0x00100008u;
    const auto words = make_synthetic_startup_prefix_words(base);
    auto memory = make_memory(words, base);

    R5900BlockDispatcherOptions options{};
    options.block_options.max_instructions = 128u;
    R5900BlockDispatcher dispatcher(memory, options);

    R5900IrExecutionState state{};
    state.gpr[31] = {0x1122334455667788ull, 0x8877665544332211ull};

    const auto result = dispatcher.run(base, state, 1u);
    expect(result.reason == R5900DispatchStopReason::ControlFlow,
           "synthetic startup dispatch must stop before BEQ");
    expect(result.next_pc == 0x00100130u,
           "synthetic startup BEQ boundary PC mismatch");
    expect(result.blocks_executed == 1u && result.instructions_executed == 74u,
           "synthetic startup dispatcher must execute exactly 74 instructions");
    expect(state.gpr[2].low64 == 0x00000000004e2680ull,
           "synthetic startup r2 result mismatch");
    expect(state.gpr[3].low64 == 0x0000000001ecea00ull,
           "synthetic startup r3 result mismatch");
    expect(state.gpr[4].low64 == 0u,
           "synthetic startup delay slot executed unexpectedly");
    expect(state.hi == 0u && state.lo == 0u && state.hi1 == 0u && state.lo1 == 0u,
           "synthetic startup HI/LO state mismatch");
    expect(state.sa == 0u && state.fcr31 == 0u && state.fp_acc == 0u,
           "synthetic startup SA/COP1 state mismatch");
    for (const auto raw : state.fpr) {
        expect(raw == 0u, "synthetic startup FPR must remain raw zero");
    }
    expect(state.gpr[31].low64 == 0x1122334455667788ull &&
               state.gpr[31].high64 == 0x8877665544332211ull,
           "synthetic startup sentinel GPR31 must remain unchanged");

    std::cout << "SYNTHETIC_STARTUP_PREFIX_VALIDATED start=0x00100008 stop=0x00100130 instructions=74\n";
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
