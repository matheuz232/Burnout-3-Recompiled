#include "core/frame_schedule.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void expect_near(double actual, double expected, double epsilon, const char* label) {
    if (std::abs(actual - expected) > epsilon) {
        std::cerr << label << ": expected " << expected << ", got " << actual << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void expect_true(bool condition, const char* label) {
    if (!condition) {
        std::cerr << label << ": expected true\n";
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using b3r::core::FrameSchedule;

    FrameSchedule schedule{120.0, 10.0};
    expect_near(schedule.period_seconds(), 1.0 / 120.0, 1e-12, "120 Hz period");
    expect_near(schedule.deadline_seconds(), 10.0 + (1.0 / 120.0), 1e-12, "initial deadline");

    const double first_deadline = schedule.deadline_seconds();
    schedule.advance_after_frame(first_deadline);
    expect_near(schedule.deadline_seconds(), first_deadline + (1.0 / 120.0), 1e-12,
                "on-time frame advances one slot");

    const double before_miss = schedule.deadline_seconds();
    const double late_now = before_miss + (2.5 / 120.0);
    schedule.advance_after_frame(late_now);
    expect_true(schedule.deadline_seconds() > late_now, "missed-frame recovery returns future deadline");
    expect_near(schedule.deadline_seconds(), before_miss + (3.0 / 120.0), 1e-12,
                "missed-frame recovery preserves timeline");

    std::cout << "frame_schedule_tests: PASS\n";
    return EXIT_SUCCESS;
}
