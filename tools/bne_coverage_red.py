from pathlib import Path

# Extend the generic dispatcher matrix with BNE taken/not-taken behavior on the
# same cached guest block plus delay-word invalidation.
path = Path('tests/r5900_block_dispatcher_windows_tests.cpp')
text = path.read_text(encoding='utf-8')
anchor = '''    {\n        const std::vector<std::uint32_t> words = {\n            i_type(0x09, 0, 1, 1u),\n'''
insert = '''    {\n        const auto bne = i_type(0x05, 1, 2, 2u);\n        const auto delay = i_type(0x09, 3, 3, 1u);\n        auto memory = make_memory({bne, delay, 0u, 0u}, base);\n        R5900BlockDispatcher dispatcher(memory);\n\n        R5900IrExecutionState equal_state{};\n        equal_state.gpr[1].low64 = 5u;\n        equal_state.gpr[2].low64 = 5u;\n        const auto equal_result = dispatcher.run(base, equal_state, 1u);\n        expect(equal_result.reason == R5900DispatchStopReason::BlockBudgetExhausted &&\n                   equal_result.next_pc == base + 8u,\n               "equal BNE operands must select fallthrough PC");\n        expect(equal_result.cache_misses == 1u && equal_result.cache_hits == 0u &&\n                   equal_state.gpr[3].low64 == 1u,\n               "first BNE execution must miss cache and execute delay once");\n\n        R5900IrExecutionState unequal_state{};\n        unequal_state.gpr[1].low64 = 5u;\n        unequal_state.gpr[2].low64 = 6u;\n        const auto unequal_result = dispatcher.run(base, unequal_state, 1u);\n        expect(unequal_result.reason == R5900DispatchStopReason::BlockBudgetExhausted &&\n                   unequal_result.next_pc == base + 12u,\n               "unequal BNE operands must select branch target");\n        expect(unequal_result.cache_hits == 1u && unequal_result.recompilations == 0u &&\n                   unequal_state.gpr[3].low64 == 1u,\n               "BNE runtime outcome change must reuse cached native block");\n\n        expect(memory.write_u32(base + 4u, i_type(0x09, 3, 3, 2u)),\n               "BNE delay mutation must succeed");\n        R5900IrExecutionState mutated_state{};\n        mutated_state.gpr[1].low64 = 7u;\n        mutated_state.gpr[2].low64 = 8u;\n        const auto mutated_result = dispatcher.run(base, mutated_state, 1u);\n        expect(mutated_result.next_pc == base + 12u &&\n                   mutated_result.recompilations == 1u &&\n                   mutated_result.cache_hits == 0u &&\n                   mutated_state.gpr[3].low64 == 2u,\n               "BNE delay mutation must invalidate cache and execute new delay");\n    }\n\n'''
if text.count(anchor) != 1:
    raise SystemExit('generic dispatcher insertion anchor not found exactly once')
text = text.replace(anchor, insert + anchor, 1)
path.write_text(text, encoding='utf-8')

# Add BNE+SQ delay rejection coverage. Current production behavior rejects it,
# but the diagnostic still incorrectly calls this a JR/JALR delay slot; that is
# the intentional RED assertion.
path = Path('tests/r5900_block_dispatcher_store128_windows_tests.cpp')
text = path.read_text(encoding='utf-8')
anchor = '''    std::cout << "r5900_block_dispatcher_store128_windows_tests: PASS\\n";\n'''
insert = '''    {\n        const auto bne = i_type(0x05u, 1u, 2u, 2u);\n        auto memory = make_memory({bne, sq_r7_r2, 0u, 0u}, code_base, data_base);\n        const auto before = memory.read_u128(target);\n        expect(before.has_value(), "BNE SQ-delay target must be mapped");\n        R5900BlockDispatcher dispatcher(memory);\n        R5900IrExecutionState state{};\n        state.gpr[1].low64 = 1u;\n        state.gpr[2].low64 = target;\n        state.gpr[7] = {9u, 10u};\n\n        const auto result = dispatcher.run(code_base, state, 1u);\n        expect(result.reason == R5900DispatchStopReason::LoweringFailure &&\n                   result.next_pc == code_base + 4u &&\n                   result.blocks_executed == 0u && result.instructions_executed == 0u,\n               "SQ in BNE delay slot must fail before guest progress");\n        expect(dispatcher.cache_size() == 0u,\n               "rejected BNE SQ delay must not populate cache");\n        const auto after = memory.read_u128(target);\n        expect(after.has_value() && *after == *before,\n               "rejected BNE SQ delay must not mutate guest memory");\n        expect(result.message.find("BEQ/BNE") != std::string::npos,\n               "BNE SQ-delay diagnostic must identify BEQ/BNE scope boundary");\n    }\n\n'''
if text.count(anchor) != 1:
    raise SystemExit('Store128 insertion anchor not found exactly once')
text = text.replace(anchor, insert + anchor, 1)
path.write_text(text, encoding='utf-8')
