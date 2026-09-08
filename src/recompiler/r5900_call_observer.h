#pragma once

#include <array>
#include <cstdint>

namespace b3r::recompiler {

struct R5900CallObservation {
    std::uint32_t call_pc{};
    std::uint32_t target_pc{};
    std::uint32_t return_pc{};
    bool indirect{};
    std::array<std::uint64_t, 4> args{};
};

class IR5900CallObserver {
public:
    virtual ~IR5900CallObserver() = default;

    virtual void observe(const R5900CallObservation& observation) noexcept = 0;
};

} // namespace b3r::recompiler
