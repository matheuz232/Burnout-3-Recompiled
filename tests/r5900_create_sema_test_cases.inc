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
