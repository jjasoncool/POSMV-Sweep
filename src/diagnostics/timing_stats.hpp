#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>
namespace posmv {
// Timing of the frames seen on one link (received or sent), in monotonic seconds.
class TimingStats {
public:
    // A pause longer than `gap_seconds` (normally the Stale Timeout) is a gap, not an interval.
    explicit TimingStats(double gap_seconds = 0.5) : gap_(gap_seconds) {}
    void observe(double seconds) {
        if (!std::isfinite(seconds) || seconds < last_) return;
        if (last_ >= 0) {
            const double interval = seconds - last_;
            if (interval > gap_) ++gaps_;
            else {
                const double ms = interval * 1000;
                summary_.min_ms = summary_.count ? std::min(summary_.min_ms, ms) : ms;
                summary_.max_ms = std::max(summary_.max_ms, ms);
                ++summary_.count;
                interval_total_ += interval;
                summary_.mean_ms = interval_total_ * 1000 / summary_.count;
                ++histogram_[std::min<std::size_t>(static_cast<std::size_t>(std::max(ms, 0.0) / bin_ms_), bins_)];
            }
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
    double average_rate() const { return interval_total_ > 0 ? summary_.count / interval_total_ : 0; }
    struct Intervals { std::uint64_t count{}; double min_ms{}, mean_ms{}, max_ms{}, p99_ms{}; };
    Intervals intervals() const {
        auto result = summary_;
        // Centre of the 0.1 ms bin holding the ceil(0.99 n)-th interval; the overflow bin reports the maximum.
        const auto rank = static_cast<std::uint64_t>(std::ceil(0.99 * static_cast<double>(summary_.count)));
        std::uint64_t seen = 0;
        for (std::size_t bin = 0; bin < histogram_.size() && rank; ++bin)
            if ((seen += histogram_[bin]) >= rank) {
                result.p99_ms = bin == bins_ ? summary_.max_ms : (static_cast<double>(bin) + 0.5) * bin_ms_;
                break;
            }
        return result;
    }
    std::uint64_t gaps() const { return gaps_; }
    void reset() { *this = TimingStats(gap_); }
private:
    static constexpr double window_ = 1.0;
    static constexpr double bin_ms_ = 0.1;
    static constexpr std::size_t bins_ = 10000;  // up to 1000 ms, plus one overflow bin
    std::vector<std::uint32_t> histogram_ = std::vector<std::uint32_t>(bins_ + 1);
    double gap_;
    std::deque<double> recent_;
    double last_ = -1;
    Intervals summary_;
    double interval_total_{};
    std::uint64_t gaps_{};
};
}
