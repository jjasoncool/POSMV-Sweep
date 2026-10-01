#include <stdexcept>
#include "simulator/fixed_frame_check.hpp"
#include "diagnostics/intervals.hpp"
#include <cmath>
#include <limits>
int main() {
    posmv::Intervals timing;
    timing.observe(1.0); timing.observe(1.02); timing.observe(1.06);
    if (timing.intervals != 2 || std::abs(timing.average_ms - 30) > 1e-8 ||
        std::abs(timing.minimum_ms - 20) > 1e-8 || std::abs(timing.maximum_ms - 40) > 1e-8) return 7;
    timing.observe(0.5); timing.observe(std::numeric_limits<double>::quiet_NaN());
    timing.observe(std::numeric_limits<double>::infinity());
    if (timing.intervals != 2) return 8;
    timing.observe(1.06);
    if (timing.intervals != 3 || timing.minimum_ms != 0 || std::abs(timing.average_ms - 20) > 1e-8) return 9;
    const std::string frame = ":000000  0000G 0000  0000\r\n";
    posmv::FixedFrameCheck check(frame);
    if (check.passed()) return 1;
    check.feed(frame.substr(0, 9)); check.feed(frame.substr(9));
    check.feed(frame); check.finish();
    if (!check.passed()) return 2;
    check.feed(":000000  0000G 0001  0000\r\n");
    if (check.passed() || check.mismatches() != 1) return 3;
    posmv::FixedFrameCheck garbage(frame); garbage.feed(frame + "x");
    if (garbage.passed()) return 4;
    posmv::FixedFrameCheck truncated(frame); truncated.feed(frame + ":partial"); truncated.finish();
    if (truncated.passed()) return 5;
    try { posmv::FixedFrameCheck invalid("bad"); return 6; } catch(const std::invalid_argument&) {}
}