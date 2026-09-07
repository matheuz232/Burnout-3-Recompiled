    {
        R5900IrExecutionState create_sema_state{};
        create_sema_state.gpr[2] = {
            0x1122334455667788ull,
            0x8877665544332211ull,
        };
        create_sema_state.gpr[3].low64 = 0x40u;
        create_sema_state.gpr[4].low64 = 0x00100000u;
        const auto create_sema_before = create_sema_state;

        const auto create_sema_result = service.handle(
            R5900HostSyscallRequest{0x0010be24u, 0x0000000cu},
            create_sema_state,
            memory);

        expect(create_sema_result.status == R5900HostSyscallStatus::Fault,
               "CreateSema with unavailable guest descriptor must fault");
        expect(create_sema_result.message.find("CreateSema") != std::string::npos,
               "CreateSema descriptor fault must identify the syscall");
        expect_states_equal(create_sema_before, create_sema_state,
                            "CreateSema descriptor fault must preserve guest state");
    }

    {
        std::vector<std::uint8_t> elf_bytes(0x200u, 0u);
        const auto put16 = [&elf_bytes](std::size_t offset, std::uint16_t value) {
            elf_bytes[offset + 0u] = static_cast<std::uint8_t>(value & 0xffu);
            elf_bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
        };
        const auto put32 = [&elf_bytes](std::size_t offset, std::uint32_t value) {
            elf_bytes[offset + 0u] = static_cast<std::uint8_t>(value & 0xffu);
            elf_bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
            elf_bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
            elf_bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
        };

        elf_bytes[0] = 0x7fu;
        elf_bytes[1] = 'E';
        elf_bytes[2] = 'L';
        elf_bytes[3] = 'F';
        elf_bytes[4] = 1u;
        elf_bytes[5] = 1u;
        elf_bytes[6] = 1u;
        put16(16u, 2u);
        put16(18u, 8u);
        put32(20u, 1u);
        put32(24u, 0x00100000u);
        put32(28u, 52u);
        put16(40u, 52u);
        put16(42u, 32u);
        put16(44u, 1u);

        constexpr std::size_t ph = 52u;
        put32(ph + 0u, 1u);
        put32(ph + 4u, 0x100u);
        put32(ph + 8u, 0x00100000u);
        put32(ph + 12u, 0x00100000u);
        put32(ph + 16u, 4u);
        put32(ph + 20u, 0x1000u);
        put32(ph + 24u, 6u);
        put32(ph + 28u, 0x1000u);

        auto parsed = parse_ps2_elf(elf_bytes);
        expect(parsed.ok(), "CreateSema synthetic ELF must parse");
        auto built = b3r::runtime::Ps2MemoryMap::from_elf(*parsed.image);
        expect(built.ok(), "CreateSema synthetic EE RAM must build");
        auto sema_memory = std::move(*built.memory);

        constexpr std::uint32_t descriptor = 0x00100100u;
        expect(sema_memory.write_u32(descriptor + 0u, 0x7fffffffu),
               "CreateSema test descriptor count write must succeed");
        expect(sema_memory.write_u32(descriptor + 4u, 1u),
               "CreateSema test descriptor max_count write must succeed");
        expect(sema_memory.write_u32(descriptor + 8u, 1u),
               "CreateSema test descriptor init_count write must succeed");
        expect(sema_memory.write_u32(descriptor + 12u, 0xdeadbeefu),
               "CreateSema test descriptor wait_threads write must succeed");
        expect(sema_memory.write_u32(descriptor + 16u, 0x20u),
               "CreateSema test descriptor attr write must succeed");
        expect(sema_memory.write_u32(descriptor + 20u, 0x12345678u),
               "CreateSema test descriptor option write must succeed");

        R5900HostSyscallService sema_service{};
        R5900IrExecutionState sema_state{};
        for (std::size_t index = 0u; index < 32u; ++index) {
            sema_state.gpr[index] = {
                0x1000000000000000ull + static_cast<std::uint64_t>(index),
                0x2000000000000000ull + static_cast<std::uint64_t>(index),
            };
        }
        sema_state.gpr[2] = {0x1122334455667788ull, 0x8877665544332211ull};
        sema_state.gpr[3].low64 = 0x40u;
        sema_state.gpr[4].low64 = descriptor;
        sema_state.hi = 0x1111222233334444ull;
        sema_state.lo = 0x5555666677778888ull;
        sema_state.hi1 = 0x9999aaaabbbbccccull;
        sema_state.lo1 = 0xddddeeeeffff0000ull;
        sema_state.sa = 11u;
        sema_state.fpr[3] = 0x3f800000u;
        sema_state.fcr31 = 0x1234u;
        sema_state.fp_acc = 0x5678u;
        const auto sema_before = sema_state;

        const auto result = sema_service.handle(
            R5900HostSyscallRequest{0x0010be24u, 0x0000000cu},
            sema_state,
            sema_memory);

        expect(result.status == R5900HostSyscallStatus::Handled,
               "valid CreateSema descriptor must be handled");
        expect(sema_state.gpr[2].low64 == 1u,
               "first CreateSema must return deterministic id 1 in v0.low64");
        expect(sema_state.gpr[2].high64 == sema_before.gpr[2].high64,
               "CreateSema must preserve v0.high64");
        for (std::size_t index = 0u; index < 32u; ++index) {
            if (index == 2u) {
                continue;
            }
            expect(sema_state.gpr[index].low64 == sema_before.gpr[index].low64 &&
                       sema_state.gpr[index].high64 == sema_before.gpr[index].high64,
                   "CreateSema must preserve unrelated GPR state");
        }
        expect(sema_state.hi == sema_before.hi && sema_state.lo == sema_before.lo &&
                   sema_state.hi1 == sema_before.hi1 && sema_state.lo1 == sema_before.lo1 &&
                   sema_state.sa == sema_before.sa && sema_state.fcr31 == sema_before.fcr31 &&
                   sema_state.fp_acc == sema_before.fp_acc && sema_state.fpr == sema_before.fpr,
               "CreateSema must preserve unrelated special/FPR state");
        expect(sema_memory.read_u32(descriptor + 0u).value_or(0u) == 0x7fffffffu &&
                   sema_memory.read_u32(descriptor + 4u).value_or(0u) == 1u &&
                   sema_memory.read_u32(descriptor + 8u).value_or(0u) == 1u &&
                   sema_memory.read_u32(descriptor + 12u).value_or(0u) == 0xdeadbeefu &&
                   sema_memory.read_u32(descriptor + 16u).value_or(0u) == 0x20u &&
                   sema_memory.read_u32(descriptor + 20u).value_or(0u) == 0x12345678u,
               "CreateSema must not mutate the guest descriptor");
    }
