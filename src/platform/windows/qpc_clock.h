#pragma once

namespace b3r::platform::windows {

class QpcClock {
public:
    [[nodiscard]] static double now_seconds() noexcept;
    [[nodiscard]] static double frequency_hz() noexcept;
};

} // namespace b3r::platform::windows
