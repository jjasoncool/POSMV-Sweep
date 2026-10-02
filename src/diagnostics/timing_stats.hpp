#pragma once
#include "diagnostics/distribution.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
namespace posmv {
// Timing of the frames seen on one link (received or sent), in monotonic seconds.
class TimingStats {
public:
    using Intervals = Distribution::Summary;
    // A pause longer than `gap_seconds` (normally the Stale Timeout) is a gap, not an interval.
    explicit TimingStats(double gap_seconds = 0.5) : gap_(gap_seconds) {}
    void observe(double seconds) {
        if (!std::isfinite(seconds) || seconds < last_) return;
        if (last_ >= 0) {
            const double interval = seconds - last_;
            if (interval > gap_) ++gaps_;
            else { intervals_.add(interval * 1000); interval_total_ += interval; }
        }
        last_ = seconds;
        recent_.push_back(seconds);
        while (!recent_.empty() && recent_.front() <= seconds - window_) recent_.pop_front();
    }
    // Frames per second over the last second before `now`.
    double current_rate(double now) const {
        return static_cast<double>(std::count_if(recent_.begin(), recent_.end(),
            [&](double t) { return t > now - window_ && t <= now; })) / window_;
    }
    // Frames per second while frames were flowing, from the first frame on.
    double average_rate() const { return interval_total_ > 0 ? intervals_.summary().count / interval_total_ : 0; }
    Intervals intervals() const { return intervals_.summary(); }
    std::uint64_t gaps() const { return gaps_; }
    void reset() { *this = TimingStats(gap_); }
private:
    static constexpr double window_ = 1.0;
    double gap_;
    std::deque<double> recent_;
    double last_ = -1;
    Distribution intervals_;
    double interval_total_{};
    std::uint64_t gaps_{};
};
}
