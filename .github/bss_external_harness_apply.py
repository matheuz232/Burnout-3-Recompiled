from pathlib import Path
import subprocess

path = Path('tests/r5900_block_dispatcher_startup_windows_tests.cpp')
text = path.read_text(encoding='utf-8')
start = text.index('void validate_external_startup(const char* path) {')
end = text.index('\nvoid validate_synthetic_startup()', start)
new_function = r'''void validate_external_startup(const char* path) {
    using namespace b3r::recompiler;

    constexpr std::uint32_t kBssEnd = 0x01ecea00u;
    constexpr std::uint32_t kSetupThreadSyscallPc = 0x001001c8u;
    constexpr std::size_t kBssIterations =
        static_cast<std::size_t>((kBssEnd - kSqTarget) / 16u);
    constexpr std::size_t kExpectedBlocks = 2u * kBssIterations + 4u;
    constexpr std::size_t kExpectedInstructions = 8u * kBssIterations + 95u;
    constexpr std::size_t kExpectedFastCacheHits = 2u * (kBssIterations - 1u);
    static_assert(kBssIterations == 1698872u);
    static_assert(kExpectedBlocks == 3397748u);
    static_assert(kExpectedInstructions == 13591071u);
    static_assert(kExpectedFastCacheHits == 3397742u);

    const auto bytes = read_binary_file(path);
    const auto parsed = parse_ps2_elf(bytes);
    expect(parsed.ok(), "external ELF must parse as a PS2 ELF");
    expect(parsed.image->entry_point() == 0x00100008u,
           "external ELF entry point mismatch");

    auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
    expect(built.ok(), "external ELF must map into PS2 memory");

    const auto bss_begin_before = built.memory->read_u128(kSqTarget);
    const auto bss_end_before = built.memory->read_u128(kBssEnd - 16u);
    expect(bss_begin_before.has_value() && bss_end_before.has_value(),
           "external ELF must map the complete BSS clear range");

    R5900BlockDispatcherOptions options{};
    options.block_options.max_instructions = 256u;
    R5900BlockDispatcher dispatcher(*built.memory, options);

    R5900IrExecutionState state{};
    state.gpr[31] = {0x1122334455667788ull, 0x8877665544332211ull};

    const auto result = dispatcher.run(
        parsed.image->entry_point(), state, kExpectedBlocks + 1u);
    expect(result.reason == R5900DispatchStopReason::Trap,
           "real startup must stop at SetupThread syscall after the BSS clear loop");
    expect(result.next_pc == kSetupThreadSyscallPc,
           "real startup must reach the exact SetupThread syscall PC");
    expect(result.blocks_executed == kExpectedBlocks,
           "real startup BSS clear block count mismatch");
    expect(result.instructions_executed == kExpectedInstructions,
           "real startup BSS clear selected-word count mismatch");
    expect(result.cache_misses == 6u,
           "real startup must compile exactly six distinct blocks before SetupThread");
    expect(result.cache_hits == kExpectedFastCacheHits &&
               result.fast_cache_hits == kExpectedFastCacheHits,
           "real startup repeated BSS blocks must use fast cache replay");
    expect(result.recompilations == 0u,
           "real startup BSS clear must not recompile stable guest code");

    const auto bss_begin_after = built.memory->read_u128(kSqTarget);
    const auto bss_end_after = built.memory->read_u128(kBssEnd - 16u);
    expect(bss_begin_after.has_value() && bss_end_after.has_value(),
           "external BSS clear range must remain mapped after execution");
    expect((*bss_begin_after)[0] == 0u && (*bss_begin_after)[1] == 0u &&
               (*bss_end_after)[0] == 0u && (*bss_end_after)[1] == 0u,
           "real startup must leave the complete BSS clear endpoints zeroed");

    expect(state.gpr[2].low64 == 0x0000000001ecea00ull,
           "real startup BSS pointer must finish at the exact BSS end");
    expect(state.gpr[3].low64 == 0x000000000000003cull,
           "real startup must select SetupThread syscall 0x3c");
    expect(state.gpr[4].low64 == 0x00000000004e8670ull,
           "real startup SetupThread arg0 mismatch");
    expect(state.gpr[5].low64 == 0x0000000001ff0000ull,
           "real startup SetupThread arg1 mismatch");
    expect(state.gpr[6].low64 == 0x0000000000010000ull,
           "real startup SetupThread arg2 mismatch");
    expect(state.gpr[7].low64 == 0x0000000001d9ce80ull,
           "real startup SetupThread arg3 mismatch");
    expect(state.gpr[8].low64 == 0x0000000000100220ull,
           "real startup SetupThread arg4 mismatch");
    expect(state.gpr[28].low64 == 0x00000000004e8670ull,
           "real startup global pointer setup mismatch");
    expect(state.gpr[31].low64 == 0u && state.gpr[31].high64 == 0u,
           "real startup PADDUW must clear GPR31");
    expect(state.hi == 0u && state.lo == 0u && state.hi1 == 0u && state.lo1 == 0u,
           "real startup HI/LO state mismatch");
    expect(state.sa == 0u && state.fcr31 == 0u && state.fp_acc == 0u,
           "real startup SA/COP1 state mismatch");
    for (const auto raw : state.fpr) {
        expect(raw == 0u, "real startup FPR must remain raw zero");
    }

    std::cout << "REAL_ELF_BSS_CLEAR_VALIDATED begin=0x" << std::hex
              << std::setw(8) << std::setfill('0') << kSqTarget
              << " end=0x" << std::setw(8) << kBssEnd
              << " stop=0x" << std::setw(8) << result.next_pc
              << std::dec << " iterations=" << kBssIterations
              << " blocks=" << result.blocks_executed
              << " instructions=" << result.instructions_executed
              << " fast_cache_hits=" << result.fast_cache_hits << '\n';
}
'''
path.write_text(text[:start] + new_function + text[end:], encoding='utf-8')

subprocess.run(['git', 'config', 'user.name', 'github-actions[bot]'], check=True)
subprocess.run(['git', 'config', 'user.email', '41898282+github-actions[bot]@users.noreply.github.com'], check=True)
subprocess.run(['git', 'add', str(path)], check=True)
if subprocess.run(['git', 'diff', '--cached', '--quiet']).returncode != 0:
    subprocess.run(['git', 'commit', '-m', 'test: require full external BSS clear startup'], check=True)
    subprocess.run(['git', 'push'], check=True)
