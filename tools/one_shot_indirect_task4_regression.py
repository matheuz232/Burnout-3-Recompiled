from pathlib import Path

path = Path("tests/r5900_block_dispatcher_windows_tests.cpp")
text = path.read_text(encoding="utf-8")
old = '''    {
        const auto prefix = i_type(0x09, 0, 1, 7u);
        const auto delay = i_type(0x09, 0, 2, 9u);
        const std::vector<std::uint32_t> terminators = {
            r_type(31, 0, 0, 0, 0x08),
            r_type(31, 0, 30, 0, 0x09),
        };

        for (const auto terminator : terminators) {
            auto memory = make_memory({prefix, terminator, delay}, base);
            R5900BlockDispatcher dispatcher(memory);
            R5900IrExecutionState state{};

            const auto result = dispatcher.run(base, state, 1u);
            expect(result.reason == R5900DispatchStopReason::ControlFlow,
                   "supported prefix must stop before unsupported indirect control-flow terminator");
            expect(result.next_pc == base + 4u,
                   "control-flow boundary PC must be exact");
            expect(result.blocks_executed == 1u && result.instructions_executed == 1u,
                   "only straight-line prefix must execute");
            expect(state.gpr[1].low64 == 7u,
                   "straight-line prefix must execute before control-flow stop");
            expect(state.gpr[2].low64 == 0u,
                   "unsupported control-flow delay slot must not execute");
        }
    }
'''
new = '''    {
        const auto prefix = i_type(0x09, 0, 1, 7u);
        const auto bne = i_type(0x05, 0, 0, 0u);
        const auto delay = i_type(0x09, 0, 2, 9u);
        auto memory = make_memory({prefix, bne, delay}, base);
        R5900BlockDispatcher dispatcher(memory);
        R5900IrExecutionState state{};

        const auto result = dispatcher.run(base, state, 1u);
        expect(result.reason == R5900DispatchStopReason::ControlFlow,
               "supported prefix must stop before unsupported BNE terminator");
        expect(result.next_pc == base + 4u,
               "control-flow boundary PC must be exact");
        expect(result.blocks_executed == 1u && result.instructions_executed == 1u,
               "only straight-line prefix must execute");
        expect(state.gpr[1].low64 == 7u,
               "straight-line prefix must execute before control-flow stop");
        expect(state.gpr[2].low64 == 0u,
               "unsupported BNE delay slot must not execute");
    }
'''
if text.count(old) != 1:
    raise SystemExit("obsolete indirect-boundary regression block missing or duplicated")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("Task 4 generic dispatcher regression updated")
# trigger one-shot runner
