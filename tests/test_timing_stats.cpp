#include "diagnostics/timing_stats.hpp"
#include <cmath>
#include <iostream>
namespace {
int failures = 0;
void check(bool ok, const char* behavior) {
    if (!ok) { std::cerr << "FAILED: " << behavior << '\n'; ++failures; }
}
bool near(double actual, double expected, double tolerance = 1e-6) { return std::abs(actual - expected) <= tolerance; }
// One frame every `period` seconds from `start` to `end` inclusive.
void steady(posmv::TimingStats& stats, double start, double end, double period) {
    for (int i = 0; start + i * period <= end + 1e-9; ++i) stats.observe(start + i * period);
}
}
int main() {
    {
        posmv::TimingStats stats;
        steady(stats, 0.0, 2.0, 0.04);
        check(near(stats.current_rate(2.0), 25.0), "current rate counts the frames of the last second");
        check(near(stats.current_rate(3.5), 0.0), "current rate falls to zero once frames stop");
    }
    {
        posmv::TimingStats stats;  // connected at 0 s, first frame only at 5 s
        steady(stats, 5.0, 7.0, 0.04);
        check(near(stats.average_rate(), 25.0, 1e-3), "average rate ignores the wait before the first frame");
    }
    {
        posmv::TimingStats stats;  // input stopped for 22 s in the middle
        steady(stats, 0.0, 1.0, 0.04);
        steady(stats, 23.0, 24.0, 0.04);
        const auto intervals = stats.intervals();
        check(stats.gaps() == 1 && near(intervals.max_ms, 40.0, 1e-6) && near(intervals.mean_ms, 40.0, 1e-6) &&
            near(stats.average_rate(), 25.0, 1e-3), "a pause beyond the gap threshold is a gap, not an interval");
    }
    {
        // 100 intervals: 98 x 40 ms and 2 x 100 ms, then 99 x 40 ms and 1 x 100 ms.
        const auto p99 = [](int slow) {
            posmv::TimingStats stats;
            double t = 0;
            stats.observe(t);
            for (int i = 0; i < 100; ++i) stats.observe(t += (i % 50 == 49 && i / 50 < slow) ? 0.100 : 0.040);
            return stats.intervals().p99_ms;
        };
        check(near(p99(2), 100.0, 0.1) && near(p99(1), 40.0, 0.1), "p99 interval is what 99 % of intervals stay within");
    }
    {
        posmv::TimingStats stats;
        stats.observe(1.00); stats.observe(1.04);
        stats.observe(0.50); stats.observe(std::nan("")); stats.observe(INFINITY);
        stats.observe(1.08);
        const auto intervals = stats.intervals();
        check(intervals.count == 2 && near(intervals.min_ms, 40.0, 1e-6) && near(intervals.max_ms, 40.0, 1e-6),
            "times that go backwards or are not finite are ignored");
    }
    {
        posmv::TimingStats stats;
        steady(stats, 0.0, 1.0, 0.04);
        stats.reset();  // reconnect
        stats.observe(10.00); stats.observe(10.04);
        const auto intervals = stats.intervals();
        check(intervals.count == 1 && stats.gaps() == 0 && near(stats.current_rate(10.04), 2.0) &&
            near(intervals.max_ms, 40.0, 1e-6), "reset starts a new session");
    }
    {
        posmv::TimingStats stats;  // two frames arrived in one serial read
        stats.observe(1.00); stats.observe(1.04); stats.observe(1.04);
        const auto intervals = stats.intervals();
        check(intervals.count == 2 && near(intervals.min_ms, 0.0) && near(intervals.max_ms, 40.0, 1e-6),
            "frames read together count as zero-length intervals");
    }
    return failures ? 1 : 0;
}
