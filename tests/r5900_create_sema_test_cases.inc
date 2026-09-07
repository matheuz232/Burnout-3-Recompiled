    const auto make_sema_memory = []() {
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
        return std::move(*built.memory);
    };

    const auto write_sema_descriptor = [](
        b3r::runtime::Ps2MemoryMap& target,
        std::uint32_t address,
        std::uint32_t max_count,
        std::uint32_t init_count,
        std::uint32_t attr,
        std::uint32_t option) {
        return target.write_u32(address + 0u, 0x7fffffffu) &&
               target.write_u32(address + 4u, max_count) &&
               target.write_u32(address + 8u, init_count) &&
               target.write_u32(address + 12u, 0xdeadbeefu) &&
               target.write_u32(address + 16u, attr) &&
               target.write_u32(address + 20u, option);
    };

    {
        R5900IrExecutionState unavailable{};
        unavailable.gpr[2] = {0x1122334455667788ull, 0x8877665544332211ull};
        unavailable.gpr[3].low64 = 0x40u;
        unavailable.gpr[4].low64 = 0x00100000u;
        const auto before_unavailable = unavailable;
        const auto unavailable_result = service.handle(
            R5900HostSyscallRequest{0x0010be24u, 0x0000000cu},
            unavailable,
            memory);
        expect(unavailable_result.status == R5900HostSyscallStatus::Fault,
               "CreateSema with unavailable guest descriptor must fault");
        expect(unavailable_result.message.find("CreateSema") != std::string::npos,
               "CreateSema descriptor fault must identify the syscall");
        expect_states_equal(before_unavailable, unavailable,
                            "CreateSema descriptor fault must preserve guest state");
    }

    {
        auto sema_memory = make_sema_memory();
        constexpr std::uint32_t descriptor = 0x00100100u;
        expect(write_sema_descriptor(
                   sema_memory, descriptor, 1u, 1u, 0x20u, 0x12345678u),
               "CreateSema valid descriptor writes must succeed");

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
        const auto first_before = sema_state;

        const auto first = sema_service.handle(
            R5900HostSyscallRequest{0x0010be24u, 0x0000000cu},
            sema_state,
            sema_memory);
        expect(first.status == R5900HostSyscallStatus::Handled &&
                   sema_state.gpr[2].low64 == 1u,
               "first CreateSema must return deterministic id 1");
        expect(sema_state.gpr[2].high64 == first_before.gpr[2].high64,
               "first CreateSema must preserve v0.high64");
        for (std::size_t index = 0u; index < 32u; ++index) {
            if (index == 2u) {
                continue;
            }
            expect(sema_state.gpr[index].low64 == first_before.gpr[index].low64 &&
                       sema_state.gpr[index].high64 == first_before.gpr[index].high64,
                   "CreateSema must preserve unrelated GPR state");
        }
        expect(sema_state.hi == first_before.hi && sema_state.lo == first_before.lo &&
                   sema_state.hi1 == first_before.hi1 && sema_state.lo1 == first_before.lo1 &&
                   sema_state.sa == first_before.sa && sema_state.fcr31 == first_before.fcr31 &&
                   sema_state.fp_acc == first_before.fp_acc && sema_state.fpr == first_before.fpr,
               "CreateSema must preserve unrelated special/FPR state");
        expect(sema_memory.read_u32(descriptor + 0u).value_or(0u) == 0x7fffffffu &&
                   sema_memory.read_u32(descriptor + 12u).value_or(0u) == 0xdeadbeefu,
               "CreateSema must ignore count/wait_threads and preserve descriptor memory");

        const auto second_v0_high = sema_state.gpr[2].high64;
        const auto second = sema_service.handle(
            R5900HostSyscallRequest{0x0010be28u, 0x0000000cu},
            sema_state,
            sema_memory);
        expect(second.status == R5900HostSyscallStatus::Handled &&
                   sema_state.gpr[2].low64 == 2u,
               "second CreateSema must return deterministic id 2");
        expect(sema_state.gpr[2].high64 == second_v0_high,
               "second CreateSema must preserve v0.high64");

        const auto expect_fault_preserves_id = [&](std::uint32_t max_count,
                                                    std::uint32_t init_count,
                                                    const char* label) {
            expect(write_sema_descriptor(
                       sema_memory, descriptor, max_count, init_count, 0x20u, 0x12345678u),
                   "CreateSema invalid descriptor writes must succeed");
            const auto before_fault = sema_state;
            const auto fault = sema_service.handle(
                R5900HostSyscallRequest{0x0010be2cu, 0x0000000cu},
                sema_state,
                sema_memory);
            expect(fault.status == R5900HostSyscallStatus::Fault, label);
            expect_states_equal(before_fault, sema_state,
                                "invalid CreateSema must preserve guest state");
        };

        expect_fault_preserves_id(0u, 1u,
                                  "CreateSema max_count zero must fault");
        expect_fault_preserves_id(0xffffffffu, 0u,
                                  "CreateSema negative max_count must fault");
        expect_fault_preserves_id(2u, 3u,
                                  "CreateSema init_count above max_count must fault");
        expect_fault_preserves_id(2u, 0xffffffffu,
                                  "CreateSema negative init_count must fault");

        expect(write_sema_descriptor(
                   sema_memory, descriptor, 2u, 1u, 0x40u, 0xabcdef01u),
               "CreateSema final valid descriptor writes must succeed");
        sema_state.gpr[4].low64 = descriptor + 2u;
        const auto before_misaligned = sema_state;
        const auto misaligned = sema_service.handle(
            R5900HostSyscallRequest{0x0010be30u, 0x0000000cu},
            sema_state,
            sema_memory);
        expect(misaligned.status == R5900HostSyscallStatus::Fault,
               "CreateSema misaligned descriptor must fault");
        expect(misaligned.message.find("align") != std::string::npos,
               "CreateSema misalignment diagnostic must identify alignment");
        expect_states_equal(before_misaligned, sema_state,
                            "CreateSema misalignment must preserve guest state");

        sema_state.gpr[4].low64 = descriptor;
        const auto third = sema_service.handle(
            R5900HostSyscallRequest{0x0010be34u, 0x0000000cu},
            sema_state,
            sema_memory);
        expect(third.status == R5900HostSyscallStatus::Handled &&
                   sema_state.gpr[2].low64 == 3u,
               "CreateSema faults must not consume deterministic ids");

        const auto& contexts = sema_service.sema_contexts();
        expect(contexts.size() == 3u,
               "CreateSema registry must contain only successful creations");
        expect(contexts[0].id == 1 && contexts[0].current_count == 1 &&
                   contexts[0].max_count == 1 && contexts[0].attr == 0x20u &&
                   contexts[0].option == 0x12345678u,
               "first CreateSema registry context mismatch");
        expect(contexts[1].id == 2 && contexts[1].current_count == 1 &&
                   contexts[1].max_count == 1 && contexts[1].attr == 0x20u &&
                   contexts[1].option == 0x12345678u,
               "second CreateSema registry context mismatch");
        expect(contexts[2].id == 3 && contexts[2].current_count == 1 &&
                   contexts[2].max_count == 2 && contexts[2].attr == 0x40u &&
                   contexts[2].option == 0xabcdef01u,
               "third CreateSema registry context mismatch");
    }
