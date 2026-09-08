#pragma once

#include "analysis/ps2_pad_binding_discovery.h"
#include "recompiler/r5900_call_observer.h"
#include "runtime/ps2_memory_map.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace b3r::analysis {

enum class PadRuntimeConfirmationStatus : std::uint8_t {
    Unobserved,
    ObservedIncompatible,
    RuntimeConfirmed,
    RuntimeAmbiguous,
};

struct Ps2PadRuntimePcEvidence {
    PadBindingFunction function{};
    std::uint32_t guest_pc{};
    std::size_t calls_observed{};
    std::size_t compatible_calls{};
    std::size_t incompatible_calls{};
    std::optional<recompiler::R5900CallObservation> first_compatible{};
    std::optional<recompiler::R5900CallObservation> first_incompatible{};
};

struct PadRuntimeFunctionResult {
    PadBindingFunction function{};
    PadBindingConfidence static_confidence{PadBindingConfidence::Unresolved};
    PadRuntimeConfirmationStatus runtime_status{PadRuntimeConfirmationStatus::Unobserved};
    std::optional<std::uint32_t> guest_pc{};
    std::size_t calls_observed{};
    std::size_t compatible_calls{};
    std::size_t incompatible_calls{};
    std::vector<Ps2PadRuntimePcEvidence> pc_evidence{};
};

struct PadRuntimeConfirmationResult {
    std::array<PadRuntimeFunctionResult, 6> functions{};
};

namespace ps2_pad_runtime_confirmation_detail {

inline void saturating_increment(std::size_t& value) noexcept {
    if (value != std::numeric_limits<std::size_t>::max()) {
        ++value;
    }
}

[[nodiscard]] inline std::size_t saturating_add(std::size_t lhs,
                                                std::size_t rhs) noexcept {
    const auto max = std::numeric_limits<std::size_t>::max();
    return rhs > max - lhs ? max : lhs + rhs;
}

[[nodiscard]] inline std::uint32_t arg32(
    const recompiler::R5900CallObservation& observation,
    std::size_t index) noexcept {
    return static_cast<std::uint32_t>(observation.args[index]);
}

[[nodiscard]] inline bool abi_compatible(
    PadBindingFunction function,
    const recompiler::R5900CallObservation& observation,
    const runtime::Ps2MemoryMap& memory) noexcept {
    const auto a0 = arg32(observation, 0u);
    const auto a1 = arg32(observation, 1u);
    const auto a2 = arg32(observation, 2u);

    switch (function) {
    case PadBindingFunction::PadInit:
        return a0 == 0u;
    case PadBindingFunction::PadPortOpen:
        return a0 == 0u && a1 == 0u && a2 != 0u &&
               (a2 % 64u) == 0u && memory.translate(a2, 256u).has_value();
    case PadBindingFunction::PadGetState:
        return a0 == 0u && a1 == 0u;
    case PadBindingFunction::PadRead:
        return a0 == 0u && a1 == 0u && a2 != 0u &&
               memory.translate(a2, 32u).has_value();
    case PadBindingFunction::PadPortClose:
        return a0 == 0u && a1 == 0u;
    case PadBindingFunction::PadEnd:
        return true;
    }
    return false;
}

} // namespace ps2_pad_runtime_confirmation_detail

class Ps2PadRuntimeConfirmation final : public recompiler::IR5900CallObserver {
public:
    Ps2PadRuntimeConfirmation(const PadBindingDiscoveryResult& discovery,
                              const runtime::Ps2MemoryMap& memory)
        : memory_(memory) {
        for (std::size_t index = 0; index < state_.functions.size(); ++index) {
            const auto function = static_cast<PadBindingFunction>(index);
            auto& destination = state_.functions[index];
            destination.function = function;

            const auto& resolution = discovery.resolutions[index];
            destination.static_confidence = resolution.confidence;

            std::vector<std::uint32_t> pcs{};
            pcs.reserve(resolution.evidence.size());
            for (const auto& evidence : resolution.evidence) {
                if (evidence.function == function) {
                    pcs.push_back(evidence.guest_pc);
                }
            }
            std::sort(pcs.begin(), pcs.end());
            pcs.erase(std::unique(pcs.begin(), pcs.end()), pcs.end());
            destination.pc_evidence.reserve(pcs.size());
            for (const auto pc : pcs) {
                destination.pc_evidence.push_back(
                    Ps2PadRuntimePcEvidence{function, pc});
            }
        }
    }

    void observe(const recompiler::R5900CallObservation& observation) noexcept override {
        for (auto& function : state_.functions) {
            for (auto& pc : function.pc_evidence) {
                if (pc.guest_pc != observation.target_pc) {
                    continue;
                }

                ps2_pad_runtime_confirmation_detail::saturating_increment(
                    pc.calls_observed);
                if (ps2_pad_runtime_confirmation_detail::abi_compatible(
                        function.function, observation, memory_)) {
                    ps2_pad_runtime_confirmation_detail::saturating_increment(
                        pc.compatible_calls);
                    if (!pc.first_compatible.has_value()) {
                        pc.first_compatible = observation;
                    }
                } else {
                    ps2_pad_runtime_confirmation_detail::saturating_increment(
                        pc.incompatible_calls);
                    if (!pc.first_incompatible.has_value()) {
                        pc.first_incompatible = observation;
                    }
                }
            }
        }
    }

    [[nodiscard]] PadRuntimeConfirmationResult result() const {
        auto result = state_;
        for (auto& function : result.functions) {
            function.calls_observed = 0u;
            function.compatible_calls = 0u;
            function.incompatible_calls = 0u;
            function.guest_pc.reset();

            std::size_t compatible_pc_count{};
            std::uint32_t unique_compatible_pc{};
            for (const auto& pc : function.pc_evidence) {
                function.calls_observed =
                    ps2_pad_runtime_confirmation_detail::saturating_add(
                        function.calls_observed, pc.calls_observed);
                function.compatible_calls =
                    ps2_pad_runtime_confirmation_detail::saturating_add(
                        function.compatible_calls, pc.compatible_calls);
                function.incompatible_calls =
                    ps2_pad_runtime_confirmation_detail::saturating_add(
                        function.incompatible_calls, pc.incompatible_calls);
                if (pc.compatible_calls != 0u) {
                    ++compatible_pc_count;
                    unique_compatible_pc = pc.guest_pc;
                }
            }

            if (compatible_pc_count >= 2u) {
                function.runtime_status = PadRuntimeConfirmationStatus::RuntimeAmbiguous;
            } else if (compatible_pc_count == 1u) {
                function.runtime_status = PadRuntimeConfirmationStatus::RuntimeConfirmed;
                function.guest_pc = unique_compatible_pc;
            } else if (function.calls_observed != 0u) {
                function.runtime_status =
                    PadRuntimeConfirmationStatus::ObservedIncompatible;
            } else {
                function.runtime_status = PadRuntimeConfirmationStatus::Unobserved;
            }
        }
        return result;
    }

private:
    const runtime::Ps2MemoryMap& memory_;
    PadRuntimeConfirmationResult state_{};
};

} // namespace b3r::analysis
