#pragma once
#include <chrono>
#include <thread>
#include <stdexcept>
#include <algorithm>
namespace posmv {
class OutputScheduler {
public:
    using Clock = std::chrono::steady_clock;
    explicit OutputScheduler(unsigned rate) {
        if (rate != 25 && rate != 50 && rate != 100) throw std::invalid_argument("Rate must be 25, 50 or 100");
        period_ = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / rate));
        start_ = deadline_ = Clock::now();
    }
    double wait() {
        std::this_thread::sleep_until(deadline_);
        const auto now = Clock::now();
        const auto late = now - deadline_;
        if (late > std::chrono::milliseconds(1)) ++late_frames;
        max_lateness_ms = std::max(max_lateness_ms, std::chrono::duration<double, std::milli>(late).count());
        deadline_ += period_;
        // Skip missed slots, never burst to catch up or accumulate work duration.
        if (deadline_ <= now) deadline_ += period_ * ((now - deadline_) / period_ + 1);
        return std::chrono::duration<double>(now - start_).count();
    }
    unsigned long long late_frames{};
    double max_lateness_ms{};
private:
    Clock::duration period_{};
    Clock::time_point start_, deadline_;
};
}