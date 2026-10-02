#include "motion/sweep.hpp"
#include <cmath>
#include <iostream>
namespace {
int failures = 0;
void check(bool ok, const char* behavior) {
    if (!ok) { std::cerr << "FAILED: " << behavior << '\n'; ++failures; }
}
bool near(double actual, double expected, double tolerance = 1e-9) { return std::abs(actual - expected) <= tolerance; }
}
int main() {
    {
        posmv::SweepConfig config;
        config.mode = posmv::SweepMode::Fixed;
        config.fixed_offset_deg = 5;
        check(near(posmv::synthetic_roll(config, 0), 5) && near(posmv::synthetic_roll(config, 7.3), 5),
            "fixed mode holds the same Offset at every moment");
    }
    {
        const auto rejected = [](double offset) {
            posmv::SweepConfig config;
            config.mode = posmv::SweepMode::Fixed;
            config.fixed_offset_deg = offset;
            try { config.validate(); } catch (const std::invalid_argument&) { return true; }
            return false;
        };
        check(rejected(90.5) && rejected(-91) && rejected(std::nan("")) && !rejected(-90),
            "a fixed Offset beyond ±90° or not finite is rejected");
    }
    {
        posmv::SweepConfig sine;  // ±10° over 4 s
        posmv::SweepConfig fixed;
        fixed.mode = posmv::SweepMode::Fixed;
        fixed.fixed_offset_deg = 5;
        check(near(posmv::peak_sweep_rate(sine), 15.707963267948966) && near(posmv::peak_sweep_rate(fixed), 0),
            "peak Offset rate is 2·pi·Angle/Cycle for a sine and zero when fixed");
    }
    {
        posmv::SweepConfig sine;  // CENTER every 2 s
        posmv::SweepConfig fixed;
        fixed.mode = posmv::SweepMode::Fixed;
        check(near(posmv::next_center(sine, 0.5), 2.0) && near(posmv::next_center(sine, 2.0), 2.0) &&
            near(posmv::next_center(sine, 3.1), 4.0) && near(posmv::next_center(fixed, 3.1), 3.1),
            "finishing at CENTER waits for the next zero crossing of the sine; a fixed Offset stops at once");
    }
    return failures ? 1 : 0;
}
