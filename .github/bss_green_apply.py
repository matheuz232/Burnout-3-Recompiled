from pathlib import Path
import subprocess

# Trigger one-shot production patch after the RED is confirmed.
path = Path('src/recompiler/windows/r5900_block_dispatcher.cpp')
text = path.read_text(encoding='utf-8')

fingerprint = '''std::uint64_t fingerprint_guest_words(\n    std::uint32_t start_pc,\n    const std::vector<std::uint32_t>& words) noexcept {\n    std::uint64_t hash = kFnvOffset;\n    fnv_u32_le(hash, start_pc);\n    fnv_u64_le(hash, static_cast<std::uint64_t>(words.size()));\n    for (const auto word : words) {\n        fnv_u32_le(hash, word);\n    }\n    return hash;\n}\n'''
helper = fingerprint + '''\nbool cached_guest_words_match(\n    const runtime::Ps2MemoryMap& memory,\n    std::uint32_t start_pc,\n    const std::vector<std::uint32_t>& expected) noexcept {\n    for (std::size_t index = 0; index < expected.size(); ++index) {\n        const auto pc = start_pc + static_cast<std::uint32_t>(index * 4u);\n        const auto word = memory.read_u32(pc);\n        if (!word.has_value() || *word != expected[index]) {\n            return false;\n        }\n    }\n    return true;\n}\n'''
if 'bool cached_guest_words_match(' not in text:
    if fingerprint not in text:
        raise SystemExit('fingerprint marker not found')
    text = text.replace(fingerprint, helper, 1)

loop_marker = '''    while (result.blocks_executed < max_blocks) {\n        const auto analyzed = analysis::analyze_r5900_basic_block(\n'''
fast_path = '''    while (result.blocks_executed < max_blocks) {\n        auto fast_cached = cache_.find(current_pc);\n        if (fast_cached != cache_.end() &&\n            fast_cached->second.fast_replay_eligible &&\n            cached_guest_words_match(memory_, current_pc, fast_cached->second.guest_words)) {\n            R5900IrExecutionContext execution_context{};\n            execution_context.state = &state;\n            execution_context.memory.user = &memory_;\n            execution_context.memory.write128 = &ps2_memory_write128_adapter;\n\n            ++result.cache_hits;\n            ++result.fast_cache_hits;\n            const auto native_execution =\n                fast_cached->second.native_block.execute(execution_context);\n\n            if (!native_execution.ok()) {\n                if (native_execution.error == R5900IrExecutionError::MemoryAccessFailure &&\n                    execution_context.memory_fault.active) {\n                    const auto& fault = execution_context.memory_fault;\n                    std::size_t completed_prefix{};\n                    if (fault.guest_pc >= current_pc &&\n                        ((fault.guest_pc - current_pc) % 4u) == 0u) {\n                        completed_prefix = static_cast<std::size_t>(\n                            (fault.guest_pc - current_pc) / 4u);\n                    }\n                    result.instructions_executed += completed_prefix;\n                    result.reason = R5900DispatchStopReason::MemoryAccessFailure;\n                    result.next_pc = fault.guest_pc;\n                    result.message = format_stage_error(\n                        "runtime-memory",\n                        fault.guest_pc,\n                        format_memory_fault_detail(fault));\n                    return result;\n                }\n\n                result.reason = R5900DispatchStopReason::CompileFailure;\n                result.next_pc = current_pc;\n                result.message = format_stage_error(\n                    "x64 execute", current_pc, native_execution.message);\n                return result;\n            }\n\n            ++result.blocks_executed;\n            result.instructions_executed += fast_cached->second.guest_instruction_count;\n            current_pc = native_execution.next_pc;\n            result.next_pc = native_execution.next_pc;\n\n            if (result.blocks_executed == max_blocks) {\n                result.reason = R5900DispatchStopReason::BlockBudgetExhausted;\n                return result;\n            }\n            continue;\n        }\n\n        const auto analyzed = analysis::analyze_r5900_basic_block(\n'''
if 'auto fast_cached = cache_.find(current_pc);' not in text:
    if loop_marker not in text:
        raise SystemExit('dispatcher loop marker not found')
    text = text.replace(loop_marker, fast_path, 1)

count_marker = '''            replacement.guest_words = guest_words;\n            replacement.guest_instruction_count = guest_words.size();\n            replacement.native_block = std::move(*compiled.block);\n'''
count_replacement = '''            replacement.guest_words = guest_words;\n            replacement.guest_instruction_count = guest_words.size();\n            replacement.fast_replay_eligible = has_supported_transfer;\n            replacement.native_block = std::move(*compiled.block);\n'''
if 'replacement.fast_replay_eligible = has_supported_transfer;' not in text:
    if count_marker not in text:
        raise SystemExit('cache replacement marker not found')
    text = text.replace(count_marker, count_replacement, 1)

path.write_text(text, encoding='utf-8')

subprocess.run(['git', 'config', 'user.name', 'github-actions[bot]'], check=True)
subprocess.run(['git', 'config', 'user.email', '41898282+github-actions[bot]@users.noreply.github.com'], check=True)
subprocess.run(['git', 'add', str(path)], check=True)
if subprocess.run(['git', 'diff', '--cached', '--quiet']).returncode != 0:
    subprocess.run(['git', 'commit', '-m', 'feat: add transfer-block fast cache replay'], check=True)
    subprocess.run(['git', 'push'], check=True)
